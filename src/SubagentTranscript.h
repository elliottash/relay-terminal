// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Live transcript of one subagent: the subagent_transcript snapshot, then streamed deltas, tool
// calls and outputs (subagent_event payloads), with a small input that sends agent_message.
#include "SubagentsPanel.h"
#include <QJsonObject>
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

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

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    enum class Ink { Agent, User, Tool, ToolOutput, DiffAdd, DiffRemove, Error, Note };
    void append(const QString &text, Ink ink);
    void ensureLineStart() { if (!m_atLineStart) append(QStringLiteral("\n"), Ink::Note); }
    void toolStarted(const QJsonObject &payload);
    void toolResult(const QJsonObject &payload);
    QString m_id, m_type, m_description;
    QLabel *m_title = nullptr, *m_status = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLineEdit *m_input = nullptr;
    bool m_atLineStart = true, m_snapshot = false;
};

}  // namespace relay
