// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AgentInternalsView.h"

#include "CopyOnSelect.h"
#include "Theme.h"

#include <QColor>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShowEvent>
#include <QTextCharFormat>
#include <QVBoxLayout>

#include <algorithm>

namespace relay {

namespace {
// Model and tool text is untrusted: drop control characters except newline and tab, exactly as
// every other transcript surface does.
QString sanitize(const QString &text) {
    QString clean;
    clean.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) clean += c;
    }
    return clean;
}

// The reasoning block is written out whole (this pane is where it lives), but a runaway stream
// must not turn one block into a document nothing can scroll.
constexpr int kThinkingChars = 400000;
constexpr int kMaxBlocks = 12000;
constexpr int kKeepRows = 600;
}  // namespace

// Machine lines are muted and upright (docs/ARCHITECTURE.md, "Legible text"); the two levels are
// the terminal's own — a failure takes the error ink, everything else the same grey.
QColor AgentInternalsView::inkColor(Ink ink) {
    switch (ink) {
    case Ink::Text: return theme::Text;
    case Ink::Tool: return theme::TextMuted;
    case Ink::Muted: return theme::TextMuted;
    case Ink::Error: return theme::Error;
    case Ink::User: return theme::Agent;
    }
    return theme::Text;
}

// The fold rows' colours, from the live theme — the same table Pane::foldPalette() fills, so a
// section unfolded here and the same section unfolded in the terminal are the same picture.
relay::calllines::Palette AgentInternalsView::palette() {
    namespace t = relay::theme;
    relay::calllines::Palette out;
    out.text = t::Text;
    out.muted = t::TextMuted;
    out.code = t::SyntaxCommand;
    out.link = t::Link;
    out.addBg = t::Success;
    out.removeBg = t::Error;
    out.add = t::contrastInk(out.addBg);
    out.remove = t::contrastInk(out.removeBg);
    out.error = t::SyntaxUnknown;
    out.accent = t::Accent;
    return out;
}

AgentInternalsView::AgentInternalsView(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("agentInternals"));
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 8, 8);
    layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    m_title = new QLabel(QStringLiteral("✦ Activity"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    header->addWidget(m_title, 1);
    layout->addLayout(header);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setObjectName(QStringLiteral("panelKeys"));
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_status);
    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("transcriptView"));
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::ClickFocus);
    m_log->setFont(theme::legible(QFontDatabase::systemFont(QFontDatabase::FixedFont), theme::BodyPt));
    m_log->setMaximumBlockCount(kMaxBlocks);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_log->setToolTip(QStringLiteral("Click a ▸ tool line to fold its detail open, and again to fold it shut · "
                                     "End follows the bottom again"));
    relay::installCopyOnSelect(m_log);
    m_log->viewport()->installEventFilter(this);
    m_log->installEventFilter(this);
    layout->addWidget(m_log, 1);
    // The Ask row (#FEJQ), under the log. Its "Turn n" chip follows the text cursor, which a click
    // on a tool row moves, so the turn it offers to ask about is the one the reader is reading.
    m_ask = new relay::askrow::AskRow(QStringLiteral("Ask the agent about this activity"),
                                      QStringLiteral("drafts a question in the terminal's prompt box · nothing is sent"));
    m_ask->onAsk = [this](const QString &text) { if (onAskOwner) onAskOwner(text); };
    m_ask->hide();   // until the window has wired onAskOwner; updateAskRow() decides from then on
    layout->addWidget(m_ask);
    connect(m_log, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateAskRow(); });
    m_clock.start();
    setHeader();
}

QString AgentInternalsView::paneTitle() const { return QStringLiteral("Activity"); }

void AgentInternalsView::focusView() { m_log->setFocus(Qt::OtherFocusReason); }

// The pane chrome's buttons cover the right of the first row: the title yields that much.
void AgentInternalsView::setHeaderRightInset(int pixels) {
    if (m_title->contentsMargins().right() != pixels) m_title->setContentsMargins(0, 0, pixels, 0);
}

// The one line under the title: what this pane is doing to the terminal, which is the thing a
// reader has to know before they close it.
void AgentInternalsView::setHeader() {
    QStringList parts;
    int calls = 0;   // calls, not rows: a merged run is one row for several
    for (const ToolCall &call : m_calls) calls += std::max(1, int(call.callIds.size()));
    parts << QStringLiteral("%1 turn%2").arg(m_turns).arg(m_turns == 1 ? QString() : QStringLiteral("s"));
    parts << QStringLiteral("%1 tool call%2").arg(calls).arg(calls == 1 ? QString() : QStringLiteral("s"));
    parts << QStringLiteral("the terminal prints these again when you close this pane");
    m_status->setText(parts.join(QStringLiteral(" · ")));
    updateAskRow();
}

// ----- the Ask row (#FEJQ) ----------------------------------------------------------------------
//
// This pane has no agent of its own: it draws one pane's agent working, and that agent is the one
// that can say why a turn was slow. So a chip drafts the question into that pane's composer and
// the person sends it — `onAskOwner` is the window's wire to Pane::insertInComposer, and until it
// is set there is nowhere to draft to and the row is not drawn at all.
//
// The chips carry the figures the log is showing, because the question and the screen have to
// agree, and the questions themselves come from src/AskRow.h, which the ⓘ pane's row shares.

int AgentInternalsView::turnAtCursor() const {
    if (m_turnMarks.isEmpty()) return -1;
    const int block = m_log->textCursor().blockNumber();
    int found = m_turnMarks.size() - 1;   // cursor untouched, view pinned: the newest turn
    for (int at = 0; at < m_turnMarks.size(); ++at)
        if (!m_turnMarks.at(at).at.isNull() && m_turnMarks.at(at).at.blockNumber() <= block) found = at;
    return found;
}

void AgentInternalsView::updateAskRow() {
    if (!m_ask) return;
    m_ask->setVisible(bool(onAskOwner));
    if (!onAskOwner) return;

    QVector<relay::askrow::Question> questions;
    if (!m_turnMarks.isEmpty()) {
        const TurnMark &last = m_turnMarks.last();
        const qint64 took = std::max<qint64>(0, last.lastMs - last.startMs);
        questions.append({QStringLiteral("Last turn · %1").arg(relay::askrow::howLong(took)),
                          relay::askrow::lastTurnQuestion(took)});
    }
    questions.append({QStringLiteral("Slowest tool calls"), relay::askrow::slowestToolsQuestion()});
    // A third chip only when the reader has gone back up the log to another turn: on the newest
    // turn it would ask the same thing as "Last turn", and two chips that draft the same question
    // are one chip and a mistake.
    const int selected = turnAtCursor();
    if (selected >= 0 && selected < m_turnMarks.size() - 1) {
        const TurnMark &mark = m_turnMarks.at(selected);
        questions.append({QStringLiteral("Turn %1").arg(mark.number),
                          relay::askrow::turnQuestion(mark.number, mark.request)});
    }
    m_ask->setQuestions(questions);
    m_ask->setAvailable(!m_turnMarks.isEmpty(),
                        QStringLiteral("There is nothing to ask about yet: this pane's agent has "
                                       "not run a turn."));
}

void AgentInternalsView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    updateAskRow();   // the window wires onAskOwner after the view is built
}

bool AgentInternalsView::pinned() const {
    QScrollBar *bar = m_log->verticalScrollBar();
    return bar->value() >= bar->maximum() - 4;
}

void AgentInternalsView::pin() {
    QScrollBar *bar = m_log->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void AgentInternalsView::append(const QString &text, Ink ink, bool bold) {
    const QString clean = sanitize(text);
    if (clean.isEmpty()) return;
    const bool follow = pinned();
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(inkColor(ink));
    if (bold) format.setFontWeight(QFont::Bold);
    cursor.insertText(clean, format);
    m_atLineStart = clean.endsWith(QLatin1Char('\n'));
    if (follow) pin();
}

// A fold's rows, in the colours relay::calllines gave them: this is the same content the terminal
// unfolds under the same row, so nothing about it is decided twice.
void AgentInternalsView::appendFoldLines(const QVector<FoldLine> &lines, int indent) {
    const bool follow = pinned();
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    const QString pad(indent, QLatin1Char(' '));
    for (const FoldLine &line : lines) {
        cursor.insertText(pad);
        for (const FoldSpan &span : line.spans) {
            QTextCharFormat format;
            format.setForeground(span.fg.isValid() ? span.fg : inkColor(Ink::Text));
            if (span.bg.isValid()) format.setBackground(span.bg);
            if (span.bold) format.setFontWeight(QFont::Bold);
            format.setFontItalic(span.italic);
            format.setFontUnderline(span.underline);
            cursor.insertText(sanitize(span.text), format);
        }
        cursor.insertText(QStringLiteral("\n"));
    }
    m_atLineStart = true;
    if (follow) pin();
}

// ----- turns ----------------------------------------------------------------------------------

void AgentInternalsView::beginTurn(const QString &turnId, const QString &request) {
    if (turnId.isEmpty() || turnId == m_turnId) return;
    m_turnId = turnId;
    ++m_turns;
    endThinking();
    m_merge.clear();
    m_mergeHead = -1;
    ensureLineStart();
    const QString line = request.trimmed().section(QLatin1Char('\n'), 0, 0).left(200);
    const int start = m_log->document()->characterCount() - 1;
    append(QStringLiteral("\n── ") + (line.isEmpty() ? QStringLiteral("agent turn") : line)
               + QStringLiteral(" ──\n"),
           Ink::Muted);
    // What the Ask row needs to name this turn and time it. The cursor rides the rule's own block,
    // so the log trimming itself at kMaxBlocks does not leave the mark pointing at somebody else.
    TurnMark mark;
    mark.number = m_turns;
    mark.turnId = turnId;
    mark.request = line;
    mark.at = QTextCursor(m_log->document());
    mark.at.setPosition(std::min(start, m_log->document()->characterCount() - 1));
    mark.startMs = mark.lastMs = m_clock.elapsed();
    m_turnMarks.append(mark);
    while (m_turnMarks.size() > 200) m_turnMarks.removeFirst();
    setHeader();
}

// The turn that is running has done something: its clock reading moves to now, so "the last turn
// took 42 s" is the time up to its last event rather than the time since its rule was drawn (a
// turn that ended ten minutes ago must not still be counting).
void AgentInternalsView::touchTurn() {
    if (!m_turnMarks.isEmpty()) m_turnMarks.last().lastMs = m_clock.elapsed();
}

// ----- reasoning ------------------------------------------------------------------------------
//
// One block, rewritten in place while it streams: the header row says whether it is still coming,
// and the text under it is the whole buffer as Markdown. A tool row or a new turn ends the block,
// so the next delta starts one of its own — the same rule the terminal's anchor follows.
void AgentInternalsView::setThinking(const QString &turnId, const QString &blockKey, const QString &text,
                                     bool done, qint64 elapsedMs) {
    touchTurn();
    const QString key = blockKey.isEmpty() ? turnId : blockKey;
    const QString body = text.right(kThinkingChars);
    const QString head = !done      ? QStringLiteral("✦ thinking…")
                       : elapsedMs > 0 ? QStringLiteral("✦ thought for %1 s").arg(std::max<qint64>(1, (elapsedMs + 500) / 1000))
                       : elapsedMs < 0 ? QStringLiteral("✦ reasoning so far in this turn")
                                       : QStringLiteral("✦ thinking stopped");
    relay::calllines::FoldOptions options;
    options.maxLines = 100000;   // uncapped on purpose: this pane is where the whole block lives
    const QVector<FoldLine> rendered = relay::calllines::foldForMarkdown(body, palette(), options);

    const bool follow = pinned();
    if (m_thinkingKey == key && !m_thinkingAt.isNull()) {
        // Replace the block where it stands. Nothing has printed under it — a tool row or a turn
        // ends the block — so the selection can run to the end of the document.
        QTextCursor cursor(m_log->document());
        cursor.setPosition(m_thinkingAt.position());
        cursor.setPosition(std::min(m_thinkingAt.position() + m_thinkingChars,
                                    m_log->document()->characterCount() - 1),
                           QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        ensureLineStart();
        m_thinkingKey = key;
        m_thinkingAt = QTextCursor(m_log->document());
        m_thinkingAt.setPosition(m_log->document()->characterCount() - 1);
        m_thinkingAt.setKeepPositionOnInsert(true);   // the block is written at this position: stay before it
        m_blocks.append({turnId, m_thinkingAt.position()});
        while (m_blocks.size() > 200) m_blocks.removeFirst();
    }
    const int before = m_log->document()->characterCount();
    append(head + QLatin1Char('\n'), Ink::Muted, true);
    if (rendered.isEmpty()) append(QStringLiteral("  (nothing yet)\n"), Ink::Muted);
    else appendFoldLines(rendered, 2);
    m_thinkingChars = m_log->document()->characterCount() - before;
    if (done) endThinking();
    if (follow) pin();
    updateAskRow();   // "Last turn · 42 s" while the turn is still thinking
}

bool AgentInternalsView::hasThinking(const QString &turnId) const {
    for (const auto &block : m_blocks)
        if (block.first == turnId) return true;
    return false;
}

void AgentInternalsView::showThinking(const QString &turnId) {
    int position = -1;
    for (const auto &block : m_blocks)
        if (block.first == turnId) position = block.second;
    if (position < 0) { focusView(); return; }
    QTextCursor cursor(m_log->document());
    cursor.setPosition(std::min(position, m_log->document()->characterCount() - 1));
    m_log->setTextCursor(cursor);
    m_log->ensureCursorVisible();
    focusView();
}

void AgentInternalsView::note(const QString &text) {
    if (text.isEmpty() || text == m_lastNote) return;
    m_lastNote = text;
    endThinking();
    ensureLineStart();
    append(text + QLatin1Char('\n'), Ink::Muted);
}

void AgentInternalsView::refreshTheme() {
    m_log->setFont(theme::legible(QFontDatabase::systemFont(QFontDatabase::FixedFont), theme::BodyPt));
}

// ----- one row per tool call (§ 23, card #TK9C) ------------------------------------------------

QString AgentInternalsView::rowText(const ToolCall &call) const {
    // The same marker the terminal draws: ▸ shut, ▾ open. A failure is already in the row's own
    // text (" ✗" after the title, finishedRow) and in its ink.
    const QString marker = call.expanded ? QStringLiteral("▾") : QStringLiteral("▸");
    relay::calllines::Row row = call.merged
        ? relay::calllines::mergedRow(call.run, 0)
        : call.done ? relay::calllines::finishedRow(call.label, 0)
                    : relay::calllines::runningRow(call.label, 0, call.liveLines);
    QString text = row.text();
    if (text.isEmpty()) text = call.label.title;
    return marker + QLatin1Char(' ') + text;
}

void AgentInternalsView::rewriteLine(ToolCall &call, const QString &text, Ink ink) {
    const bool follow = pinned();
    QTextCursor cursor(call.line);
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat format;
    format.setForeground(inkColor(ink));
    const int start = cursor.selectionStart();
    cursor.insertText(sanitize(text), format);
    call.line.setPosition(start);
    if (follow) pin();
}

void AgentInternalsView::drawRow(ToolCall &call) {
    rewriteLine(call, rowText(call), call.done && call.label.failed() ? Ink::Error : Ink::Tool);
}

int AgentInternalsView::indexOfCall(const QString &callId, const QString &turnId) const {
    if (callId.isEmpty()) return -1;
    for (int at = m_calls.size() - 1; at >= 0; --at) {
        const ToolCall &call = m_calls.at(at);
        if (!turnId.isEmpty() && !call.turnId.isEmpty() && call.turnId != turnId) continue;
        if (call.callId == callId || call.callIds.contains(callId)) return at;
    }
    return -1;
}

int AgentInternalsView::callAt(int blockNumber) const {
    for (int at = 0; at < m_calls.size(); ++at)
        if (m_calls.at(at).line.blockNumber() == blockNumber) return at;
    return -1;
}

// The log trims itself at kMaxBlocks, which invalidates the oldest rows' cursors: drop the rows
// that have scrolled out rather than rewrite a block that is no longer theirs.
void AgentInternalsView::forgetCalls() {
    while (m_calls.size() > kKeepRows) {
        m_calls.removeFirst();
        if (m_mergeHead >= 0) --m_mergeHead;
    }
    if (m_mergeHead < 0 || m_mergeHead >= m_calls.size()) { m_mergeHead = -1; m_merge.clear(); }
}

void AgentInternalsView::toolStarted(const QJsonObject &event) {
    const QString callId = event.value(QStringLiteral("call_id")).toString();
    const QString turnId = event.value(QStringLiteral("turn_id")).toString();
    const toollabel::Label label = toollabel::fromEvent(event);
    // Inside a run of reads the started row shows nothing of its own: the run's row is already
    // there, and a "reading x.py…" that lives a millisecond only flickers (LineCursor::start).
    if (m_mergeHead >= 0 && m_merge.active() && label.hasMerge && m_merge.accepts(label)) return;
    endThinking();
    ToolCall call;
    call.callId = callId;
    call.turnId = turnId;
    call.label = label;
    call.callIds << callId;
    ensureLineStart();
    const int start = m_log->document()->characterCount() - 1;
    append(QStringLiteral(" \n"), Ink::Tool);       // a block of its own, to rewrite in place
    call.line = QTextCursor(m_log->document());
    call.line.setPosition(start);
    call.after = QTextCursor(m_log->document());
    call.after.setPosition(m_log->document()->characterCount() - 1);
    call.after.setKeepPositionOnInsert(true);
    m_calls.append(call);
    forgetCalls();
    m_liveTick.invalidate();   // the new row's first counter draws at once
    drawRow(m_calls.last());
    touchTurn();
    setHeader();
}

// The live counter on a running command's row ("running pytest… · 120 lines"). Every chunk is
// counted; the row is redrawn at most ten times a second, as the terminal's own row is — a build
// that prints a thousand lines must not repaint a row a thousand times.
void AgentInternalsView::toolOutput(int lines) {
    if (m_calls.isEmpty() || m_calls.last().done) return;
    ToolCall &call = m_calls.last();
    call.liveLines += lines;
    if (m_liveTick.isValid() && m_liveTick.elapsed() < 100) return;
    m_liveTick.restart();
    drawRow(call);
    touchTurn();
    updateAskRow();   // a command that runs for a minute is a minute the last turn is taking
}

void AgentInternalsView::toolResult(const QJsonObject &event) {
    const QString callId = event.value(QStringLiteral("call_id")).toString();
    const QString turnId = event.value(QStringLiteral("turn_id")).toString();
    const toollabel::Label label = toollabel::fromEvent(event);
    int at = indexOfCall(callId, turnId);
    if (at < 0) {
        // A result with no started row (a merged member, or a worker that sends only results):
        // a run that accepts it grows the row above; anything else opens a row now.
        if (m_mergeHead >= 0 && m_merge.accepts(label)) {
            m_merge.add(label);
            ToolCall &head = m_calls[m_mergeHead];
            head.run = m_merge;
            head.merged = true;
            head.done = true;
            head.callIds << callId;
            head.members.append({label.line(), label.path, callId});
            head.detail.clear();
            head.asked = false;
            if (head.expanded) toggleToolCall(m_mergeHead);
            drawRow(head);
            touchTurn();
            setHeader();
            return;
        }
        toolStarted(QJsonObject{{"call_id", callId}, {"turn_id", turnId}, {"tool", event.value(QStringLiteral("tool"))}});
        at = m_calls.size() - 1;
        if (at < 0) return;
    }
    ToolCall &call = m_calls[at];
    call.label = label;
    call.done = true;
    call.turnId = turnId.isEmpty() ? call.turnId : turnId;
    call.diff = event.value(QStringLiteral("diff")).toString();
    call.liveLines = 0;
    drawRow(call);
    // A mergeable result opens a run: the next read of the same kind grows this row.
    m_merge.clear();
    if (label.hasMerge && !label.failed() && at == m_calls.size() - 1) {
        m_merge.add(label);
        m_mergeHead = at;
        call.run = m_merge;
        call.members.append({label.line(), label.path, callId});
    } else {
        m_mergeHead = -1;
    }
    touchTurn();
    setHeader();
}

// ----- folding a row open ----------------------------------------------------------------------

void AgentInternalsView::setToolOutput(const QJsonObject &reply) {
    const QString callId = reply.value(QStringLiteral("call_id")).toString();
    int at = indexOfCall(callId);
    if (at < 0) {
        // The reply names no call this view still has a row for: the row that asked is the last
        // one that asked and has nothing yet.
        for (int i = m_calls.size() - 1; i >= 0; --i)
            if (m_calls.at(i).asked && m_calls.at(i).detail.isEmpty()) { at = i; break; }
        if (at < 0) return;
    }
    ToolCall &call = m_calls[at];
    if (!call.label.valid) call.label = toollabel::fromEvent(reply);
    relay::calllines::FoldOptions options;
    options.diffToPane = relay::calllines::clickFor(call.label) == relay::calllines::Click::Diff;
    call.detail = relay::calllines::foldForReply(reply, palette(), options);
    call.detailNote.clear();
    if (call.expanded) { toggleToolCall(at); toggleToolCall(at); }   // redraw what is already open
    else toggleToolCall(at);
}

void AgentInternalsView::setToolOutputError(const QString &callId, const QString &text) {
    int at = indexOfCall(callId);
    if (at < 0)
        for (int i = m_calls.size() - 1; i >= 0; --i)
            if (m_calls.at(i).asked && m_calls.at(i).detail.isEmpty()) { at = i; break; }
    if (at < 0) return;
    ToolCall &call = m_calls[at];
    call.detail = relay::calllines::foldForNote(
        text.isEmpty() ? QStringLiteral("The detail of this call is not available any more.") : text, palette());
    if (call.expanded) { toggleToolCall(at); toggleToolCall(at); }
    else toggleToolCall(at);
}

void AgentInternalsView::toggleToolCall(int index) {
    if (index < 0 || index >= m_calls.size()) return;
    ToolCall &call = m_calls[index];
    if (call.expanded) {
        QTextCursor cursor(m_log->document());
        cursor.setPosition(call.after.position());
        cursor.setPosition(std::min(call.after.position() + call.foldChars, m_log->document()->characterCount() - 1),
                           QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        call.foldChars = 0;
        call.expanded = false;
        drawRow(call);
        return;
    }
    // A diff of more than 12 changed lines belongs in the diff pane, not in this narrow log (§ 23.6).
    if (relay::calllines::clickFor(call.label) == relay::calllines::Click::Diff && !call.diff.isEmpty() && onOpenDiff) {
        onOpenDiff(call.label.path.isEmpty() ? call.label.title : call.label.path, call.diff);
        return;
    }
    QVector<FoldLine> rows = call.detail;
    if (rows.isEmpty() && call.merged)
        rows = relay::calllines::foldForRun(call.members, palette(), relay::calllines::FoldOptions{});
    if (rows.isEmpty()) {
        // Nothing stored here: the worker has it. Ask, and say so under the row meanwhile — the
        // same `tool_output_get` round trip the terminal's own folds use.
        if (!call.asked && onOpenOutput && !call.callIds.isEmpty() && !call.turnId.isEmpty()) {
            call.asked = true;
            onOpenOutput(call.turnId, call.callIds.first());
            rows = relay::calllines::foldForNote(QStringLiteral("asking the agent for this call's output…"), palette());
        } else if (!call.detailNote.isEmpty()) {
            rows = relay::calllines::foldForNote(call.detailNote, palette());
        } else {
            rows = relay::calllines::foldForNote(QStringLiteral("No output was recorded for this call."), palette());
        }
    }
    const bool follow = pinned();
    QTextCursor cursor(m_log->document());
    cursor.setPosition(call.after.position());
    const int before = m_log->document()->characterCount();
    const QString pad(4, QLatin1Char(' '));
    for (const FoldLine &line : rows) {
        cursor.insertText(pad);
        for (const FoldSpan &span : line.spans) {
            QTextCharFormat format;
            format.setForeground(span.fg.isValid() ? span.fg : inkColor(Ink::Text));
            if (span.bg.isValid()) format.setBackground(span.bg);
            if (span.bold) format.setFontWeight(QFont::Bold);
            format.setFontItalic(span.italic);
            format.setFontUnderline(span.underline);
            cursor.insertText(sanitize(span.text), format);
        }
        cursor.insertText(QStringLiteral("\n"));
    }
    call.foldChars = m_log->document()->characterCount() - before;
    call.expanded = true;
    drawRow(call);
    if (follow) pin();
}

QStringList AgentInternalsView::toolLines() const {
    QStringList out;
    for (const ToolCall &call : m_calls) out << rowText(call);
    return out;
}

QString AgentInternalsView::plainText() const { return m_log->toPlainText(); }

bool AgentInternalsView::eventFilter(QObject *object, QEvent *event) {
    if (object == m_log->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        // Only a plain click on a tool row, and only with nothing selected: dragging across the
        // log to copy must not fold rows open under the pointer.
        if (mouse->button() == Qt::LeftButton && !m_log->textCursor().hasSelection()) {
            const int at = callAt(m_log->cursorForPosition(mouse->pos()).blockNumber());
            if (at >= 0) { toggleToolCall(at); return true; }
        }
    }
    if (object == m_log && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        // End pins the view to the bottom again (the card: "scrolling up unpins, End or reaching
        // the bottom re-pins"). Ctrl+End is the document end, which the editor already does.
        if (key->key() == Qt::Key_End && key->modifiers() == Qt::NoModifier) { pin(); return true; }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay
