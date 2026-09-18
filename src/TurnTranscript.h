// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// One agent turn, opened from the inline "✦ N tool calls" line: a list of tool calls above the
// turn's transcript.
//
// Each row is the call's concise line (docs/AGENT-SESSIONS-PROTOCOL.md § 23, card #TK9C) — the
// label's `title` and `stats`, "✓" or "✗" from its `ok`, and the exact duration in a second
// column — not the tool's name and the first line of its preview. A run of consecutive reads or
// listings becomes one parent row ("read 6 files · 4,100 lines") whose children are the calls
// themselves, so each one can still be opened. Activating a row asks the host for that call's
// output; the host hands the reply back through setToolOutput(), which renders the `detail`
// sections into the log.
#include "ToolLabel.h"
#include <QJsonObject>
#include <QWidget>
#include <functional>

class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace relay {

class TurnTranscriptView final : public QWidget {
public:
    explicit TurnTranscriptView(const QString &turnId, QWidget *parent = nullptr);

    QString turnId() const { return m_turnId; }
    QString title() const;

    // Activating a tool call row (double-click, Enter, or the Open output button). The host answers
    // with tool_output_get and feeds the reply to setToolOutput().
    std::function<void(const QString &callId)> onOpenOutput;

    // A diff too big to read in the log (`open: {"type": "diff"}`, more than 12 changed lines) was
    // opened. **The host wires this**: this view has no pane of its own, so it hands over the title
    // and the unified diff and the host opens them in a diff pane (src/DiffView.h, `DiffView::
    // setDiff(title, unifiedDiff)`). Unset: the diff is written into the log instead.
    std::function<void(const QString &title, const QString &unifiedDiff)> onOpenDiff;

    // turn_summary {turn_id, elapsed_ms, thinking_ms, tools: [{call_id, name, preview, ok,
    // exit_code?, label?}]} — `label` is § 23's, and `preview` is the fallback for a worker that
    // sends none.
    void setSummary(const QJsonObject &summary);
    // turn_transcript {turn_id, items|messages: [{role, content, tool_calls?}]}
    void setTranscript(const QJsonObject &transcript);
    // The reply to tool_output_get {call_id, label?, detail?, diff?, text|preview}: the call's line,
    // then its `detail` sections (§ 23.5) in order, written into the log. Without `detail` it falls
    // back to the reply's own text, which is what an older worker sends.
    void setToolOutput(const QJsonObject &reply);
    // Thinking text collected while the turn ran (optional).
    void setThinking(const QString &text);
    void focusInput();
    // Tool calls in the turn — the number the title says, not the number of rows, which is smaller
    // when a run of reads has been merged.
    int toolCount() const { return m_toolCount; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void openSelected();
    // One call's row: the line in column 0, the exact duration in column 1.
    QTreeWidgetItem *addRow(QTreeWidgetItem *parent, const QJsonObject &tool, const toollabel::Label &label);
    QString m_turnId;
    QLabel *m_header = nullptr;
    QTreeWidget *m_tools = nullptr;
    QPlainTextEdit *m_log = nullptr;
    qint64 m_elapsedMs = 0, m_thinkingMs = 0;
    int m_toolCount = 0;
};

}  // namespace relay
