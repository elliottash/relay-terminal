// SPDX-License-Identifier: GPL-3.0-or-later
#include "TerminalSession.h"

#include <QMetaObject>
#include <QPointer>

#include <chrono>
#include <thread>

namespace relay {

TerminalSession::GuiLock::GuiLock(const TerminalSession *session)
    : s(session)
{
    s->m_guiWaiting.fetch_add(1);
    s->m_mutex.lock();
    s->m_guiWaiting.fetch_sub(1);
}

TerminalSession::GuiLock::~GuiLock()
{
    s->m_mutex.unlock();
}

TerminalSession::TerminalSession(const QString &coreName, QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<relay::PromptMark>("relay::PromptMark");
    m_core = createVtCore(coreName, m_rows, m_cols);
    if (!m_core)
        m_core = createVtCore(QString(), m_rows, m_cols);
    m_core->resize(m_rows, m_cols, 8, 16);

    // Core events run with m_mutex held (inside feed or an input call).
    VtCore::Events &ev = m_core->events;
    ev.reply = [this](const char *d, size_t n) {
        if (m_pty)
            m_pty->write(d, n);
    };
    ev.titleChanged = [this](const QString &t) {
        m_title = t;
        m_events.push_back({Event::Title, t, {}, {}, 0, 0});
    };
    ev.cwdChanged = [this](const QString &path, const QString &host) {
        m_cwd = path;
        m_events.push_back({Event::Cwd, path, host, {}, 0, 0});
    };
    ev.bell = [this] { m_events.push_back({Event::Bell, {}, {}, {}, 0, 0}); };
    ev.altScreenChanged = [this](bool a) {
        m_alt = a;
        m_events.push_back({Event::Alt, {}, {}, {}, a ? 1 : 0, 0});
    };
    ev.promptMark = [this](PromptMark k, int row, int code) {
        m_events.push_back({Event::Mark, {}, {}, {}, int(k) | (row << 8), code});
    };
    ev.clipboardWrite = [this](const QString &target, const QByteArray &data) {
        m_events.push_back({Event::Clipboard, target, {}, data, 0, 0});
    };
    ev.notification = [this](const QString &t, const QString &b) { m_events.push_back({Event::Notify, t, b, {}, 0, 0}); };
}

TerminalSession::~TerminalSession()
{
    // Stop the I/O thread before the core goes away.
    m_pty.reset();
}

QString TerminalSession::coreName() const
{
    return QString::fromLatin1(m_core->name());
}

bool TerminalSession::start(const StartOptions &o)
{
    if (m_pty) {
        m_error = QStringLiteral("session already started");
        return false;
    }
    m_pty = Pty::create();
    m_pty->onOutput = [this](const char *d, size_t n) { onPtyOutput(d, n); };
    m_pty->onFinished = [this](int code) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_events.push_back({Event::Finished, {}, {}, {}, code, 0});
        }
        scheduleDelivery();
    };
    Pty::StartOptions po;
    po.program = o.program;
    po.arguments = o.arguments;
    po.workingDirectory = o.workingDirectory;
    po.environment = QStringList{QStringLiteral("TERM=xterm-256color"), QStringLiteral("COLORTERM=truecolor"),
                                 QStringLiteral("TERM_PROGRAM=Relay")}
        + o.environment;
    po.rows = m_rows;
    po.cols = m_cols;
    if (!m_pty->start(po)) {
        m_error = m_pty->errorString();
        m_pty.reset();
        return false;
    }
    return true;
}

QString TerminalSession::errorString() const { return m_error; }
bool TerminalSession::isRunning() const { return m_pty && m_pty->isRunning(); }
qint64 TerminalSession::shellPid() const { return m_pty ? m_pty->childPid() : -1; }
qint64 TerminalSession::foregroundPid() const { return m_pty ? m_pty->foregroundPid() : -1; }

void TerminalSession::terminate()
{
    if (m_pty)
        m_pty->terminate();
}

void TerminalSession::onPtyOutput(const char *data, size_t len)
{
    m_bytes.fetch_add(len);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_core->feed(data, len);
        if (m_outputSignal.load() && m_pendingOutput.size() < (64 << 20))
            m_pendingOutput.append(data, int(len));
    }
    m_contentDirty = true;
    scheduleDelivery();
    // Let a waiting GUI thread (paint snapshot, key press) take the lock before
    // the next chunk; std::mutex is not fair.
    for (int spins = 0; m_guiWaiting.load() > 0 && spins < 2000; ++spins)
        std::this_thread::yield();
}

void TerminalSession::scheduleDelivery()
{
    if (m_deliveryQueued.exchange(true))
        return;
    QMetaObject::invokeMethod(this, &TerminalSession::deliver, Qt::QueuedConnection);
}

void TerminalSession::deliver()
{
    m_deliveryQueued = false;
    std::vector<Event> events;
    QByteArray out;
    {
        GuiLock lock(this);
        events.swap(m_events);
        out.swap(m_pendingOutput);
    }
    QPointer<TerminalSession> self(this);
    if (!out.isEmpty())
        emit output(out);
    for (const Event &e : events) {
        if (!self)
            return;
        switch (e.kind) {
        case Event::Title: emit titleChanged(e.a); break;
        case Event::Cwd: emit cwdChanged(e.a, e.b); break;
        case Event::Bell: emit bell(); break;
        case Event::Alt: emit altScreenChanged(e.i != 0); break;
        case Event::Mark: emit promptMark(PromptMark(e.i & 0xFF), e.i >> 8, e.j); break;
        case Event::Clipboard: emit clipboardWriteRequested(e.a, e.bytes); break;
        case Event::Notify: emit notification(e.a, e.b); break;
        case Event::Finished: break; // after contentChanged below
        }
    }
    if (self && m_contentDirty.exchange(false))
        emit contentChanged();
    for (const Event &e : events) {
        if (self && e.kind == Event::Finished)
            emit finished(e.i);
    }
}

void TerminalSession::resize(int rows, int cols, int cellWidthPx, int cellHeightPx)
{
    rows = std::max(1, rows);
    cols = std::max(2, cols);
    {
        GuiLock lock(this);
        m_core->resize(rows, cols, cellWidthPx, cellHeightPx);
        m_rows = rows;
        m_cols = cols;
    }
    if (m_pty)
        m_pty->resize(rows, cols, cols * cellWidthPx, rows * cellHeightPx);
    m_contentDirty = true;
    scheduleDelivery();
}

int TerminalSession::rows() const
{
    GuiLock lock(this);
    return m_rows;
}

int TerminalSession::columns() const
{
    GuiLock lock(this);
    return m_cols;
}

void TerminalSession::sendInput(const QByteArray &bytes)
{
    if (m_pty)
        m_pty->write(bytes);
}

void TerminalSession::writeToDisplay(const QByteArray &bytes)
{
    {
        GuiLock lock(this);
        m_core->feed(bytes.constData(), size_t(bytes.size()));
    }
    m_contentDirty = true;
    scheduleDelivery();
}

QString TerminalSession::screenText()
{
    GuiLock lock(this);
    return m_core->screenText();
}

QStringList TerminalSession::scrollbackText(int maxLines)
{
    GuiLock lock(this);
    return m_core->historyText(maxLines);
}

QString TerminalSession::title() const
{
    GuiLock lock(this);
    return m_title;
}

QString TerminalSession::currentDirectory() const
{
    GuiLock lock(this);
    return m_cwd;
}

void TerminalSession::setScrollbackLines(int lines)
{
    GuiLock lock(this);
    m_core->setScrollbackLines(lines);
}

void TerminalSession::setClipboardWriteAllowed(bool allowed)
{
    GuiLock lock(this);
    m_core->setClipboardWriteAllowed(allowed);
}

void TerminalSession::setOutputSignalEnabled(bool enabled)
{
    m_outputSignal = enabled;
}

} // namespace relay
