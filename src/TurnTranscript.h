// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// One agent turn, opened from the inline "✦ N tool calls" line: an expandable list of tool calls
// (activating one opens its full output in a preview pane) above the turn's transcript.
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

    // Activating a tool call row (double-click, Enter, or the Open output button).
    std::function<void(const QString &callId)> onOpenOutput;

    // turn_summary {turn_id, elapsed_ms, thinking_ms, tools: [{call_id, name, preview, ok, exit_code?}]}
    void setSummary(const QJsonObject &summary);
    // turn_transcript {turn_id, items|messages: [{role, content, tool_calls?}]}
    void setTranscript(const QJsonObject &transcript);
    // Thinking text collected while the turn ran (optional).
    void setThinking(const QString &text);
    void focusInput();
    int toolCount() const;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void openSelected();
    QString m_turnId;
    QLabel *m_header = nullptr;
    QTreeWidget *m_tools = nullptr;
    QPlainTextEdit *m_log = nullptr;
    qint64 m_elapsedMs = 0, m_thinkingMs = 0;
};

}  // namespace relay
