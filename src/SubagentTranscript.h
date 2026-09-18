// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Live transcript of one subagent: the subagent_transcript snapshot, then streamed deltas, tool
// calls and outputs (subagent_event payloads), with a small input that sends agent_message.
//
// A tool call is **one line** (docs/AGENT-SESSIONS-PROTOCOL.md § 23, card #TK9C): "reading x.py"
// while it runs, rewritten in the same row to "read x.py · 412 lines" when it lands. Clicking the
// line folds its detail open underneath — the command and its output, the arguments, the diff —
// and clicking it again folds it shut. A short diff (`inline_diff`) prints under the line with no
// click at all, added lines green and removed lines red, and a run of consecutive reads or
// listings collapses into "read 6 files · 4,100 lines".
#include "SubagentsPanel.h"
#include "ToolLabel.h"
#include <QJsonObject>
#include <QTextCursor>
#include <QVector>
#include <QWidget>
#include <functional>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

namespace relay {

class SubagentTranscriptView final : public QWidget {
    Q_OBJECT
public:
    explicit SubagentTranscriptView(const QString &id, QWidget *parent = nullptr);

    QString agentId() const { return m_id; }
    QString title() const;

    std::function<void(const QString &text)> onSend;
    std::function<void()> onClose;

    // Row state from the model: title, status line.
    void setRow(const SubagentRow &row, qint64 elapsedMs);
    // subagent_transcript {status, messages}, subagent_event {payload}, subagent_progress, subagent_finished.
    void handleEvent(const QJsonObject &event);
    void appendNote(const QString &text);
    void focusInput();
    QString plainText() const;
    // In a pane of its own the pane chrome's × closes it, so its own × (which the floating overlay
    // needs) is hidden; two crosses there landed on top of each other (owner report, 2026-09-18).
    void setHostedInPane(bool hosted);
    // Room kept free at the right of the title row for the pane chrome's buttons (PaneChrome).
    void setHeaderRightInset(int pixels);

    // A tool line whose diff is too big to print inline (`open: {"type": "diff"}`, more than 12
    // changed lines) was clicked. The host opens it in a diff pane (src/DiffView.h); this view has
    // no pane of its own, so it only reports. Unset: the detail folds open in place instead.
    std::function<void(const QString &title, const QString &unifiedDiff)> onOpenDiff;

    // The tool lines drawn so far, newest last, exactly as they read (marker included). A merged
    // run of reads is one entry. For tests and for the pane's own bookkeeping.
    QStringList toolLines() const;
    int toolCallCount() const { return int(m_calls.size()); }
    // Folds one tool line's detail open, or shut again. Out-of-range does nothing.
    void toggleToolCall(int index);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    enum class Ink { Agent, User, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note };
    // One tool call's row: the line itself, what a click folds open under it, and where both sit.
    struct ToolCall {
        QString callId;
        toollabel::Label label;
        QString detail;          // command, arguments, output, error — what the fold shows
        QString diff;            // the unified diff of a write or an edit, when the event had one
        QTextCursor line;        // start of the row's own first block (the line, then any inline diff)
        QTextCursor after;       // start of the block under the row; the fold is inserted here
        int foldChars = 0;       // characters the open fold occupies, 0 when it is shut
        bool expanded = false;
        bool done = false;
    };
    void append(const QString &text, Ink ink);
    void ensureLineStart() { if (!m_atLineStart) append(QStringLiteral("\n"), Ink::Note); }
    void toolStarted(const QJsonObject &payload);
    void toolResult(const QJsonObject &payload);
    void toolOutput(const QString &text);
    // Replaces the whole block `call.line` sits in, keeping everything around it.
    void rewriteLine(ToolCall &call, const QString &text, Ink ink);
    // The row as it should read now: the fold arrow, the ✗ of a failure, and the label's line.
    QString rowText(const ToolCall &call) const;
    void drawRow(ToolCall &call);
    // Prints a unified diff under the row, added lines green and removed lines red.
    void appendDiff(const QString &diff);
    int callAt(int blockNumber) const;
    int indexOfCall(const QString &callId) const;
    void forgetCalls();
    QString m_id, m_type, m_description;
    QHBoxLayout *m_header = nullptr;
    QToolButton *m_close = nullptr;
    QLabel *m_title = nullptr, *m_status = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLineEdit *m_input = nullptr;
    QVector<ToolCall> m_calls;
    toollabel::MergeRun m_merge;
    int m_mergeHead = -1;        // the row showing the merged run, or -1 when none is open
    bool m_atLineStart = true, m_snapshot = false;
};

}  // namespace relay
