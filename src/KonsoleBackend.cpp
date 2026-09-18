// SPDX-License-Identifier: GPL-3.0-or-later
#include "KonsoleBackend.h"

#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <kde_terminal_interface.h>

#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QMetaObject>
#include <QScrollBar>
#include <QWidget>

#include <stdexcept>

namespace relay {

KonsoleBackend::KonsoleBackend(QObject *parent)
    : QObject(parent)
{
#if QT_VERSION_MAJOR >= 6
    const auto factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/konsolepart")));
    if (!factory.plugin)
        throw std::runtime_error("Qt6 KonsolePart could not be loaded. Install the KDE Frameworks 6 version of Konsole.");
#else
    // KF5 Konsole (e.g. Ubuntu 24.04 konsole-kpart) installs the part at the plugin root.
    auto factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("konsolepart")));
    if (!factory.plugin)
        factory = KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf5/parts/konsolepart")));
    if (!factory.plugin)
        throw std::runtime_error("Qt5 KonsolePart could not be loaded. Install the KDE Frameworks 5 Konsole part (konsole-kpart).");
#endif
    m_part = factory.plugin->create<KParts::ReadOnlyPart>(this);
    if (!m_part)
        throw std::runtime_error("KonsolePart could not be created.");
    m_iface = qobject_cast<TerminalInterface *>(m_part.data());
    if (!m_iface)
        throw std::runtime_error("KonsolePart does not provide TerminalInterface.");
    m_widget = m_part->widget();

    connect(m_part.data(), &QObject::destroyed, this, [this] {
        m_iface = nullptr;
        m_widget = nullptr;
        m_session = nullptr;
        m_watchedSession = nullptr;
        m_sessionProbe.stop();
        if (!m_destroying && onFinished)
            onFinished(-1);
    });

    // Konsole's Session object appears on the session bus only once the part has
    // started its shell; poll briefly until it can be found and watched.
    m_sessionProbe.setInterval(200);
    connect(&m_sessionProbe, &QTimer::timeout, this, [this] {
        connectSession();
        if (m_watchedSession)
            m_sessionProbe.stop();
    });
}

KonsoleBackend::~KonsoleBackend()
{
    m_destroying = true;
    m_sessionProbe.stop();
    if (m_part)
        delete m_part.data();
}

bool KonsoleBackend::startProgram(const QString &program, const QStringList &args, const QString & /*workingDirectory*/,
                                  const QStringList & /*extraEnvironment*/)
{
    // KonsolePart inherits this process's environment (Relay sets RELAY_* with
    // qputenv before constructing the backend) and starts in Konsole's own default
    // directory; Relay's Bash integration changes to RELAY_START_DIR afterwards.
    // Konsole's argument list is the full argv, so argv[0] is prepended here.
    if (!m_iface)
        return false;
    m_iface->startProgram(program, QStringList{program} + args);
    m_sessionProbe.start();
    return true;
}

void KonsoleBackend::sendInput(const QByteArray &bytes)
{
    if (m_iface)
        m_iface->sendInput(QString::fromUtf8(bytes));
}

void KonsoleBackend::sendText(const QString &text, bool asPaste)
{
    // KonsolePart has no bracketed-paste-aware injection; sendInput() is what
    // Relay has always used for both.
    Q_UNUSED(asPaste);
    if (m_iface)
        m_iface->sendInput(text);
}

qint64 KonsoleBackend::shellPid() const
{
    return m_iface ? const_cast<TerminalInterface *>(m_iface)->terminalProcessId() : 0;
}

qint64 KonsoleBackend::foregroundProcessId() const
{
    return m_iface ? const_cast<TerminalInterface *>(m_iface)->foregroundProcessId() : 0;
}

bool KonsoleBackend::isRunning() const { return m_iface != nullptr; }

QObject *KonsoleBackend::konsoleSession()
{
    if (m_session)
        return m_session;
    if (!m_iface)
        return nullptr;
    const int pid = const_cast<TerminalInterface *>(m_iface)->terminalProcessId();
    for (int n = 1; n <= 256 && pid > 0; ++n) {
        QObject *session = QDBusConnection::sessionBus().objectRegisteredAt(QStringLiteral("/Sessions/%1").arg(n));
        if (!session)
            continue;
        for (QObject *child : session->children()) {
            int childPid = 0;
            if (child->metaObject()->indexOfMethod("processId()") >= 0
                && QMetaObject::invokeMethod(child, "processId", Qt::DirectConnection, Q_RETURN_ARG(int, childPid))
                && childPid == pid) {
                m_session = session;
                return session;
            }
        }
    }
    return nullptr;
}

void KonsoleBackend::connectSession()
{
    QObject *session = konsoleSession();
    if (!session || session == m_watchedSession)
        return;
    m_watchedSession = session;
    // Konsole emits primaryScreenInUse(bool) when a program switches to or from the
    // alternate screen (vim, less, htop, tmux); connecting to a string-based signal
    // needs a real slot, hence the public slot on this object.
    QObject::connect(session, SIGNAL(primaryScreenInUse(bool)), this, SLOT(primaryScreenInUse(bool)));
}

void KonsoleBackend::primaryScreenInUse(bool primary)
{
    m_altScreen = !primary;
    if (onAltScreenChanged)
        onAltScreenChanged(m_altScreen);
}

void KonsoleBackend::writeToDisplay(const QByteArray &bytes)
{
    // Session::onReceiveBlock() feeds bytes to the terminal emulator exactly like
    // program output. Nothing is typed into the shell, so agent text never reaches
    // shell history and is never executed.
    QObject *session = konsoleSession();
    if (!session) {
        fprintf(stderr, "%s", bytes.constData());
        return;
    }
    QMetaObject::invokeMethod(session, "onReceiveBlock", Qt::DirectConnection, Q_ARG(const char *, bytes.constData()),
                              Q_ARG(int, bytes.size()));
}

void KonsoleBackend::redrawPrompt()
{
    if (m_iface && !m_redrawSequence.isEmpty())
        m_iface->sendInput(QString::fromLatin1(m_redrawSequence));
}

int KonsoleBackend::capabilities() const
{
    // Alternate-screen state comes from the Session signal above; inline output from
    // its onReceiveBlock() slot; scrolling from the (possibly hidden) scrollbar.
    int caps = AltScreenState | DisplayInjection | ScrollControl;
    if (m_session && m_session->metaObject()->indexOfMethod("getDisplayedText()") >= 0)
        caps |= ScreenText; // KF6 Konsole only
    if (displayHasSlot("increaseFontSize()") && displayHasSlot("decreaseFontSize()"))
        caps |= FontZoom;
    return caps;
}

QString KonsoleBackend::screenText() const
{
    QObject *session = const_cast<KonsoleBackend *>(this)->konsoleSession();
    if (!session || session->metaObject()->indexOfMethod("getDisplayedText()") < 0)
        return {};
    QString text;
    QMetaObject::invokeMethod(session, "getDisplayedText", Qt::DirectConnection, Q_RETURN_ARG(QString, text));
    return text;
}

QStringList KonsoleBackend::scrollbackText(int) const { return {}; } // KonsolePart exposes no history

int KonsoleBackend::rows() const
{
    QObject *session = const_cast<KonsoleBackend *>(this)->konsoleSession();
    int value = 0;
    if (session && session->metaObject()->indexOfMethod("lines()") >= 0)
        QMetaObject::invokeMethod(session, "lines", Qt::DirectConnection, Q_RETURN_ARG(int, value));
    return value;
}

int KonsoleBackend::columns() const
{
    QObject *session = const_cast<KonsoleBackend *>(this)->konsoleSession();
    int value = 0;
    if (session && session->metaObject()->indexOfMethod("columns()") >= 0)
        QMetaObject::invokeMethod(session, "columns", Qt::DirectConnection, Q_RETURN_ARG(int, value));
    return value;
}

QString KonsoleBackend::title() const
{
    QObject *session = const_cast<KonsoleBackend *>(this)->konsoleSession();
    QString value;
    if (session && session->metaObject()->indexOfMethod("title(int)") >= 0)
        QMetaObject::invokeMethod(session, "title", Qt::DirectConnection, Q_RETURN_ARG(QString, value), Q_ARG(int, 1));
    return value;
}

QString KonsoleBackend::currentDirectory() const
{
    return m_iface ? m_iface->currentWorkingDirectory() : QString();
}

// KonsolePart derives its grid from the widget size and its profile; neither can be
// set through KParts, so these follow the layout instead.
void KonsoleBackend::resizeTerminal(int, int) {}
void KonsoleBackend::setTerminalFont(const QFont &) {}

QObject *KonsoleBackend::display() const
{
    if (!m_widget)
        return nullptr;
    if (m_widget->metaObject()->indexOfMethod("copyToClipboard()") >= 0)
        return m_widget;
    for (QObject *child : m_widget->findChildren<QObject *>())
        if (child->metaObject()->indexOfMethod("copyToClipboard()") >= 0)
            return child;
    return nullptr;
}

void KonsoleBackend::invokeOnDisplay(const char *slot)
{
    if (QObject *view = display())
        QMetaObject::invokeMethod(view, slot, Qt::DirectConnection);
}

bool KonsoleBackend::displayHasSlot(const char *signature) const
{
    const QObject *view = display();
    return view && view->metaObject()->indexOfMethod(signature) >= 0;
}

bool KonsoleBackend::zoom(int step)
{
    // resetFontSize() is newer than the two steps; without it, "Reset zoom" is not offered.
    const char *slot = step > 0 ? "increaseFontSize" : step < 0 ? "decreaseFontSize" : "resetFontSize";
    const QByteArray signature = QByteArray(slot) + "()";
    if (!displayHasSlot(signature.constData()))
        return false;
    invokeOnDisplay(slot);
    return true;
}

// Konsole exposes no "has selection" query and its copy does nothing without one;
// callers detect a clipboard change to learn whether anything was copied.
void KonsoleBackend::copySelection() { invokeOnDisplay("copyToClipboard"); }
void KonsoleBackend::paste() { invokeOnDisplay("pasteFromClipboard"); }
void KonsoleBackend::selectAll() { invokeOnDisplay("selectAll"); }

QString KonsoleBackend::selectedText() const { return {}; } // no query in KonsolePart

void KonsoleBackend::clearScrollback()
{
    if (QObject *session = konsoleSession())
        QMetaObject::invokeMethod(session, "clearHistory", Qt::DirectConnection);
}

void KonsoleBackend::clear()
{
    clearScrollback();
    writeToDisplay(QByteArrayLiteral("\x1b[H\x1b[2J"));
}

QScrollBar *KonsoleBackend::scrollBar() const
{
    if (!m_widget)
        return nullptr;
    for (QScrollBar *candidate : m_widget->findChildren<QScrollBar *>())
        if (candidate->orientation() == Qt::Vertical)
            return candidate;
    return nullptr;
}

void KonsoleBackend::scrollLines(int lines)
{
    if (QScrollBar *bar = scrollBar())
        bar->setValue(bar->value() + lines);
}

void KonsoleBackend::scrollPages(int pages)
{
    if (QScrollBar *bar = scrollBar())
        bar->setValue(bar->value() + pages * std::max(1, bar->pageStep() - 1));
}

void KonsoleBackend::scrollToBottom()
{
    if (QScrollBar *bar = scrollBar())
        bar->setValue(bar->maximum());
}

} // namespace relay
