// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextEdit>

// Ctrl+Shift+Z is a text redo only while a Ctrl+Z undo in this very field is
// still current. A new edit or a move to another field gives the key back to
// the window's restore-closed action.
class TextRedoShortcut final : public QObject {
public:
    explicit TextRedoShortcut(QObject *owner) : QObject(owner) {
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
            if (fieldOf(now) != m_field) clear();
        });
    }

    static QWidget *fieldOf(QWidget *widget) {
        for (QWidget *candidate = widget; candidate; candidate = candidate->parentWidget()) {
            if (auto *line = qobject_cast<QLineEdit *>(candidate); line && !line->isReadOnly()) return line;
            if (auto *plain = qobject_cast<QPlainTextEdit *>(candidate); plain && !plain->isReadOnly()) return plain;
            if (auto *rich = qobject_cast<QTextEdit *>(candidate); rich && !rich->isReadOnly()) return rich;
        }
        return nullptr;
    }

    // Return true when the key belongs to the field. The caller handles
    // closed.restore when this returns false.
    bool handle(QWidget *widget, QKeyEvent *key, const QString &action, QEvent::Type type) {
        QWidget *field = fieldOf(widget);
        if (!field) return false;
        const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
        const bool undoKey = key->key() == Qt::Key_Z && modifiers == Qt::ControlModifier;
        const bool redoKey = key->key() == Qt::Key_Z && modifiers == (Qt::ControlModifier | Qt::ShiftModifier);
        if (undoKey && action.isEmpty()) {
            key->accept();
            if (type == QEvent::KeyPress && !key->isAutoRepeat()) {
                clear();
                if (canUndo(field)) {
                    undo(field);
                    if (canRedo(field)) arm(field);
                }
            }
            return true;
        }
        if (!redoKey || (!action.isEmpty() && action != QStringLiteral("closed.restore"))) return false;
        if (field == m_field && canRedo(field)) {
            key->accept();
            if (type == QEvent::KeyPress && !key->isAutoRepeat()) {
                m_redoing = true;
                redo(field);
                m_redoing = false;
                if (!canRedo(field)) clear();
            }
            return true;
        }
        clear();
        // An unbound Ctrl+Shift+Z must not reach Qt's older native redo stack
        // after the field lost focus or received another edit.
        if (action.isEmpty()) { key->accept(); return true; }
        return false;
    }

private:
    static bool canUndo(QWidget *field) {
        if (auto *line = qobject_cast<QLineEdit *>(field)) return line->isUndoAvailable();
        if (auto *plain = qobject_cast<QPlainTextEdit *>(field)) return plain->document()->isUndoAvailable();
        return static_cast<QTextEdit *>(field)->document()->isUndoAvailable();
    }
    static bool canRedo(QWidget *field) {
        if (auto *line = qobject_cast<QLineEdit *>(field)) return line->isRedoAvailable();
        if (auto *plain = qobject_cast<QPlainTextEdit *>(field)) return plain->document()->isRedoAvailable();
        return static_cast<QTextEdit *>(field)->document()->isRedoAvailable();
    }
    static void undo(QWidget *field) {
        if (auto *line = qobject_cast<QLineEdit *>(field)) line->undo();
        else if (auto *plain = qobject_cast<QPlainTextEdit *>(field)) plain->undo();
        else static_cast<QTextEdit *>(field)->undo();
    }
    static void redo(QWidget *field) {
        if (auto *line = qobject_cast<QLineEdit *>(field)) line->redo();
        else if (auto *plain = qobject_cast<QPlainTextEdit *>(field)) plain->redo();
        else static_cast<QTextEdit *>(field)->redo();
    }
    void clear() {
        disconnect(m_editConnection);
        m_editConnection = {};
        m_field.clear();
    }
    void arm(QWidget *field) {
        m_field = field;
        if (auto *line = qobject_cast<QLineEdit *>(field))
            m_editConnection = connect(line, &QLineEdit::textChanged, this, [this] { if (!m_redoing) clear(); });
        else if (auto *plain = qobject_cast<QPlainTextEdit *>(field))
            m_editConnection = connect(plain, &QPlainTextEdit::textChanged, this, [this] { if (!m_redoing) clear(); });
        else if (auto *rich = qobject_cast<QTextEdit *>(field))
            m_editConnection = connect(rich, &QTextEdit::textChanged, this, [this] { if (!m_redoing) clear(); });
    }

    QPointer<QWidget> m_field;
    QMetaObject::Connection m_editConnection;
    bool m_redoing = false;
};
