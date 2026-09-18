// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubagentTranscript.h"
#include "Theme.h"
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
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
}  // namespace

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
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMaximumBlockCount(6000);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_log, 1);
    m_input = new QLineEdit;
    m_input->setObjectName(QStringLiteral("subagentInput"));
    m_input->setPlaceholderText(QStringLiteral("Message %1 · Enter sends (resumes it if finished) · Esc closes").arg(id));
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

void SubagentTranscriptView::setRow(const SubagentRow &row, qint64 elapsedMs) {
    m_type = row.type; m_description = row.description;
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
    const QString clean = sanitize(text);
    if (clean.isEmpty()) return;
    // Same two-level scheme as the terminal's inks (main.cpp, owner 2026-09-18): user lines carry
    // a destination colour and machine lines are grey. Every user line here goes to an agent, so
    // "User" is the agent violet, not the shell cyan.
    QColor color(226, 229, 235);
    switch (ink) {
    case Ink::Agent: break;
    case Ink::User: color = QColor(180, 142, 247); break;
    case Ink::Tool: case Ink::ToolOutput: case Ink::Note: color = QColor(128, 135, 150); break;
    case Ink::DiffAdd: color = QColor(126, 200, 140); break;
    case Ink::DiffRemove: case Ink::Error: color = QColor(240, 113, 120); break;
    }
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 4;
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setForeground(color);
    if (ink == Ink::User) format.setFontWeight(QFont::Bold);
    if (ink == Ink::Note) format.setFontItalic(true);
    cursor.insertText(clean, format);
    m_atLineStart = clean.endsWith(QLatin1Char('\n'));
    if (follow) bar->setValue(bar->maximum());
}

void SubagentTranscriptView::appendNote(const QString &text) {
    ensureLineStart();
    append(text + QLatin1Char('\n'), Ink::Note);
}

void SubagentTranscriptView::toolStarted(const QJsonObject &payload) {
    const QString preview = payload.value(QStringLiteral("preview")).toString();
    QStringList lines = preview.left(6000).split(QLatin1Char('\n'));
    const QString heading = lines.isEmpty() ? QString() : lines.takeFirst().trimmed();
    QStringList body;
    for (const QString &line : std::as_const(lines)) {
        if (line.startsWith(QStringLiteral("Working directory: ")) || line.startsWith(QStringLiteral("Timeout: ")) || line.startsWith(QStringLiteral("Old bytes: "))) continue;
        if (body.isEmpty() && line.trimmed().isEmpty()) continue;
        body << line;
    }
    while (!body.isEmpty() && body.last().trimmed().isEmpty()) body.removeLast();
    const QString verb = heading == QStringLiteral("RUN COMMAND") ? QStringLiteral("$")
                       : heading == QStringLiteral("READ FILE") ? QStringLiteral("read")
                       : heading == QStringLiteral("LIST DIRECTORY") ? QStringLiteral("list")
                       : heading == QStringLiteral("WRITE FILE") ? QStringLiteral("write")
                       : payload.value(QStringLiteral("tool")).toString();
    const QString head = body.isEmpty() ? QString() : body.takeFirst();
    ensureLineStart();
    append(QStringLiteral("⚙ ") + verb + QLatin1Char(' ') + head + QLatin1Char('\n'), Ink::Tool);
    for (const QString &line : std::as_const(body)) {
        if (line.trimmed().isEmpty()) continue;
        Ink ink = Ink::ToolOutput;
        if (line.startsWith(QLatin1Char('+')) && !line.startsWith(QStringLiteral("+++"))) ink = Ink::DiffAdd;
        else if (line.startsWith(QLatin1Char('-')) && !line.startsWith(QStringLiteral("---"))) ink = Ink::DiffRemove;
        append(line + QLatin1Char('\n'), ink);
    }
}

void SubagentTranscriptView::toolResult(const QJsonObject &payload) {
    const QJsonObject result = payload.value(QStringLiteral("result")).toObject();
    ensureLineStart();
    if (result.contains(QStringLiteral("error"))) append(QStringLiteral("✗ ") + result.value(QStringLiteral("error")).toString() + QLatin1Char('\n'), Ink::Error);
    else if (result.contains(QStringLiteral("exit_code"))) {
        const int code = result.value(QStringLiteral("exit_code")).toInt();
        append(QStringLiteral("exit %1\n").arg(code), code == 0 ? Ink::Note : Ink::Error);
    } else append(QStringLiteral("✓ ") + payload.value(QStringLiteral("tool")).toString() + QLatin1Char('\n'), Ink::Note);
}

void SubagentTranscriptView::handleEvent(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("subagent_transcript")) {
        m_log->clear(); m_atLineStart = true; m_snapshot = true;
        for (const auto &value : event.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject message = value.toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            const QString content = message.value(QStringLiteral("content")).toString();
            ensureLineStart();
            if (role == QStringLiteral("user")) {
                append(QStringLiteral("› ") + content.trimmed() + QLatin1Char('\n'), Ink::User);
            } else if (role == QStringLiteral("assistant")) {
                if (!content.trimmed().isEmpty()) append(content.trimmed() + QLatin1Char('\n'), Ink::Agent);
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
    if (kind == QStringLiteral("delta")) append(payload.value(QStringLiteral("text")).toString(), Ink::Agent);
    else if (kind == QStringLiteral("tool_started")) toolStarted(payload);
    else if (kind == QStringLiteral("tool_output")) append(payload.value(QStringLiteral("text")).toString(), Ink::ToolOutput);
    else if (kind == QStringLiteral("tool_result")) toolResult(payload);
    // "status" payloads (step counters) update the row via subagent_progress; they are not logged.
}

}  // namespace relay
