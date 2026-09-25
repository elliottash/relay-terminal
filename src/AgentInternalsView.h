// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Activity pane (card #QT8C; named "Activity" by #4X53 -- the class and the pane type keep
// the "internals" spelling): the reasoning and the tool calls of one terminal pane,
// interleaved in the order they happened, live, beside the terminal that keeps the answer and the
// shell.
//
// The owner: "have a separate agent thinking pane (side-by-side but draggable) that you can then
// watch separate from your terminal pane", holding "thinking and tool calls". While this view is
// open its owner prints neither; when it closes, everything it took is reprinted into the terminal
// (src/InternalsLedger.h holds what, and Pane::reprintHiddenRows() writes it).
//
// A scrolling log, the shape the subagent transcript already uses (src/SubagentTranscript.h):
//
//   * a turn boundary is a thin muted rule carrying the request's first line;
//   * a reasoning block is a "✦ thinking… / ✦ thought for N s" header over its text rendered as
//     Markdown in the muted ink, uncapped — the inline fold's 6/18-row caps (#K48R) are about a
//     terminal grid, and this pane is where the whole block lives;
//   * a tool call is its § 23 label row, running → settled, a run of reads merged into one row,
//     which a click unfolds in place through the same `tool_output_get` round trip the terminal's
//     folds use. The fold's rows come from relay::calllines, so every section it learns to draw
//     (a diff, a task list) appears here without this file knowing about it. A diff of more than
//     12 changed lines still goes to the diff pane.
//
// It stays pinned to the bottom while output arrives; scrolling up unpins it, and End or reaching
// the bottom pins it again.
//
// At the foot of the pane, the "Ask" row (#FEJQ): this pane has no helper agent of its own — it is
// about the owning pane's agent, and that agent is the one that can say why a turn was slow — so
// the row drafts a question about what is on screen into that pane's prompt box. src/AskRow.h
// holds the wording, which the ⓘ pane's row shares.
#include "AskRow.h"
#include "CallLines.h"
#include "PaneView.h"
#include "ToolLabel.h"

#include <QColor>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QTextCursor>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

class QLabel;
class QPlainTextEdit;
class QShowEvent;

namespace relay {

class AgentInternalsView final : public QWidget, public relay::PaneView {
public:
    explicit AgentInternalsView(QWidget *parent = nullptr);

    // ----- relay::PaneView ---------------------------------------------------------------------
    QString paneTitle() const override;
    void focusView() override;
    void setHeaderRightInset(int pixels) override;

    // ----- what the owning pane feeds it -------------------------------------------------------

    // A turn's rule. Called with the request's first line as far as the pane knows it; a repeat of
    // the turn already at the bottom draws nothing.
    void beginTurn(const QString &turnId, const QString &request);
    // One reasoning block, as it streams and once more when it ends. `blockKey` tells two blocks of
    // the same turn apart (the pane's own anchor does); the same key rewrites the block in place.
    // `elapsedMs` below zero with `done` is a block that streamed before this pane existed, shown
    // whole under a header that claims no time.
    void setThinking(const QString &turnId, const QString &blockKey, const QString &text, bool done,
                     qint64 elapsedMs);
    // tool_started / tool_output / tool_result, exactly as the worker sends them (§ 23).
    void toolStarted(const QJsonObject &event);
    // A live `tool_output`, as its line count alone: this view never showed the text, only the
    // counter on the running row, and since #PPR4 the worker may send the count and keep the
    // text off the wire (§ 23.10). The full output is a click away either way, through the
    // `tool_output_get` round trip setToolOutput() answers.
    void toolOutput(int lines);
    void toolResult(const QJsonObject &event);
    // One provider call's `usage` (§ 4), as the muted line that closes that call: what it put in,
    // how much of it the provider's prefix cache served, and what came back. One line per call and
    // not per turn — a turn makes several calls, and "which request paid for the prefix again" is
    // the question a cached prompt is judged by (#GMCF decision 5).
    void noteUsage(const QJsonObject &usage);
    // The reply to the `tool_output_get` a click asked for: its rows go under the row that asked.
    void setToolOutput(const QJsonObject &reply);
    // The worker could not answer (the turn has left its log of fifty).
    void setToolOutputError(const QString &callId, const QString &text);
    // One muted line of the view's own ("Reasoning display is off…", "closed"). Repeats are dropped.
    void note(const QString &text);
    // The link in a reasoning fold: show that turn's block. Unpins and scrolls to it.
    void showThinking(const QString &turnId);
    bool hasThinking(const QString &turnId) const;
    // The theme changed under an open pane: redraw what is already written in the new inks.
    void refreshTheme();

    // A row was clicked and this view has no detail for it: the host asks the worker with
    // `tool_output_get` and answers with setToolOutput().
    std::function<void(const QString &turnId, const QString &callId)> onOpenOutput;
    // A diff of more than 12 changed lines: the host opens a diff pane beside the terminal (§ 23.6).
    std::function<void(const QString &title, const QString &unifiedDiff)> onOpenDiff;
    // The Ask row's draft: the question goes into the owning pane's composer, at the cursor, and
    // the composer takes focus (Pane::insertInComposer). A draft the person confirms, never a
    // send. Left unset the row is not shown at all; the view notices on its own, so the window has
    // only to assign this.
    std::function<void(const QString &text)> onAskOwner;

    // ----- for the pane header, the tests and the QA screenshots -------------------------------
    int toolRowCount() const { return int(m_calls.size()); }
    int turnCount() const { return m_turns; }
    QString plainText() const;
    // Folds one tool row open, or shut again (the mouse path, and the tests').
    void toggleToolCall(int index);
    QStringList toolLines() const;
    // The Ask row, for the tests and for a QA screenshot. It has no Q_OBJECT, so findChild()
    // cannot pick it out of the pane by type.
    relay::askrow::AskRow *askRow() const { return m_ask; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Ink { Text, Tool, Muted, Error, User };
    static QColor inkColor(Ink ink);
    static relay::calllines::Palette palette();

    // One tool row: the line, what a click folds open under it, and where both sit in the log.
    struct ToolCall {
        QString callId, turnId, timestamp;
        QStringList callIds;                            // the members of a merged run
        toollabel::Label label;
        toollabel::MergeRun run;                        // the run this row stands for, once merged
        QVector<calllines::RunMember> members;
        QString diff;
        QVector<FoldLine> detail;                       // the reply's rows, once they have arrived
        QString detailNote;                             // "asking the agent…", or why there is none
        QTextCursor line;                               // the row's own block
        QTextCursor after;                              // where the fold is inserted
        int foldChars = 0;
        bool expanded = false, done = false, merged = false, asked = false;
        qint64 liveLines = 0;
    };

    void append(const QString &text, Ink ink, bool bold = false);
    void appendFoldLines(const QVector<FoldLine> &lines, int indent);
    void ensureLineStart() { if (!m_atLineStart) append(QStringLiteral("\n"), Ink::Muted); }
    // The reasoning block, drawn where setThinking() decided to draw it: the settled rows appended
    // once and only the tail after them redrawn (#PPR4).
    void drawThinking(const QString &turnId, const QString &key, const QString &text, bool done,
                      qint64 elapsedMs);
    // A block held while the pane was hidden goes in before anything else can print under it.
    void flushHeldThinking();
    void endThinking();
    QString rowText(const ToolCall &call) const;
    void drawRow(ToolCall &call);
    void rewriteLine(ToolCall &call, const QString &text, Ink ink);
    int callAt(int blockNumber) const;
    // The row for a call, in `turnId` when one is given: a worker numbers calls per turn, so the
    // same id can name a call in two turns, and the newer row must not answer for the older.
    int indexOfCall(const QString &callId, const QString &turnId = QString()) const;
    void forgetCalls();
    bool pinned() const;
    void pin();
    void setHeader();
    // What the Ask row says, and whether it is shown at all. Called wherever the figures on it can
    // have moved: a turn, a tool event, and the text cursor landing in another turn.
    void updateAskRow();
    // The turn the reader is looking at: the last rule at or above the text cursor. With the
    // cursor untouched — the usual case, the view pinned to the bottom — that is the newest turn.
    int turnAtCursor() const;
    // Every event of the turn that is running moves its clock on, so "the last turn took 42 s" is
    // the time up to the last thing it did rather than the time since the rule was drawn.
    void touchTurn();

    // One turn's rule, for the Ask row. The number is this pane's own count — a pane opened mid
    // conversation starts at 1 — so the request's first line travels with it and the agent can
    // find the turn either way (askrow::turnQuestion says so in the question).
    struct TurnMark {
        int number = 0;
        QString turnId, request;
        QTextCursor at;                // the rule's block, so trimming the log does not move it
        qint64 startMs = 0, lastMs = 0;   // both on m_clock
    };

    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_log = nullptr;
    relay::askrow::AskRow *m_ask = nullptr;
    QVector<TurnMark> m_turnMarks;    // oldest first, capped like m_blocks
    QElapsedTimer m_clock;            // started when the view is built; every mark reads it
    QVector<ToolCall> m_calls;
    toollabel::MergeRun m_merge;
    int m_mergeHead = -1;
    int m_turns = 0;
    QString m_turnId;                 // the turn the last rule was drawn for
    QString m_thinkingKey;            // the block being rewritten in place, empty when none is
    QTextCursor m_thinkingAt;         // where that block starts
    // The block streams in: only what has settled is written, and only the rows after it are
    // redrawn per flush (#PPR4). The stream renders forward, so a `text` that is not a
    // continuation of what it has already drawn — the 400 000-character window sliding past the
    // block's start — takes the block down and renders it again.
    std::unique_ptr<calllines::MarkdownStream> m_stream;
    QString m_streamTurn;             // the turn the open block belongs to
    QString m_streamFed;              // the body the stream has already been given
    QString m_streamHead;             // the header row as it is drawn now
    QTextCursor m_streamHeadAt;       // that row, so the end of the block can rewrite it in place
    QTextCursor m_streamTailAt;       // where the redrawn rows start; everything after it is the tail
    bool m_streamDrewRows = false;    // anything but "(nothing yet)" is under the header
    // What setThinking() was given while the pane was hidden: a hidden pane draws nothing at all,
    // and this is written the moment it is shown, or the moment anything else has to print under
    // it — which is what keeps the log in the order things happened.
    struct HeldThinking {
        bool live = false;
        QString turnId, key, text;
        bool done = false;
        qint64 elapsedMs = 0;
    };
    HeldThinking m_held;
    bool m_everShown = false;
    QString m_lastNote;
    QVector<QPair<QString, int>> m_blocks;   // (turn, position) of each reasoning block, for the link
    QElapsedTimer m_liveTick;                // last redraw of the running row's line counter
    bool m_atLineStart = true;
};

}  // namespace relay
