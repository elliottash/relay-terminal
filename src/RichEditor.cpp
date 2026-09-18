// SPDX-License-Identifier: GPL-3.0-or-later
#include "RichEditor.h"

#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QDropEvent>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPaintEvent>
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
    updatePlaceholder();
    setMinimumHeight(64);
    setMaximumHeight(260);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    setUndoRedoEnabled(true);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    new ShellHighlighter(document());
    connect(document(), &QTextDocument::blockCountChanged, this, [this] {
        setFixedHeight(std::clamp(document()->blockCount() * fontMetrics().lineSpacing() + 30, 64, 260));
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

void RichEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    updatePlaceholder();
}

// Longest hint that fits on one line; a narrow pane loses the help note first, then words.
void RichEditor::setPlaceholders(const QStringList &candidates) {
    m_placeholders = candidates;
    updatePlaceholder();
}

void RichEditor::updatePlaceholder() {
    static const QStringList composer{
        QStringLiteral("Shell commands or agent prompts…      ?  for help"),
        QStringLiteral("Shell commands or agent prompts…"),
        QStringLiteral("Commands or prompts…"),
        QStringLiteral("Commands…"),
        QStringLiteral("…"),
    };
    const QStringList &candidates = m_placeholders.isEmpty() ? composer : m_placeholders;
    const QFontMetrics metrics(font());
    const int available = viewport()->width() - int(document()->documentMargin()) * 2 - 8;
    for (const QString &text : candidates) {
        if (metrics.horizontalAdvance(text) <= available || text == candidates.last()) {
            if (placeholderText() != text) setPlaceholderText(text);
            return;
        }
    }
}

void RichEditor::setAutoHeight(int minLines, int maxLines) {
    m_minLines = std::max(1, minLines);
    m_maxLines = std::max(m_minLines, maxLines);
    auto apply = [this] {
        const QFontMetrics metrics(font());
        const int line = std::max(14, metrics.lineSpacing());
        const int lines = std::clamp(int(std::ceil(document()->size().height())), m_minLines, m_maxLines);
        const int chrome = int(document()->documentMargin()) * 2 + frameWidth() * 2 + 4;
        setFixedHeight(lines * line + chrome);
    };
    connect(document(), &QTextDocument::contentsChanged, this, apply);
    connect(this, &QPlainTextEdit::textChanged, this, apply);
    apply();
}

void RichEditor::setGhost(const QString &remainder) {
    if (remainder == m_ghost) return;
    m_ghost = remainder;
    viewport()->update();
}

bool RichEditor::acceptGhost(bool wholeSuggestion) {
    if (m_ghost.isEmpty()) return false;
    QString take = m_ghost;
    if (!wholeSuggestion) {
        // One word: leading spaces plus the following run of non-space characters.
        int i = 0;
        while (i < take.size() && take.at(i).isSpace()) ++i;
        while (i < take.size() && !take.at(i).isSpace()) ++i;
        take = take.left(std::max(1, i));
    }
    const QString rest = m_ghost.mid(take.size());
    moveCursor(QTextCursor::End);
    insertPlainText(take);
    setGhost(rest);
    return true;
}

void RichEditor::setCaretColor(const QColor &color) {
    m_caret = color;
    // Hide Qt's own caret and blink ours at the desktop's rate.
    setCursorWidth(m_caret.isValid() ? 0 : 1);
    if (m_caret.isValid()) {
        if (!m_caretBlink) {
            m_caretBlink = new QTimer(this);
            const int flash = QApplication::cursorFlashTime();
            m_caretBlink->setInterval(std::max(200, flash > 0 ? flash / 2 : 500));
            connect(m_caretBlink, &QTimer::timeout, this, [this] { m_caretOn = !m_caretOn; viewport()->update(); });
        }
        m_caretOn = true;
        m_caretBlink->start();
    } else if (m_caretBlink) {
        m_caretBlink->stop();
    }
    viewport()->update();
}

void RichEditor::paintEvent(QPaintEvent *event) {
    QPlainTextEdit::paintEvent(event);
    if (m_caret.isValid() && m_caretOn && hasFocus() && !isReadOnly() && !textCursor().hasSelection()) {
        QPainter caret(viewport());
        QRect rect = cursorRect();
        rect.setWidth(2);
        caret.fillRect(rect, m_caret);
    }
    if (m_ghost.isEmpty() || textCursor().hasSelection() || !textCursor().atEnd() || m_preedit) return;
    QPainter painter(viewport());
    QColor color = palette().color(QPalette::Text);
    color.setAlphaF(0.38);
    painter.setPen(color);
    painter.setFont(font());
    const QRect cursor = cursorRect();
    const QRect area(cursor.right() + 1, cursor.top(), viewport()->width() - cursor.right() - 4, cursor.height());
    painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     fontMetrics().elidedText(m_ghost, Qt::ElideRight, std::max(0, area.width())));
}

void RichEditor::keyPressEvent(QKeyEvent *event) {
    if (isReadOnly()) { QPlainTextEdit::keyPressEvent(event); return; }
    // Numpad Enter arrives as Qt::Key_Enter with Qt::KeypadModifier set; drop that flag so
    // keypad Enter takes the same branches as Return, including Ctrl and Ctrl+Shift.
    auto mods = event->modifiers();
    mods.setFlag(Qt::KeypadModifier, false);
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
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) { copy(); return; }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V) { paste(); return; }
    // History: Up on the first line and Down on the last line, like a shell prompt.
    // Alt+arrows are reserved for moving between panes.
    const bool up = event->key() == Qt::Key_Up, down = event->key() == Qt::Key_Down;
    if (mods == Qt::NoModifier && (up || down) && !textCursor().hasSelection() && !m_history.isEmpty()
        && ((up && textCursor().blockNumber() == 0) || (down && textCursor().blockNumber() == document()->blockCount() - 1))
        && !(down && m_historyIndex == m_history.size())) {
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
    // An image pasted or dropped here is attached, not pasted as text: the pane writes it out and
    // hands back `@path` tokens (issue EM1E). Text is unaffected.
    if (onImageMime) {
        const QStringList tokens = onImageMime(source, m_dropping);
        if (!tokens.isEmpty()) { insertAttachments(tokens); return; }
    }
    // Pasting never submits, and HTML formatting is never interpreted as commands.
    const QString text = source->text();
    if (text.toUtf8().size() + toPlainText().toUtf8().size() > 131072) {
        QApplication::beep();
        return;
    }
    insertPlainText(text);
}

bool RichEditor::canInsertFromMimeData(const QMimeData *source) const {
    // A drop of image files may carry no text at all, so the base class would refuse it.
    if (onImageMime && source && (source->hasImage() || source->hasUrls())) return true;
    return QPlainTextEdit::canInsertFromMimeData(source);
}

void RichEditor::dropEvent(QDropEvent *event) {
    // Remembered for insertFromMimeData, which cannot otherwise tell a drop from a paste — and the
    // two get different hints (a drop is told about the paste shortcut).
    m_dropping = true;
    QPlainTextEdit::dropEvent(event);
    m_dropping = false;
}

void RichEditor::insertAttachments(const QStringList &tokens) {
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    const QString before = toPlainText();
    QString text;
    if (!before.isEmpty() && !before.endsWith(QLatin1Char(' ')) && !before.endsWith(QLatin1Char('\n')))
        text += QLatin1Char(' ');
    text += tokens.join(QLatin1Char(' ')) + QLatin1Char(' ');
    cursor.insertText(text);
    setTextCursor(cursor);
}
