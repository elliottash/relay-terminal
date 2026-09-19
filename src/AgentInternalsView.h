// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The agent internals pane (card #QT8C): the reasoning and the tool calls of one terminal pane,
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

class QLabel;
class QPlainTextEdit;

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
    void toolOutput(const QString &text);
    void toolResult(const QJsonObject &event);
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

    // ----- for the pane header, the tests and the QA screenshots -------------------------------
    int toolRowCount() const { return int(m_calls.size()); }
    int turnCount() const { return m_turns; }
    QString plainText() const;
    // Folds one tool row open, or shut again (the mouse path, and the tests').
    void toggleToolCall(int index);
    QStringList toolLines() const;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    enum class Ink { Text, Tool, Muted, Error, User };
    static QColor inkColor(Ink ink);
    static relay::calllines::Palette palette();

    // One tool row: the line, what a click folds open under it, and where both sit in the log.
    struct ToolCall {
        QString callId, turnId;
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
    void endThinking() { m_thinkingKey.clear(); }
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

    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QVector<ToolCall> m_calls;
    toollabel::MergeRun m_merge;
    int m_mergeHead = -1;
    int m_turns = 0;
    QString m_turnId;                 // the turn the last rule was drawn for
    QString m_thinkingKey;            // the block being rewritten in place, empty when none is
    QTextCursor m_thinkingAt;         // where that block starts
    int m_thinkingChars = 0;
    QString m_lastNote;
    QVector<QPair<QString, int>> m_blocks;   // (turn, position) of each reasoning block, for the link
    QElapsedTimer m_liveTick;                // last redraw of the running row's line counter
    bool m_atLineStart = true;
};

}  // namespace relay
