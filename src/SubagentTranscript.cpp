#include "PaneTabNavigation.h"
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SubagentTranscript.h"
#include "CopyOnSelect.h"
#include "Theme.h"
#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace relay {

namespace {
// Model and tool output is untrusted; drop control characters except newline and tab.
QString sanitize(const QString &text) {
    QString clean;
    clean.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) clean += c;
    }
    return clean;
}

calllines::Palette transcriptPalette(bool prose = false) {
    calllines::Palette p;
    p.text = prose ? theme::Agent : theme::Text;
    p.muted = prose ? theme::Text : theme::TextMuted;
    p.code = theme::SyntaxCommand;
    p.link = theme::Link;
    p.error = theme::SyntaxUnknown;
    p.accent = theme::Accent;
    return p;
}

void insertMarkdown(QTextCursor &cursor, const QVector<FoldLine> &lines, bool finalNewline) {
    for (int i = 0; i < lines.size(); ++i) {
        for (const auto &span : lines.at(i).spans) {
            QTextCharFormat f;
            f.setForeground(span.fg.isValid() ? span.fg : theme::Text);
            f.setFontWeight(span.bold ? QFont::Bold : QFont::Normal);
            f.setFontItalic(span.italic);
            f.setFontUnderline(span.underline);
            cursor.insertText(span.text, f);
        }
        if (i + 1 < lines.size() || finalNewline) cursor.insertText(QStringLiteral("\n"), QTextCharFormat{});
    }
}

QString thinkingDisplay() {
    QSettings settings;
    const QString value = settings.value(QStringLiteral("agent/thinking_display")).toString();
    if (!value.isEmpty()) return value;
    return settings.value(QStringLiteral("agent/show_thinking"), true).toBool()
        ? QStringLiteral("collapse") : QStringLiteral("never");
}

}  // namespace

// The transcript's inks, read from the live theme tokens at write time (a light theme gets dark
// text; these were Relay Dark's values hard-coded, which put near-white prose on a light pane).
// Machine lines (tools, their output, notes) are muted and upright: italic muted monospace was the
// hardest text in the app to read (owner, 2026-09-18; docs/ARCHITECTURE.md, "Legible text").
QColor SubagentTranscriptView::inkColor(Ink ink) {
    switch (ink) {
    case Ink::Agent: return theme::Text;
    case Ink::User: return theme::Agent;
    case Ink::Tool: case Ink::ToolOutput: case Ink::Note: return theme::TextMuted;
    // A diff's add/remove lines are drawn as a fill with black-or-white ink on it (as everywhere
    // else since 2026-09-19), so these inks are the ink that reads on their own fill, not the
    // green and the red — those are the backgrounds, set alongside in append().
    case Ink::DiffAdd: return theme::contrastInk(theme::Success);
    case Ink::DiffRemove: return theme::contrastInk(theme::Error);
    case Ink::Error: return theme::Error;
    }
    return theme::Text;
}

SubagentTranscriptView::SubagentTranscriptView(const QString &id, QWidget *parent) : QWidget(parent), m_id(id) {
    setObjectName(QStringLiteral("subagentTranscript"));
    setAttribute(Qt::WA_StyledBackground);
    // Opaque: the view also floats over a narrow pane's terminal. The frame is in the
    // application stylesheet (QWidget#subagentTranscript) so a theme switch restyles it.
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(10, 8, 8, 8); layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    m_header = header;
    m_title = new QLabel; m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont bold = m_title->font(); bold.setBold(true); m_title->setFont(bold);
    header->addWidget(m_title, 1);
    m_close = new QToolButton; m_close->setText(QStringLiteral("×")); m_close->setAutoRaise(true);
    m_close->setToolTip(QStringLiteral("Close the transcript (Esc from the message box). The agent keeps running."));
    m_close->setFocusPolicy(Qt::NoFocus);
    connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
    header->addWidget(m_close);
    layout->addLayout(header);
    m_status = new QLabel; m_status->setTextFormat(Qt::PlainText);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_status->setObjectName(QStringLiteral("panelKeys"));
    layout->addWidget(m_status);
    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("transcriptView"));
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::ClickFocus);
    m_log->setFont(theme::legible(QFontDatabase::systemFont(QFontDatabase::FixedFont), theme::BodyPt));   // QPlainTextEdit#transcriptView sets the face
    m_log->setMaximumBlockCount(6000);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    relay::installCopyOnSelect(m_log);
    // Clicking a tool line folds its detail open in place (protocol § 23, owner 2026-09-18).
    m_log->viewport()->installEventFilter(this);
    m_log->setToolTip(QStringLiteral("Click a ▸ tool line to fold its detail open, and again to fold it shut"));
    layout->addWidget(m_log, 1);
    m_input = new QLineEdit;
    m_input->setObjectName(QStringLiteral("subagentInput"));
    m_input->setPlaceholderText(QStringLiteral("Message %1 · Enter sends (resumes it if finished) · Esc back to the main agent").arg(id));
    m_input->installEventFilter(this);
    layout->addWidget(m_input);
    m_title->setText(title());
    m_status->setText(QStringLiteral("Loading transcript…"));
}

void SubagentTranscriptView::setHostedInPane(bool hosted) {
    m_close->setVisible(!hosted);
}

void SubagentTranscriptView::setHeaderRightInset(int pixels) {
    if (m_header->contentsMargins().right() != pixels) m_header->setContentsMargins(0, 0, pixels, 0);
}

QString SubagentTranscriptView::title() const {
    const QString type = m_type.isEmpty() ? QStringLiteral("agent") : m_type;
    return m_description.isEmpty() ? QStringLiteral("✦ %1 %2").arg(type, m_id) : QStringLiteral("✦ %1 %2 · %3").arg(type, m_id, m_description);
}

void SubagentTranscriptView::restore(const QString &type, const QString &description, const QString &status, const QString &text) {
    m_type = type; m_description = description;
    // A live agent did not survive the quit: it reads as stopped, not as still running.
    m_lastStatus = (status.isEmpty() || status == QStringLiteral("waiting") || status == QStringLiteral("running"))
                       ? QStringLiteral("stopped") : status;
    m_title->setText(title());
    m_log->clear(); m_atLineStart = true;
    m_prose.reset(); m_thinking = -1;
    m_calls.clear(); m_merge.clear(); m_mergeHead = -1;
    if (!text.isEmpty()) append(text, Ink::Agent);
    setEnded(true);
}

void SubagentTranscriptView::setEnded(bool ended) {
    m_ended = ended;
    m_input->setEnabled(!ended);
    m_input->setPlaceholderText(ended ? QStringLiteral("%1 ended with the previous session · Esc back to the main agent").arg(m_id)
                                      : QStringLiteral("Message %1 · Enter sends (resumes it if finished) · Esc back to the main agent").arg(m_id));
    if (ended) {
        appendNote(restoredMark());
        m_status->setText(QStringLiteral("%1 %2 · ended with the previous session").arg(SubagentModel::statusIcon(m_lastStatus), m_lastStatus));
    }
}

void SubagentTranscriptView::setRow(const SubagentRow &row, qint64 elapsedMs) {
    m_type = row.type; m_description = row.description; m_lastStatus = row.status;
    m_title->setText(title());
    m_title->setToolTip(QStringLiteral("%1 %2\nModel: %3%4").arg(row.type, row.id, row.model.isEmpty() ? QStringLiteral("inherit") : row.model,
                                                             row.effort.isEmpty() ? QString() : QStringLiteral(" · effort ") + row.effort));
    QStringList parts{SubagentModel::statusIcon(row.status) + QLatin1Char(' ') + row.status, SubagentModel::formatElapsed(elapsedMs),
                      QStringLiteral("%1 tool%2").arg(row.tools).arg(row.tools == 1 ? QString() : QStringLiteral("s")),
                      SubagentModel::formatTokens(row.tokens, row.tokensEstimated) + QStringLiteral(" tok")};
    if (row.background) parts << QStringLiteral("background");
    if (row.live() && !row.lastActivity.isEmpty()) parts << row.lastActivity;
    m_status->setText(parts.join(QStringLiteral(" · ")));
}

void SubagentTranscriptView::focusInput() { m_input->setFocus(Qt::OtherFocusReason); }

QString SubagentTranscriptView::plainText() const { return m_log->toPlainText(); }

bool SubagentTranscriptView::eventFilter(QObject *object, QEvent *event) {
    if (object == m_log->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        // Only a plain click on a tool line, and only when nothing is selected: dragging across
        // the log to copy must not fold rows open under the pointer.
        if (mouse->button() == Qt::LeftButton && !m_log->textCursor().hasSelection()) {
            const int at = callAt(m_log->cursorForPosition(mouse->pos()).blockNumber());
            if (at >= 0) { m_calls[at].userToggled = true; toggleRow(at); return true; }
        }
    }
    if (object == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) { if (onClose) onClose(); return true; }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            const QString text = m_input->text().trimmed();
            if (!text.isEmpty() && onSend) {
                onSend(text);
                ensureLineStart();
                append(QStringLiteral("› ") + text + QLatin1Char('\n'), Ink::User);
                m_input->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void SubagentTranscriptView::append(const QString &text, Ink ink) {
    finishProse();
    const QString clean = sanitize(text);
    if (clean.isEmpty()) return;
    // Same two-level scheme as the terminal's inks (main.cpp, owner 2026-09-18): user lines carry
    // a destination colour and machine lines are grey. Every user line here goes to an agent, so
    // "User" is the agent violet, not the shell cyan.
    const QColor color = inkColor(ink);
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 4;
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    // A diff's add/remove lines carry their green/red fill, as every surface that draws a diff
    // does: black-or-white ink on the fill, not green/red text on the ground.
    if (ink == Ink::DiffAdd || ink == Ink::DiffRemove)
        format.setBackground(ink == Ink::DiffAdd ? theme::Success : theme::Error);
    if (ink == Ink::User) format.setFontWeight(QFont::Bold);
    cursor.insertText(clean, format);
    m_atLineStart = clean.endsWith(QLatin1Char('\n'));
    if (follow) bar->setValue(bar->maximum());
}

void SubagentTranscriptView::finishProse() {
    m_prose.reset();
}

void SubagentTranscriptView::appendProse(const QString &text) {
    if (text.isEmpty()) return;
    m_merge.clear(); m_mergeHead = -1;
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 4;
    if (!m_prose) {
        m_prose = std::make_unique<calllines::MarkdownStream>(transcriptPalette(true));
        m_proseTail = QTextCursor(m_log->document());
        m_proseTail.movePosition(QTextCursor::End);
    }
    QTextCursor cursor(m_log->document());
    cursor.setPosition(m_proseTail.position());
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    insertMarkdown(cursor, m_prose->feed(sanitize(text)), true);
    const int tailStart = cursor.position();
    insertMarkdown(cursor, m_prose->tail(), false);
    m_proseTail = QTextCursor(m_log->document());
    m_proseTail.setPosition(tailStart);
    m_proseTail.setKeepPositionOnInsert(false); // a tool fold inserted before this tail moves it
    m_atLineStart = cursor.atBlockStart();
    if (follow) bar->setValue(bar->maximum());
}

void SubagentTranscriptView::refreshThinking() {
    m_thinkingRefreshPending = false;
    if (m_thinking < 0 || m_thinking >= m_calls.size() || !m_calls[m_thinking].expanded) return;
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 4;
    toggleRow(m_thinking);
    toggleRow(m_thinking);
    if (follow) bar->setValue(bar->maximum());
}

void SubagentTranscriptView::thinkingEvent(const QJsonObject &payload) {
    const bool done = payload.value(QStringLiteral("event")).toString() == QStringLiteral("thinking_done");
    if (m_thinking < 0) {
        if (!done && thinkingDisplay() == QStringLiteral("never")) return;
        ensureLineStart(); finishProse();
        m_merge.clear(); m_mergeHead = -1;
        ToolCall call;
        call.thinking = true;
        const int start = m_log->document()->characterCount() - 1;
        append(QStringLiteral(" \n"), Ink::Note);
        call.line = QTextCursor(m_log->document()); call.line.setPosition(start);
        call.after = QTextCursor(m_log->document());
        call.after.setPosition(m_log->document()->characterCount() - 1);
        call.after.setKeepPositionOnInsert(true);
        m_calls.append(call); forgetCalls(); m_thinking = m_calls.size() - 1;
    }
    ToolCall &call = m_calls[m_thinking];
    if (!done) {
        call.detail = (call.detail + sanitize(payload.value(QStringLiteral("text")).toString())).right(400000);
        drawRow(call);
        if (!call.expanded && !call.userToggled) toggleRow(m_thinking);
        if (!m_thinkingRefreshPending) {
            m_thinkingRefreshPending = true;
            QTimer::singleShot(250, this, [this] { refreshThinking(); });
        }
        return;
    }
    const bool keepOpen = call.userToggled ? call.expanded : thinkingDisplay() == QStringLiteral("always");
    if (call.expanded) toggleRow(m_thinking);
    call.done = true;
    const qint64 ms = payload.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
    call.label.title = QStringLiteral("✦ thought for %1 s").arg(QString::number(ms / 1000.0, 'f', 1));
    drawRow(call);
    if (keepOpen) toggleRow(m_thinking);
    m_thinking = -1;
    m_atLineStart = true;
}

void SubagentTranscriptView::appendNote(const QString &text) {
    ensureLineStart();
    append(text + QLatin1Char('\n'), Ink::Note);
}

// ----- one line per tool call (protocol § 23, card #TK9C) --------------------------------------

QString SubagentTranscriptView::rowText(const ToolCall &call) const {
    if (call.thinking)
        return (call.expanded ? QStringLiteral("▾ ") : QStringLiteral("▸ "))
            + (call.done ? call.label.title : QStringLiteral("✦ thinking…"));
    // The marker and the arrow are this surface's, not the backend's (§ 23): ✗ for a call that
    // failed, ▾ for a row whose detail is folded open, ▸ for one that can be.
    const QString marker = call.done && call.label.failed() ? QStringLiteral("✗")
                         : call.expanded                    ? QStringLiteral("▾")
                                                            : QStringLiteral("▸");
    const QString text = call.done ? call.label.line() : call.label.runningLine();
    return marker + QLatin1Char(' ') + (text.isEmpty() ? call.label.title : text);
}

void SubagentTranscriptView::rewriteLine(ToolCall &call, const QString &text, Ink ink) {
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 4;
    QTextCursor cursor(call.line);
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat format;
    format.setForeground(inkColor(ink == Ink::Error ? Ink::Error : Ink::Tool));
    const int start = cursor.selectionStart();
    cursor.insertText(sanitize(text), format);
    // insertText leaves the cursor after the text; the row's anchor belongs at its first character.
    call.line.setPosition(start);
    if (follow) bar->setValue(bar->maximum());
}

void SubagentTranscriptView::drawRow(ToolCall &call) {
    rewriteLine(call, rowText(call), call.done && call.label.failed() && !call.label.refused ? Ink::Error : Ink::Tool);
}

int SubagentTranscriptView::indexOfCall(const QString &callId) const {
    if (callId.isEmpty()) return -1;
    for (int at = m_calls.size() - 1; at >= 0; --at) if (m_calls.at(at).callId == callId) return at;
    return -1;
}

int SubagentTranscriptView::callAt(int blockNumber) const {
    for (int at = 0; at < m_calls.size(); ++at)
        if (m_calls.at(at).line.blockNumber() == blockNumber) return at;
    return -1;
}

// The log trims itself at kMaxBlocks, which invalidates the oldest rows' cursors; drop the rows
// that have scrolled out rather than rewrite a block that is no longer theirs.
void SubagentTranscriptView::forgetCalls() {
    constexpr int kKeepRows = 400;
    while (m_calls.size() > kKeepRows) {
        m_calls.removeFirst();
        if (m_mergeHead >= 0) --m_mergeHead;
        if (m_thinking >= 0) --m_thinking;
    }
    if (m_mergeHead < 0 || m_mergeHead >= m_calls.size()) { m_mergeHead = -1; m_merge.clear(); }
}

void SubagentTranscriptView::toolStarted(const QJsonObject &payload) {
    ToolCall call;
    call.callId = payload.value(QStringLiteral("call_id")).toString();
    call.label = toollabel::fromEvent(payload);
    // `preview` is legacy for display (§ 23.1), but it is still the best detail this surface has:
    // the subagent pane never asks for tool_output_get, so the fold shows what the call was.
    call.detail = payload.value(QStringLiteral("preview")).toString().left(8000);
    ensureLineStart();
    const int start = m_log->document()->characterCount() - 1;
    append(QStringLiteral(" \n"), Ink::Tool);           // a block to rewrite in place
    call.line = QTextCursor(m_log->document());
    call.line.setPosition(start);
    call.after = QTextCursor(m_log->document());
    call.after.setPosition(m_log->document()->characterCount() - 1);
    call.after.setKeepPositionOnInsert(true);           // later rows are appended past the fold
    m_calls.append(call);
    forgetCalls();
    drawRow(m_calls.last());
}

void SubagentTranscriptView::toolResult(const QJsonObject &payload) {
    const QString callId = payload.value(QStringLiteral("call_id")).toString();
    int at = indexOfCall(callId);
    if (at < 0 && callId.isEmpty())                     // an old worker names no call: the row still
        for (int i = m_calls.size() - 1; i >= 0; --i)   // running is the one that has landed
            if (!m_calls.at(i).done && !m_calls.at(i).thinking) { at = i; break; }
    if (at < 0) {                                       // no tool_started reached us: open a row now
        toolStarted(QJsonObject{{"call_id", callId}, {"tool", payload.value(QStringLiteral("tool"))}});
        at = m_calls.size() - 1;
        if (at < 0) return;
    }
    ToolCall &call = m_calls[at];
    const toollabel::Label landed = toollabel::fromEvent(payload);
    if (landed.fallback && call.label.valid && !call.label.title.isEmpty()) {
        // A worker from before § 23 sends no label at all, and its `tool_result` carries no preview
        // either: keep the title the started event gave us and take only the verdict from this one.
        call.label.stats = landed.stats;
        call.label.hasOk = landed.hasOk;
        call.label.ok = landed.ok;
        call.label.error = landed.error;
    } else {
        call.label = landed;
    }
    call.done = true;
    call.diff = payload.value(QStringLiteral("diff")).toString();
    const QJsonObject result = payload.value(QStringLiteral("result")).toObject();
    if (result.contains(QStringLiteral("error"))) {
        if (!call.detail.isEmpty()) call.detail += QLatin1Char('\n');
        call.detail += result.value(QStringLiteral("error")).toString().left(4000);
    }
    if (!call.diff.isEmpty()) {
        if (!call.detail.isEmpty()) call.detail += QLatin1Char('\n');
        call.detail += call.diff.left(8000);
    }

    // A run of consecutive reads or listings is one line (§ 23.7). Only the row that has just
    // landed at the bottom may be swallowed into the run above it: anything printed in between
    // (an answer, another agent's line) ends the run.
    const bool last = at == m_calls.size() - 1 && !call.expanded;
    if (last && m_mergeHead >= 0 && m_mergeHead == at - 1 && m_merge.accepts(call.label)
        && !m_calls.at(m_mergeHead).expanded) {
        m_merge.add(call.label);
        QTextCursor cursor(call.line);
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.deletePreviousChar();                    // the newline the row was printed on
        ToolCall &head = m_calls[m_mergeHead];
        head.label.title = m_merge.line();
        head.label.stats.clear();
        // The fold of a merged run shows every member's detail, in order.
        if (!call.detail.isEmpty()) head.detail += QStringLiteral("\n") + call.detail;
        m_calls.removeAt(at);
        drawRow(head);
        m_atLineStart = true;
        return;
    }

    drawRow(call);
    if (call.label.hasMerge && !call.label.failed()) { m_merge.clear(); m_merge.add(call.label); m_mergeHead = at; }
    else { m_merge.clear(); m_mergeHead = -1; }

    // A short diff stays behind the row's click like every other detail (#WXT6): it is already in
    // call.detail, so toggleToolCall() draws it — nothing auto-expands here any more.
}

void SubagentTranscriptView::toolOutput(const QString &text) {
    // Output belongs behind the line, not in front of it: it goes into the fold of the call that
    // is running, and only reaches the log when there is no row to put it on.
    if (m_calls.isEmpty() || m_calls.last().done || m_calls.last().thinking) { append(text, Ink::ToolOutput); return; }
    ToolCall &call = m_calls.last();
    call.detail = (call.detail + text).right(16000);
    if (call.expanded) { toggleRow(m_calls.size() - 1); toggleRow(m_calls.size() - 1); }
}

void SubagentTranscriptView::toggleToolCall(int index) {
    for (int i = 0; i < m_calls.size(); ++i)
        if (!m_calls.at(i).thinking && index-- == 0) { toggleRow(i); return; }
}

int SubagentTranscriptView::toolCallCount() const {
    int count = 0;
    for (const auto &call : m_calls) if (!call.thinking) ++count;
    return count;
}

void SubagentTranscriptView::toggleRow(int index) {
    if (index < 0 || index >= m_calls.size()) return;
    ToolCall &call = m_calls[index];
    if (call.expanded) {
        QTextCursor cursor(m_log->document());
        cursor.setPosition(call.after.position());
        cursor.setPosition(qMin(call.after.position() + call.foldChars, m_log->document()->characterCount() - 1),
                           QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        call.foldChars = 0;
        call.expanded = false;
        drawRow(call);
        return;
    }
    // A diff too big to print inline belongs in the diff pane, not in this narrow log (§ 23.6).
    if (call.label.openType == QStringLiteral("diff") && !call.diff.isEmpty() && onOpenDiff) {
        onOpenDiff(call.label.path.isEmpty() ? call.label.title : call.label.path, call.diff);
        return;
    }
    QString detail = call.detail.trimmed();
    if (detail.isEmpty()) detail = call.label.line();
    QTextCursor cursor(m_log->document());
    cursor.setPosition(call.after.position());
    const int before = m_log->document()->characterCount();
    QTextCharFormat format;
    format.setForeground(inkColor(Ink::ToolOutput));
    if (call.thinking) {
        calllines::FoldOptions options;
        options.maxLines = call.done ? calllines::kFoldLineCap : calllines::kThinkingStreamRows;
        const int cells = qMax(10, m_log->viewport()->width() / qMax(1, m_log->fontMetrics().horizontalAdvance(QLatin1Char('M'))) - 4);
        auto lines = calllines::foldForMarkdown(call.done ? detail : detail.right(12000), transcriptPalette(), options, cells, !call.done);
        for (auto &line : lines) {
            for (auto &span : line.spans) span.text.remove(QStringLiteral(" · open in pane"));
            FoldSpan indent; indent.text = QStringLiteral("    "); line.spans.prepend(indent);
        }
        if (lines.isEmpty()) lines = calllines::foldForNote(QStringLiteral("    (No reasoning was kept for this block.)"), transcriptPalette());
        insertMarkdown(cursor, lines, true);
    } else {
        for (const QString &line : detail.split(QLatin1Char('\n'))) {
            QTextCharFormat lineFormat = format;
            if (line.startsWith(QLatin1Char('+')) && !line.startsWith(QStringLiteral("+++"))) {
                lineFormat.setForeground(inkColor(Ink::DiffAdd));
                lineFormat.setBackground(theme::Success);
            } else if (line.startsWith(QLatin1Char('-')) && !line.startsWith(QStringLiteral("---"))) {
                lineFormat.setForeground(inkColor(Ink::DiffRemove));
                lineFormat.setBackground(theme::Error);
            }
            cursor.insertText(sanitize(QStringLiteral("    ") + line + QLatin1Char('\n')), lineFormat);
        }
    }
    call.foldChars = m_log->document()->characterCount() - before;
    call.expanded = true;
    drawRow(call);
}

QStringList SubagentTranscriptView::toolLines() const {
    QStringList out;
    for (const ToolCall &call : m_calls) if (!call.thinking) out << rowText(call);
    return out;
}

void SubagentTranscriptView::handleEvent(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("subagent_transcript")) {
        m_log->clear(); m_atLineStart = true; m_snapshot = true;
        m_prose.reset(); m_thinking = -1;
        m_calls.clear(); m_merge.clear(); m_mergeHead = -1;
        for (const auto &value : event.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject message = value.toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            const QString content = message.value(QStringLiteral("content")).toString();
            ensureLineStart();
            if (role == QStringLiteral("user")) {
                append(QStringLiteral("› ") + content.trimmed() + QLatin1Char('\n'), Ink::User);
            } else if (role == QStringLiteral("assistant")) {
                if (!content.trimmed().isEmpty()) { appendProse(content); ensureLineStart(); }
                QStringList calls;
                for (const auto &call : message.value(QStringLiteral("tool_calls")).toArray()) calls << call.toString();
                if (!calls.isEmpty()) append(QStringLiteral("⚙ ") + calls.join(QStringLiteral(", ")) + QLatin1Char('\n'), Ink::Tool);
            } else if (role == QStringLiteral("tool")) {
                QStringList lines = content.split(QLatin1Char('\n'));
                const int total = lines.size();
                if (total > 12) { lines = lines.mid(0, 12); lines << QStringLiteral("… %1 more lines").arg(total - 12); }
                append(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'), Ink::ToolOutput);
            }
        }
        appendNote(QStringLiteral("── live ──"));
        return;
    }
    if (type == QStringLiteral("subagent_finished")) {
        if (m_thinking >= 0) thinkingEvent(QJsonObject{{"event", "thinking_done"}});
        finishProse();
        const QString outcome = event.value(QStringLiteral("outcome")).toString();
        QString note = SubagentModel::statusIcon(outcome) + QLatin1Char(' ') + outcome;
        const QString handoff = SubagentModel::handoffText(event.value(QStringLiteral("handoff")).toString(),
                                                           event.value(QStringLiteral("wakeups")).toInt(),
                                                           event.value(QStringLiteral("max_auto_turns")).toInt());
        if (!handoff.isEmpty()) note += QStringLiteral(" · ") + handoff;
        appendNote(note);
        return;
    }
    if (type != QStringLiteral("subagent_event")) return;
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    const QString kind = payload.value(QStringLiteral("event")).toString();
    if (kind == QStringLiteral("thinking_delta") || kind == QStringLiteral("thinking_done")) thinkingEvent(payload);
    else if (kind == QStringLiteral("delta")) appendProse(payload.value(QStringLiteral("text")).toString());
    else if (kind == QStringLiteral("tool_started")) toolStarted(payload);
    else if (kind == QStringLiteral("tool_output")) toolOutput(payload.value(QStringLiteral("text")).toString());
    else if (kind == QStringLiteral("tool_result")) toolResult(payload);
    // "status" payloads (step counters) update the row via subagent_progress; they are not logged.
}

// ----- tabbed subagent pane (card #WD83) ------------------------------------------------------

SubagentTabsView::SubagentTabsView(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("subagentTabs"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto *head = new QWidget;
    head->setObjectName(QStringLiteral("subagentTabsHeader"));
    m_header = new QHBoxLayout(head); m_header->setContentsMargins(6, 4, 0, 0); m_header->setSpacing(6);
    // Like the Switchboard's "← Back to board": always on screen, the way back to the main thread.
    m_back = new QToolButton;
    m_back->setObjectName(QStringLiteral("subagentBack"));
    m_back->setText(QStringLiteral("←  main agent"));
    m_back->setAutoRaise(true);
    m_back->setCursor(Qt::PointingHandCursor);
    m_back->setFocusPolicy(Qt::NoFocus);
    setBackKeys(QString());
    connect(m_back, &QToolButton::clicked, this, [this] {
        if (onBackClicked) onBackClicked();
        if (onBackToMain) onBackToMain();
    });
    m_header->addWidget(m_back);
    m_bar = new QTabBar(this);
    relay::paneTabs::registerTabs(this, m_bar);
    m_bar->setObjectName(QStringLiteral("subagentTabBar"));
    m_bar->setDocumentMode(true);
    m_bar->setTabsClosable(true);
    m_bar->setMovable(true);
    m_bar->setExpanding(false);
    m_bar->setUsesScrollButtons(true);
    m_bar->setElideMode(Qt::ElideRight);
    m_bar->setDrawBase(false);
    m_bar->setFocusPolicy(Qt::NoFocus);
    m_header->addWidget(m_bar, 1);
    layout->addWidget(head);
    m_stack = new QStackedWidget;
    layout->addWidget(m_stack, 1);
    connect(m_bar, &QTabBar::currentChanged, this, [this](int index) {
        const QString id = index >= 0 ? m_bar->tabData(index).toString() : QString();
        auto *view = m_views.value(id).data();
        if (!view) return;
        const bool hadFocus = isAncestorOf(QApplication::focusWidget());
        m_stack->setCurrentWidget(view);
        if (hadFocus) view->focusInput();
        if (onTitleChanged) onTitleChanged();
    });
    connect(m_bar, &QTabBar::tabCloseRequested, this, [this](int index) { closeTabByUser(m_bar->tabData(index).toString()); });
}

void SubagentTabsView::setBackKeys(const QString &keys) {
    m_back->setToolTip(keys.isEmpty() ? QStringLiteral("Back to the main agent's prompt box (Esc)")
                                      : QStringLiteral("Back to the main agent's prompt box (Esc); %1 closes this pane").arg(keys));
}

int SubagentTabsView::indexOf(const QString &id) const {
    for (int i = 0; i < m_bar->count(); ++i) if (m_bar->tabData(i).toString() == id) return i;
    return -1;
}

int SubagentTabsView::count() const { return m_bar->count(); }

// A plain × like the window's tabs, not the style's red close icon; it closes the tab, not the agent.
int SubagentTabsView::addTabFor(const QString &id, int at) {
    const int index = at >= 0 ? m_bar->insertTab(at, QString()) : m_bar->addTab(QString());
    m_bar->setTabData(index, id);
    auto *close = new QToolButton;
    close->setObjectName(QStringLiteral("subagentTabClose"));
    close->setText(QStringLiteral("×"));
    close->setAutoRaise(true);
    close->setFocusPolicy(Qt::NoFocus);
    close->setCursor(Qt::PointingHandCursor);
    close->setToolTip(QStringLiteral("Close this tab. A running agent keeps running and its row opens it again; a finished one is dismissed from the list."));
    connect(close, &QToolButton::clicked, this, [this, id] { closeTabByUser(id); });
    m_bar->setTabButton(index, QTabBar::RightSide, close);
    return index;
}

QStringList SubagentTabsView::ids() const {
    QStringList out;
    for (int i = 0; i < m_bar->count(); ++i) out << m_bar->tabData(i).toString();
    return out;
}

SubagentTranscriptView *SubagentTabsView::tab(const QString &id) const { return m_views.value(id).data(); }

QString SubagentTabsView::currentId() const {
    const int index = m_bar->currentIndex();
    return index >= 0 ? m_bar->tabData(index).toString() : QString();
}

SubagentTranscriptView *SubagentTabsView::current() const { return tab(currentId()); }

void SubagentTabsView::relabel(int index) {
    const QString id = m_bar->tabData(index).toString();
    const SubagentTranscriptView *view = tab(id);
    if (!view) return;
    const QString status = view->statusText();
    const QString type = view->type().isEmpty() ? QStringLiteral("agent") : view->type();
    m_bar->setTabText(index, QStringLiteral("%1 %2 %3").arg(SubagentModel::statusIcon(status), type, id));
    m_bar->setTabToolTip(index, (view->description().isEmpty() ? QString() : view->description() + QLatin1Char('\n'))
                                    + (view->ended() ? QStringLiteral("Ended with the previous session") : status));
    // The same ink the pane-status glyphs use (relay::panestatus::stateInk): a running agent is
    // the agent colour, a failure the error colour, so a tab and a header never disagree.
    QColor color = theme::TextMuted;
    if (view->ended()) color = theme::TextMuted;
    else if (status == QStringLiteral("running")) color = theme::Agent;
    else if (status == QStringLiteral("done")) color = theme::Success;
    else if (status == QStringLiteral("failed") || status == QStringLiteral("blocked")) color = theme::Error;
    else if (status == QStringLiteral("waiting")) color = theme::Text;
    m_bar->setTabTextColor(index, color);
}

SubagentTranscriptView *SubagentTabsView::showTab(const QString &id, bool live) {
    m_closed.remove(id);
    return ensureTab(id, live, true);
}

SubagentTranscriptView *SubagentTabsView::ensureTab(const QString &id, bool live, bool select) {
    if (id.isEmpty()) return nullptr;
    // Subagent ids start again at a1 in a new worker: a restored tab with the same id is a
    // different, finished agent, so the live one replaces it.
    int at = -1;
    if (auto *old = tab(id); old && old->ended() && live) {
        at = indexOf(id);
        m_bar->blockSignals(true); m_bar->removeTab(at); m_bar->blockSignals(false);
        m_stack->removeWidget(old); m_views.remove(id); old->deleteLater();
    }
    if (auto *existing = tab(id)) {
        if (select) {
            m_bar->setCurrentIndex(indexOf(id));
            m_stack->setCurrentWidget(existing);
        }
        return existing;
    }
    auto *view = new SubagentTranscriptView(id);
    view->setHostedInPane(true);   // the tab's × and the pane's × close it
    view->onClose = [this] { if (onBackToMain) onBackToMain(); };
    m_views.insert(id, view);
    m_stack->addWidget(view);
    const int index = addTabFor(id, at);
    if (onViewCreated) onViewCreated(view);
    relabel(index);
    if (select || m_bar->count() == 1) {
        m_bar->setCurrentIndex(index);
        m_stack->setCurrentWidget(view);
    }
    if (onTitleChanged) onTitleChanged();
    return view;
}

void SubagentTabsView::closeTab(const QString &id) {
    const int index = indexOf(id);
    if (index < 0) return;
    const bool hadFocus = isAncestorOf(QApplication::focusWidget());
    m_bar->removeTab(index);
    if (auto *view = m_views.take(id).data()) { m_stack->removeWidget(view); view->deleteLater(); }
    m_seen.remove(id);
    if (m_bar->count() == 0) { if (onEmpty) onEmpty(); return; }
    if (auto *view = current()) { m_stack->setCurrentWidget(view); if (hadFocus) view->focusInput(); }
    if (onTitleChanged) onTitleChanged();
}

// The tab's × (not the list's rules): the owner dismisses a finished agent's row, which agrees
// with dismissing a row closing its tab. A running agent's row stays.
void SubagentTabsView::closeTabByUser(const QString &id) {
    m_closed.insert(id);
    if (onUserClosed) onUserClosed(id);
    closeTab(id);   // already gone if dismissing the row closed it
}

void SubagentTabsView::syncRows(const SubagentModel &model) {
    // The strip folds away while this pane is open, so every listed agent needs a tab here.
    // Background arrivals subscribe once without stealing the selected tab or its draft/focus.
    for (auto it = m_closed.begin(); it != m_closed.end();) {
        if (!model.row(*it)) it = m_closed.erase(it);
        else ++it;
    }
    for (const auto &row : model.rows())
        if (!tab(row.id) && !m_closed.contains(row.id)) ensureTab(row.id, true, false);
    QStringList gone;
    for (const QString &id : ids()) {
        SubagentTranscriptView *view = tab(id);
        if (!view || view->ended()) continue;
        if (const SubagentRow *row = model.row(id)) {
            m_seen.insert(id, true);
            view->setRow(*row, model.elapsedNow(*row));
            relabel(indexOf(id));
        } else if (m_seen.value(id)) {
            gone << id;
        }
    }
    for (const QString &id : std::as_const(gone)) closeTab(id);
}

void SubagentTabsView::dropEnded() {
    QStringList ended;
    for (const QString &id : ids()) if (auto *view = tab(id); view && view->ended()) ended << id;
    for (const QString &id : std::as_const(ended)) closeTab(id);
}

QString SubagentTabsView::title() const {
    const auto *view = current();
    return view ? view->title() : QStringLiteral("✦ Subagents");
}

void SubagentTabsView::focusInput() { if (auto *view = current()) view->focusInput(); }

void SubagentTabsView::setHeaderRightInset(int pixels) {
    const QMargins m = m_header->contentsMargins();
    if (m.right() != pixels) m_header->setContentsMargins(m.left(), m.top(), pixels, m.bottom());
}

void SubagentTabsView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) { if (onBackToMain) onBackToMain(); return; }
    QWidget::keyPressEvent(event);
}

QJsonObject SubagentTabsView::node() const {
    QJsonArray tabs;
    for (const QString &id : ids()) {
        const auto *view = tab(id);
        if (!view) continue;
        tabs.append(QJsonObject{{"id", id}, {"type", view->type()}, {"description", view->description()},
                                {"status", view->statusText()},
                                // A tab restored twice does not stack the restart note.
                                {"text", QString(view->plainText()).remove(SubagentTranscriptView::restoredMark() + QLatin1Char('\n'))
                                             .right(kSavedChars)}});
    }
    if (tabs.isEmpty()) return {};
    return {{"subagents", QJsonObject{{"owner", m_ownerKey}, {"cwd", m_cwd}, {"current", currentId()}, {"tabs", tabs}}}};
}

void SubagentTabsView::restore(const QJsonObject &subagents) {
    m_ownerKey = subagents.value(QStringLiteral("owner")).toString();
    m_cwd = subagents.value(QStringLiteral("cwd")).toString();
    for (const auto &value : subagents.value(QStringLiteral("tabs")).toArray()) {
        const QJsonObject saved = value.toObject();
        const QString id = saved.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || tab(id)) continue;
        auto *view = new SubagentTranscriptView(id);
        view->setHostedInPane(true);
        view->onClose = [this] { if (onBackToMain) onBackToMain(); };
        view->restore(saved.value(QStringLiteral("type")).toString(), saved.value(QStringLiteral("description")).toString(),
                      saved.value(QStringLiteral("status")).toString(), saved.value(QStringLiteral("text")).toString());
        m_views.insert(id, view);
        m_stack->addWidget(view);
        relabel(addTabFor(id));
    }
    const int index = indexOf(subagents.value(QStringLiteral("current")).toString());
    if (index >= 0) m_bar->setCurrentIndex(index);
    if (auto *view = current()) m_stack->setCurrentWidget(view);
}

}  // namespace relay
