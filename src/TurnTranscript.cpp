// SPDX-License-Identifier: GPL-3.0-or-later
#include "TurnTranscript.h"
#include "Theme.h"
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay {

TurnTranscriptView::TurnTranscriptView(const QString &turnId, QWidget *parent) : QWidget(parent), m_turnId(turnId) {
    setObjectName(QStringLiteral("turnTranscript"));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(8, 6, 8, 6); layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    m_header = new QLabel(QStringLiteral("Agent turn"));
    m_header->setObjectName(QStringLiteral("turnHeader"));
    header->addWidget(m_header, 1);
    auto *open = new QPushButton(QStringLiteral("Open output"));
    open->setToolTip(QStringLiteral("Open the selected tool call's full output in a preview pane (Enter)"));
    connect(open, &QPushButton::clicked, this, [this] { openSelected(); });
    header->addWidget(open);
    layout->addLayout(header);
    auto *split = new QSplitter(Qt::Vertical);
    m_tools = new QTreeWidget;
    m_tools->setObjectName(QStringLiteral("turnTools"));
    m_tools->setHeaderHidden(true);
    m_tools->setColumnCount(1);
    m_tools->setUniformRowHeights(false);
    m_tools->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tools->installEventFilter(this);
    connect(m_tools, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *, int) { openSelected(); });
    split->addWidget(m_tools);
    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("turnLog"));
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    split->addWidget(m_log);
    split->setSizes({200, 300});
    layout->addWidget(split, 1);
}

QString TurnTranscriptView::title() const {
    return QStringLiteral("Turn · %1 tool call%2").arg(toolCount()).arg(toolCount() == 1 ? QString() : QStringLiteral("s"));
}

int TurnTranscriptView::toolCount() const { return m_tools->topLevelItemCount(); }

void TurnTranscriptView::setSummary(const QJsonObject &summary) {
    m_elapsedMs = summary.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
    m_thinkingMs = summary.value(QStringLiteral("thinking_ms")).toVariant().toLongLong();
    m_tools->clear();
    for (const auto &value : summary.value(QStringLiteral("tools")).toArray()) {
        const QJsonObject tool = value.toObject();
        const bool ok = tool.value(QStringLiteral("ok")).toBool(true);
        QString line = (ok ? QStringLiteral("✓ ") : QStringLiteral("✗ ")) + tool.value(QStringLiteral("name")).toString();
        const QString preview = tool.value(QStringLiteral("preview")).toString();
        const QString first = preview.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!first.isEmpty()) line += QStringLiteral("  ") + first.left(120);
        if (tool.contains(QStringLiteral("exit_code"))) line += QStringLiteral("  · exit %1").arg(tool.value(QStringLiteral("exit_code")).toInt());
        auto *row = new QTreeWidgetItem(m_tools, {line});
        row->setData(0, Qt::UserRole, tool.value(QStringLiteral("call_id")).toString());
        row->setForeground(0, ok ? relay::theme::Text : relay::theme::SyntaxUnknown);
        row->setToolTip(0, QStringLiteral("Enter or double-click opens the full output"));
        if (preview.contains(QLatin1Char('\n')) || preview.size() > 120) {
            // Expanding a row shows the whole preview (command, path or diff).
            auto *child = new QTreeWidgetItem(row, {preview.left(4000)});
            child->setForeground(0, relay::theme::TextMuted);
            child->setData(0, Qt::UserRole, tool.value(QStringLiteral("call_id")).toString());
        }
    }
    QString header = title();
    if (m_elapsedMs > 0) header += QStringLiteral(" · %1 s").arg((m_elapsedMs + 500) / 1000);
    if (m_thinkingMs > 0) header += QStringLiteral(" · thought %1 s").arg((m_thinkingMs + 500) / 1000);
    m_header->setText(header);
    if (m_tools->topLevelItemCount()) m_tools->setCurrentItem(m_tools->topLevelItem(0));
}

void TurnTranscriptView::setTranscript(const QJsonObject &transcript) {
    m_log->clear();
    QJsonArray items = transcript.value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) items = transcript.value(QStringLiteral("messages")).toArray();
    QTextCursor cursor(m_log->document());
    auto add = [&cursor](const QString &text, const QColor &color, bool bold) {
        QTextCharFormat format; format.setForeground(color); if (bold) format.setFontWeight(QFont::Bold);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text, format);
    };
    for (const auto &value : items) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString content = message.value(QStringLiteral("content")).toString().trimmed();
        if (role == QStringLiteral("user")) add(QStringLiteral("› ") + content + QLatin1Char('\n'), relay::theme::Accent, true);
        else if (role == QStringLiteral("assistant")) {
            if (!content.isEmpty()) add(content + QLatin1Char('\n'), relay::theme::Text, false);
            QStringList calls;
            for (const auto &call : message.value(QStringLiteral("tool_calls")).toArray())
                calls << (call.isObject() ? call.toObject().value(QStringLiteral("name")).toString() : call.toString());
            if (!calls.isEmpty()) add(QStringLiteral("⚙ ") + calls.join(QStringLiteral(", ")) + QLatin1Char('\n'), QColor(0xe5, 0xc0, 0x7b), false);
        } else if (role == QStringLiteral("tool")) {
            QStringList lines = content.split(QLatin1Char('\n'));
            const int total = lines.size();
            if (total > 12) { lines = lines.mid(0, 12); lines << QStringLiteral("… %1 more lines").arg(total - 12); }
            add(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'), relay::theme::TextMuted, false);
        }
    }
    if (items.isEmpty()) add(QStringLiteral("(Transcript not available from this worker.)\n"), relay::theme::TextMuted, false);
}

void TurnTranscriptView::setThinking(const QString &text) {
    if (text.trimmed().isEmpty()) return;
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::Start);
    QTextCharFormat format; format.setForeground(relay::theme::TextMuted); format.setFontItalic(true);
    cursor.insertText(QStringLiteral("Thinking\n") + text.trimmed().left(20000) + QStringLiteral("\n\n"), format);
}

void TurnTranscriptView::focusInput() { m_tools->setFocus(Qt::OtherFocusReason); }

void TurnTranscriptView::openSelected() {
    QTreeWidgetItem *row = m_tools->currentItem();
    if (!row) return;
    const QString callId = row->data(0, Qt::UserRole).toString();
    if (!callId.isEmpty() && onOpenOutput) onOpenOutput(callId);
}

bool TurnTranscriptView::eventFilter(QObject *object, QEvent *event) {
    if (object == m_tools && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) { openSelected(); return true; }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay
