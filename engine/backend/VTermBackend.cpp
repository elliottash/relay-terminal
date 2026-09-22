// SPDX-License-Identifier: AGPL-3.0-or-later
#include "VTermBackend.h"

#include "core/AnsiSerializer.h"
#include "core/CellTypes.h"
#include "session/TerminalSession.h"
#include "view/TerminalView.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QScrollBar>

namespace relay {

VTermBackend::VTermBackend(const QString &coreName, QWidget *parent)
    : QObject(parent)
{
    m_container = new QWidget(parent);
    m_session = new TerminalSession(coreName, this);
    m_view = new TerminalView(m_session, m_container);
    m_scrollBar = new QScrollBar(Qt::Vertical, m_container);
    auto *layout = new QHBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_scrollBar);
    m_container->setFocusProxy(m_view);

    // The container owns the widgets; the backend (with its session and shell)
    // dies with the container unless the host deletes the backend first.
    connect(m_container, &QObject::destroyed, this, [this] {
        m_container = nullptr;
        m_view = nullptr;
        m_scrollBar = nullptr;
        deleteLater();
    });

    connect(m_view, &TerminalView::scrollPositionChanged, this, [this](int top, int history, int rows) {
        m_updatingScrollBar = true;
        m_scrollBar->setRange(0, history);
        m_scrollBar->setPageStep(rows);
        m_scrollBar->setValue(top);
        m_updatingScrollBar = false;
    });
    connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        // The bar counts visual rows: with a fold open its range includes the
        // fold's own rows, and with none it is the core's scrollback again.
        if (!m_updatingScrollBar && m_view)
            m_view->scrollToVisualRow(value);
    });
    m_view->onFoldRequested = [this](const QString &uri) {
        if (onFoldRequested)
            onFoldRequested(uri);
    };
    connect(m_view, &TerminalView::linkActivated, this,
            [this](const QString &target, int line, int column, Qt::KeyboardModifiers modifiers) {
        if (onLinkActivated)
            onLinkActivated(target, line, column, modifiers);
    });
    connect(m_view, &TerminalView::bellRang, this, [this] {
        if (onBell)
            onBell();
    });
    connect(m_session, &TerminalSession::titleChanged, this, [this](const QString &t) {
        if (onTitleChanged)
            onTitleChanged(t);
    });
    connect(m_session, &TerminalSession::cwdChanged, this, [this](const QString &path, const QString &host) {
        if (onCwdChanged)
            onCwdChanged(path);
        if (onCwdHostChanged)
            onCwdHostChanged(path, host);
    });
    connect(m_session, &TerminalSession::altScreenChanged, this, [this](bool a) {
        if (onAltScreenChanged)
            onAltScreenChanged(a);
    });
    connect(m_session, &TerminalSession::promptMark, this, [this](PromptMark k, int, int exitCode) {
        if (!onPromptMark)
            return;
        const char kind = k == MarkPromptStart ? 'A' : k == MarkCommandStart ? 'B' : k == MarkOutputStart ? 'C' : 'D';
        onPromptMark(kind, exitCode);
    });
    connect(m_session, &TerminalSession::output, this, [this](const QByteArray &b) {
        if (onOutput)
            onOutput(b);
    });
    connect(m_session, &TerminalSession::finished, this, [this](int code) {
        if (onFinished)
            onFinished(code);
    });
}

VTermBackend::~VTermBackend()
{
    delete m_container; // view first, then the session (child QObject) with its PTY thread
}

bool VTermBackend::startProgram(const QString &program, const QStringList &args, const QString &workingDirectory,
                                const QStringList &extraEnvironment)
{
    TerminalSession::StartOptions o;
    o.program = program;
    o.arguments = args;
    o.workingDirectory = workingDirectory;
    o.environment = extraEnvironment;
    return m_session->start(o);
}

void VTermBackend::sendInput(const QByteArray &bytes) { m_session->sendInput(bytes); }

void VTermBackend::sendText(const QString &text, bool asPaste)
{
    if (asPaste)
        m_session->withCore([&](VtCore &c) { c.paste(text); });
    else
        m_session->withCore([&](VtCore &c) { c.sendText(text); });
}

qint64 VTermBackend::shellPid() const { return m_session->shellPid(); }
qint64 VTermBackend::foregroundProcessId() const { return m_session->foregroundPid(); }
bool VTermBackend::isRunning() const { return m_session->isRunning(); }

TerminalBackend::TermiosFlags VTermBackend::termiosFlags() const
{
    const Pty::TermiosFlags flags = m_session->termiosFlags();
    return {flags.valid, flags.canonical, flags.echo};
}

void VTermBackend::writeToDisplay(const QByteArray &bytes) { m_session->writeToDisplay(bytes); }

void VTermBackend::holdProgramResize(bool hold) { m_session->holdPtyResize(hold); }

void VTermBackend::redrawPrompt()
{
    if (!m_redrawSequence.isEmpty())
        m_session->sendInput(m_redrawSequence);
}

int VTermBackend::capabilities() const
{
    return ScreenText | Scrollback | AltScreenState | LinkClicks | Osc8Links | PromptMarks | CwdTracking | DisplayInjection
        | Search | ScrollControl | FontZoom | LinkWalk | LineDiscipline | Folds | ClipboardWrite | FormattedText;
}

void VTermBackend::setClipboardWriteAllowed(bool allowed)
{
    if (m_view)
        m_view->setClipboardWriteAllowed(allowed);
}

// ---- folds (#TK9C): straight through to the view, which owns the layer.

void VTermBackend::setFoldPrefix(const QString &uriPrefix)
{
    if (m_view)
        m_view->setFoldPrefix(uriPrefix);
}

void VTermBackend::setFoldContent(const QString &uri, const QVector<FoldLine> &lines)
{
    if (m_view)
        m_view->setFoldContent(uri, lines);
}

void VTermBackend::setProseBlock(const QString &uri, const QVector<FoldLine> &lines, int printColumns)
{
    if (m_view)
        m_view->setProseBlock(uri, lines, printColumns);
}

void VTermBackend::setFoldExpanded(const QString &uri, bool expanded)
{
    if (m_view)
        m_view->setFoldExpanded(uri, expanded);
}

bool VTermBackend::foldExpanded(const QString &uri) const { return m_view && m_view->foldExpanded(uri); }

void VTermBackend::removeFold(const QString &uri)
{
    if (m_view)
        m_view->removeFold(uri);
}

void VTermBackend::clearFolds()
{
    if (m_view)
        m_view->clearFolds();
}

QStringList VTermBackend::expandedFolds() const { return m_view ? m_view->expandedFolds() : QStringList(); }

bool VTermBackend::toggleFold(const QString &uri) { return m_view && m_view->toggleFold(uri); }

bool VTermBackend::stepLink(int delta, Link *link, int *index, int *count)
{
    if (!m_view)
        return false;
    TerminalView::Link found;
    if (!m_view->stepLink(delta, &found))
        return false;
    if (link) {
        link->target = found.target;
        link->text = found.text;
        link->card = found.card;
        link->cardTitle = found.cardTitle;
        link->url = found.url;
        link->directory = found.directory;
        link->line = found.line;
        link->column = found.column;
    }
    if (index)
        *index = m_view->linkWalkIndex();
    if (count)
        *count = m_view->linkWalkCount();
    return true;
}

void VTermBackend::endLinkWalk()
{
    if (m_view)
        m_view->endLinkWalk();
}

bool VTermBackend::linkWalkActive() const { return m_view && m_view->linkWalkActive(); }

void VTermBackend::setPlainClickOpensLinks(bool on)
{
    if (m_view)
        m_view->setPlainClickOpensLinks(on);
}

void VTermBackend::setCardLookup(std::function<bool(const QString &id, QString *title)> lookup)
{
    if (m_view)
        m_view->setCardLookup(std::move(lookup));
}

void VTermBackend::setLinkProbe(std::function<int(const QString &absolutePath)> probe,
                                std::function<QString()> directory)
{
    if (!m_view)
        return;
    relay::links::Probe wrapped;
    if (probe)
        wrapped = [probe](const QString &path) { return relay::links::Entry(probe(path)); };
    m_view->setLinkProbe(std::move(wrapped), std::move(directory));
}

void VTermBackend::linkProbeAnswered()
{
    if (m_view)
        m_view->linkProbeUpdated();
}

QString VTermBackend::screenText() const { return m_session->screenText(); }

QString VTermBackend::formattedScreenText() const
{
    return m_session->withCore([](VtCore &core) {
        ViewportFrame frame;
        core.updateFrame(&frame, true);
        const QStringList lines = linesToAnsi(frame.lines);
        return lines.join(QLatin1Char('\n'));
    });
}

QPoint VTermBackend::cursorPosition() const { return m_session->cursorPosition(); }
QStringList VTermBackend::scrollbackText(int maxLines) const { return m_session->scrollbackText(maxLines); }

QStringList VTermBackend::formattedScrollbackText(int maxLines) const
{
    return m_session->withCore([maxLines](VtCore &core) {
        const int total = core.historyRows();
        const int want = std::max(0, std::min(maxLines, total));
        std::vector<Line> lines;
        if (want > 0)
            core.historyLines(total - want, want, &lines);
        return linesToAnsi(lines);
    });
}
bool VTermBackend::altScreen() const { return m_session->altScreen(); }
int VTermBackend::rows() const { return m_session->rows(); }
int VTermBackend::columns() const { return m_session->columns(); }
QString VTermBackend::title() const { return m_session->title(); }

QString VTermBackend::currentDirectory() const
{
    const QString osc7 = m_session->currentDirectory();
    if (!osc7.isEmpty())
        return osc7;
#if defined(Q_OS_LINUX)
    qint64 pid = m_session->foregroundPid();
    if (pid <= 0)
        pid = m_session->shellPid();
    if (pid > 0)
        return QFileInfo(QStringLiteral("/proc/%1/cwd").arg(pid)).symLinkTarget();
#endif
    return QString();
}

void VTermBackend::resizeTerminal(int rows, int columns)
{
    if (!m_view)
        return;
    const QSize grid = m_view->sizeForGrid(rows, columns);
    m_view->setMinimumSize(grid);
    m_view->resize(grid);
    m_container->resize(grid.width() + m_scrollBar->sizeHint().width(), grid.height());
    m_view->setMinimumSize(QSize(m_view->cellWidth() * 2, m_view->cellHeight()));
}

QWidget *VTermBackend::widget() { return m_container; }
QWidget *VTermBackend::focusWidget() { return m_view; }

// The methods below are no-ops once the host destroyed widget().
void VTermBackend::setTerminalFont(const QFont &font)
{
    if (m_view)
        m_view->setTerminalFont(font);
}

void VTermBackend::copySelection()
{
    if (m_view)
        m_view->copySelection();
}

void VTermBackend::paste()
{
    if (m_view)
        m_view->pasteClipboard();
}

QString VTermBackend::selectedText() const
{
    // Through the view: a selection that touches an open fold is the view's,
    // and comes back in the order the rows are displayed.
    if (m_view)
        return m_view->selectedText();
    return m_session->withCore([](VtCore &c) { return c.selectedText(); });
}

void VTermBackend::selectAll()
{
    if (m_view)
        m_view->selectAll();
}

void VTermBackend::clearScrollback()
{
    m_session->withCore([](VtCore &c) { c.clearScrollback(); });
    if (m_view)
        m_view->scrollToBottom();
}

void VTermBackend::clear()
{
    m_session->withCore([](VtCore &c) { c.clearScrollback(); });
    m_session->writeToDisplay(QByteArrayLiteral("\x1b[H\x1b[2J"));
    if (m_view)
        m_view->scrollToBottom();
}

void VTermBackend::scrollLines(int lines)
{
    if (m_view)
        m_view->scrollLines(lines);
}

void VTermBackend::scrollPages(int pages)
{
    if (m_view)
        m_view->scrollPages(pages);
}

void VTermBackend::scrollToBottom()
{
    if (m_view)
        m_view->scrollToBottom();
}

bool VTermBackend::viewportAtBottom() const
{
    return !m_view || m_view->viewportAtBottom();
}

bool VTermBackend::scrollToPrompt(int direction)
{
    return m_view && m_view->scrollToPrompt(direction);
}

int VTermBackend::find(const QString &text, bool backwards)
{
    return m_view ? m_view->find(text, backwards) : 0;
}

// widget() is the container that also holds the scroll bar, so the point is mapped onto the view.
QString VTermBackend::linkAt(const QPoint &pos, int *line, int *column)
{
    if (line)
        *line = -1;
    if (column)
        *column = -1;
    if (!m_view || !m_container)
        return {};
    const TerminalView::Link link = m_view->linkAtPoint(m_view->mapFrom(m_container, pos));
    if (!link.valid())
        return {};
    if (line)
        *line = link.line;
    if (column)
        *column = link.column;
    return link.target;
}

bool VTermBackend::zoom(int step)
{
    if (!m_view)
        return false;
    if (step > 0)
        m_view->zoomIn();
    else if (step < 0)
        m_view->zoomOut();
    else
        m_view->resetZoom();
    return true;
}

void VTermBackend::setOutputCallbackEnabled(bool enabled) { m_session->setOutputSignalEnabled(enabled); }

} // namespace relay
