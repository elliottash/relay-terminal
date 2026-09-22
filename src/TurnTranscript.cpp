// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TurnTranscript.h"
#include "CopyOnSelect.h"
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
    // Column 0 is the call's concise line; column 1 the exact duration, right of it (§ 23).
    m_tools->setColumnCount(2);
    m_tools->header()->setStretchLastSection(false);
    m_tools->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tools->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tools->setUniformRowHeights(false);
    m_tools->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tools->installEventFilter(this);
    connect(m_tools, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *, int) { openSelected(); });
    split->addWidget(m_tools);
    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("turnLog"));
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    relay::installCopyOnSelect(m_log);
    split->addWidget(m_log);
    split->setSizes({200, 300});
    layout->addWidget(split, 1);
}

QString TurnTranscriptView::title() const {
    return QStringLiteral("Turn · %1 tool call%2").arg(toolCount()).arg(toolCount() == 1 ? QString() : QStringLiteral("s"));
}

namespace {
// The exact time a call took, for the second column: "820 ms", "8.1 s". The label's stats round
// this to whole or one-decimal seconds and drop anything under a second, which is most calls.
QString exactDuration(const QJsonObject &tool) {
    const qint64 ms = tool.value(QStringLiteral("ms")).toVariant().toLongLong();
    if (ms <= 0) return {};
    if (ms < 1000) return QStringLiteral("%1 ms").arg(ms);
    return QStringLiteral("%1 s").arg(ms / 1000.0, 0, 'f', ms < 10000 ? 1 : 0);
}
}  // namespace

QTreeWidgetItem *TurnTranscriptView::addRow(QTreeWidgetItem *parent, const QJsonObject &tool,
                                            const toollabel::Label &label) {
    const bool ok = !label.failed();
    const QString line = (ok ? QStringLiteral("✓ ") : QStringLiteral("✗ ")) + label.line();
    auto *row = parent ? new QTreeWidgetItem(parent, {line, exactDuration(tool)})
                       : new QTreeWidgetItem(m_tools, {line, exactDuration(tool)});
    const QString callId = tool.value(QStringLiteral("call_id")).toString();
    row->setData(0, Qt::UserRole, callId);
    row->setForeground(0, ok ? relay::theme::Text : label.refused ? relay::theme::TextMuted : relay::theme::SyntaxUnknown);
    row->setForeground(1, relay::theme::TextMuted);
    row->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    QString tip = QStringLiteral("Enter or double-click opens the full output");
    if (!label.path.isEmpty()) tip = label.path + QLatin1Char('\n') + tip;
    row->setToolTip(0, tip);
    return row;
}

void TurnTranscriptView::setSummary(const QJsonObject &summary) {
    m_elapsedMs = summary.value(QStringLiteral("elapsed_ms")).toVariant().toLongLong();
    m_thinkingMs = summary.value(QStringLiteral("thinking_ms")).toVariant().toLongLong();
    m_tools->clear();
    m_toolCount = 0;
    // A run of consecutive reads or listings is one parent row (§ 23.7); its members stay under it,
    // each with its own call id, so any one of them can still be opened.
    toollabel::MergeRun run;
    QTreeWidgetItem *head = nullptr;
    QJsonObject headTool;
    toollabel::Label headLabel;
    for (const auto &value : summary.value(QStringLiteral("tools")).toArray()) {
        const QJsonObject tool = value.toObject();
        const toollabel::Label label = toollabel::fromEvent(tool);
        ++m_toolCount;
        if (run.accepts(label) && head) {
            if (run.count() == 1) {           // the run's first member moves under its own head row
                head->setText(1, QString());
                addRow(head, headTool, headLabel);
            }
            run.add(label);
            addRow(head, tool, label);
            head->setText(0, QStringLiteral("▸ ") + run.line());
            head->setData(0, Qt::UserRole, QString());   // the run itself has no output to open
            head->setToolTip(0, QStringLiteral("%1 calls; open one of them for its output").arg(run.count()));
            continue;
        }
        run.clear();
        head = addRow(nullptr, tool, label);
        headTool = tool;
        headLabel = label;
        if (label.hasMerge && !label.failed()) run.add(label);
        // Only a worker that sends no label at all still needs the preview spelled out; with a
        // label the line says everything the preview's first line used to (§ 23.1).
        const QString preview = tool.value(QStringLiteral("preview")).toString();
        if (label.fallback && (preview.contains(QLatin1Char('\n')) || preview.size() > 120)) {
            auto *child = new QTreeWidgetItem(head, {preview.left(4000), QString()});
            child->setForeground(0, relay::theme::TextMuted);
            child->setData(0, Qt::UserRole, tool.value(QStringLiteral("call_id")).toString());
        } else if (!label.error.isEmpty()) {
            auto *child = new QTreeWidgetItem(head, {label.error, QString()});
            child->setForeground(0, label.refused ? relay::theme::TextMuted : relay::theme::SyntaxUnknown);
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
    m_transcript = transcript;
    renderLog();
}

// Thinking text collected while the turn ran. Held, not written once: the transcript arrives a
// round trip after the pane opens and rebuilds the log, and before #K48R that reply wiped the
// reasoning the fold's "open in pane" link had just promised — the pane opened with the tool list,
// the messages, and nothing of what the model had thought. The stream keeps calling this as the
// block grows, so it is idempotent: it replaces the text and redraws.
void TurnTranscriptView::setThinking(const QString &text) {
    if (text == m_thinking) return;
    m_thinking = text;
    renderLog();
}

// The log is the thinking block, then the turn's messages: whichever of the two the host has.
// setToolOutput() takes the log over for one call's output, which is an explicit ask, and the next
// of either of these puts it back.
void TurnTranscriptView::renderLog() {
    m_log->clear();
    if (!m_thinking.trimmed().isEmpty()) {
        QTextCursor cursor(m_log->document());
        QTextCharFormat format;   // upright, muted: see "Legible text"
        format.setForeground(relay::theme::TextMuted);
        // The fold in the terminal is six rows while it streams and eighteen when it settles
        // (#K48R); this pane is where the rest of it is, so the cut here is generous enough that
        // no reasoning anyone will read reaches it.
        const QString text = m_thinking.trimmed();
        cursor.insertText(QStringLiteral("Thinking\n") + text.left(kThinkingChars)
                              + (text.size() > kThinkingChars
                                     ? QStringLiteral("\n… %1 more characters").arg(text.size() - kThinkingChars)
                                     : QString())
                              + QStringLiteral("\n\n"), format);
    }
    QJsonArray items = m_transcript.value(QStringLiteral("items")).toArray();
    if (items.isEmpty()) items = m_transcript.value(QStringLiteral("messages")).toArray();
    if (items.isEmpty() && m_transcript.isEmpty()) return;   // nothing asked for yet
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
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
            // The tool line is brass (theme::Tool), not amber: amber is "this is waiting on you"
            // and nothing but that (be81edb). A list of calls the agent already made is not.
            if (!calls.isEmpty()) add(QStringLiteral("⚙ ") + calls.join(QStringLiteral(", ")) + QLatin1Char('\n'), relay::theme::Tool, false);
        } else if (role == QStringLiteral("tool")) {
            QStringList lines = content.split(QLatin1Char('\n'));
            const int total = lines.size();
            if (total > 12) { lines = lines.mid(0, 12); lines << QStringLiteral("… %1 more lines").arg(total - 12); }
            add(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'), relay::theme::TextMuted, false);
        }
    }
    if (items.isEmpty()) add(QStringLiteral("(Transcript not available from this worker.)\n"), relay::theme::TextMuted, false);
}

// The reply to tool_output_get: the call's line, then its `detail` sections in order (§ 23.5).
// Nothing here parses a preview — every section arrives with its heading and its style.
void TurnTranscriptView::setToolOutput(const QJsonObject &reply) {
    const toollabel::Label label = toollabel::fromEvent(reply);
    const QString diff = reply.value(QStringLiteral("diff")).toString();
    // More than 12 changed lines: the diff belongs in the diff pane, not in this log (§ 23.6).
    const bool toThePane = label.openType == QStringLiteral("diff") && !diff.isEmpty() && onOpenDiff;
    if (toThePane) onOpenDiff(label.path.isEmpty() ? label.title : label.path, diff);

    m_log->clear();
    QTextCursor cursor(m_log->document());
    // `fill`, when it is valid, is the ground the text sits on: a diff's add/remove lines are the
    // black-or-white ink on the theme's green/red, exactly as the diff pane draws them.
    auto add = [&cursor](const QString &text, const QColor &color, bool bold = false,
                         const QColor &fill = QColor()) {
        QTextCharFormat format;
        format.setForeground(color);
        if (bold) format.setFontWeight(QFont::Bold);
        if (fill.isValid()) format.setBackground(fill);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text, format);
    };
    if (label.valid) {
        add((label.failed() ? QStringLiteral("✗ ") : QStringLiteral("✓ ")) + label.line() + QLatin1Char('\n'),
            label.failed() ? (label.refused ? relay::theme::TextMuted : relay::theme::SyntaxUnknown) : relay::theme::Text, true);
    }
    const QJsonArray sections = reply.value(QStringLiteral("detail")).toArray();
    for (const auto &value : sections) {
        const QJsonObject section = value.toObject();
        const QString style = section.value(QStringLiteral("style")).toString();
        if (toThePane && style == QStringLiteral("diff")) {
            add(QStringLiteral("\ndiff\n"), relay::theme::TextMuted, true);
            add(QStringLiteral("(opened in a diff pane)\n"), relay::theme::TextMuted);
            continue;
        }
        add(QLatin1Char('\n') + section.value(QStringLiteral("heading")).toString() + QLatin1Char('\n'),
            relay::theme::TextMuted, true);
        const QString text = section.value(QStringLiteral("text")).toString();
        if (style == QStringLiteral("diff")) {
            for (const QString &line : text.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1Char('+')) && !line.startsWith(QStringLiteral("+++")))
                    add(line + QLatin1Char('\n'), relay::theme::contrastInk(relay::theme::Success),
                        false, relay::theme::Success);
                else if (line.startsWith(QLatin1Char('-')) && !line.startsWith(QStringLiteral("---")))
                    add(line + QLatin1Char('\n'), relay::theme::contrastInk(relay::theme::Error),
                        false, relay::theme::Error);
                else
                    add(line + QLatin1Char('\n'), relay::theme::TextMuted);
            }
        } else {
            const QColor colour = style == QStringLiteral("error")  ? (label.refused ? relay::theme::TextMuted : relay::theme::SyntaxUnknown)
                                : style == QStringLiteral("code")   ? relay::theme::SyntaxCommand
                                : style == QStringLiteral("text")   ? relay::theme::Text
                                                                    : relay::theme::TextMuted;
            add(text + QLatin1Char('\n'), colour);
        }
        if (section.value(QStringLiteral("truncated")).toBool())
            add(QStringLiteral("(truncated)\n"), relay::theme::TextMuted);
    }
    if (sections.isEmpty()) {
        // A worker from before § 23.5 answers with the text it has and no sections at all.
        const QString text = reply.contains(QStringLiteral("text")) ? reply.value(QStringLiteral("text")).toString()
                                                                    : reply.value(QStringLiteral("preview")).toString();
        add(text.isEmpty() ? QStringLiteral("(No output recorded for this call.)\n") : text + QLatin1Char('\n'),
            relay::theme::TextMuted);
    }
    m_log->moveCursor(QTextCursor::Start);
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
