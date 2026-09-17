// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"
#include <QApplication>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPalette>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <algorithm>

namespace {
class ShellHighlighter final : public QSyntaxHighlighter {
public:
    explicit ShellHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}
protected:
    void highlightBlock(const QString &text) override {
        struct Rule { QString expression; QColor color; };
        const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
        const QList<Rule> rules{
            {QStringLiteral(R"(\b(cd|git|ls|echo|printf|cat|find|grep|rg|for|do|done|if|then|fi|else|export|sudo)\b)"), QColor(dark ? "#83bbff" : "#1f5ba8")},
            {QStringLiteral(R"(\$[A-Za-z_][A-Za-z0-9_]*|\$\{[^}]*\})"), QColor(dark ? "#d9b4ff" : "#7841a3")},
            {QStringLiteral(R"([|&;<>]+)"), QColor(dark ? "#e4b978" : "#805400")},
            {QStringLiteral(R"("([^"\\]|\\.)*"|'[^']*')"), QColor(dark ? "#a8d6a2" : "#287044")},
            {QStringLiteral(R"((?:^|\s)#[^\n]*)"), QColor(dark ? "#929bab" : "#657387")}
        };
        for (const auto &rule : rules) {
            auto matches = QRegularExpression(rule.expression).globalMatch(text);
            while (matches.hasNext()) {
                const auto match = matches.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.color);
            }
        }
    }
};
}

RichEditor::RichEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setObjectName(QStringLiteral("composerEditor"));
    setAccessibleName(QStringLiteral("Relay command and agent input"));
    setAccessibleDescription(QStringLiteral("Multiline editor. Enter submits; Shift Enter inserts a newline. Control Enter forces Agent; Control Shift Enter forces Terminal."));
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setPlaceholderText(QStringLiteral("Type a command, or describe what you need…"));
    setMinimumHeight(100);
    setMaximumHeight(260);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setUndoRedoEnabled(true);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    new ShellHighlighter(document());
    connect(document(), &QTextDocument::blockCountChanged, this, [this] {
        setFixedHeight(std::clamp(document()->blockCount() * fontMetrics().lineSpacing() + 30, 100, 260));
    });
}

void RichEditor::remember(const QString &text) {
    if (!text.isEmpty() && (m_history.isEmpty() || m_history.last() != text)) {
        m_history.append(text);
        if (m_history.size() > 200) m_history.removeFirst();
    }
    m_historyIndex = m_history.size();
    m_draft.clear();
}

void RichEditor::keyPressEvent(QKeyEvent *event) {
    if (isReadOnly()) { QPlainTextEdit::keyPressEvent(event); return; }
    const auto mods = event->modifiers();
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !m_preedit) {
        if (mods == Qt::ShiftModifier) {
            insertPlainText(QStringLiteral("\n"));
        } else if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (onSubmit) onSubmit(QStringLiteral("shell"));
        } else if (mods == Qt::ControlModifier) {
            if (onSubmit) onSubmit(QStringLiteral("agent"));
        } else if (mods == Qt::NoModifier) {
            if (onSubmit) onSubmit(QStringLiteral("auto"));
        } else {
            QPlainTextEdit::keyPressEvent(event);
        }
        return;
    }
    if (event->key() == Qt::Key_Escape && mods == Qt::NoModifier && !m_preedit) {
        if (onNative) onNative();
        return;
    }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) { copy(); return; }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V) { paste(); return; }
    if (mods == Qt::AltModifier && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        if (m_historyIndex == m_history.size()) m_draft = toPlainText();
        m_historyIndex = std::clamp(m_historyIndex + (event->key() == Qt::Key_Up ? -1 : 1), 0, int(m_history.size()));
        setPlainText(m_historyIndex == m_history.size() ? m_draft : m_history[m_historyIndex]);
        moveCursor(QTextCursor::End);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void RichEditor::inputMethodEvent(QInputMethodEvent *event) {
    m_preedit = !event->preeditString().isEmpty();
    QPlainTextEdit::inputMethodEvent(event);
}

void RichEditor::insertFromMimeData(const QMimeData *source) {
    // Pasting never submits, and HTML formatting is never interpreted as commands.
    const QString text = source->text();
    if (text.toUtf8().size() + toPlainText().toUtf8().size() > 131072) {
        QApplication::beep();
        return;
    }
    insertPlainText(text);
}
