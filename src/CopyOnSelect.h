#pragma once
// Copy on highlight, for every read-only text surface Relay shows.
//
// The terminal has had this since the engine landed: TerminalView puts a mouse selection on
// PRIMARY when the button is released, and Pane's event filter puts the same text on the
// clipboard and toasts the count. The owner asked for the rest of the panes to behave the same
// way ("allow copy on highlight in info and other panes"), so the behaviour lives here once and
// every pane installs it rather than growing its own event filter.
//
// The setting is still `terminal/copy_on_select` in QSettings. The key is deliberately unchanged
// even though the feature is no longer terminal-only -- renaming it would silently turn the
// setting off for everybody who had switched it on. Only the label and description in the
// settings pane were reworded.
//
// Header-only on purpose: the surfaces live in a dozen static libraries (relay-filepanes,
// relay-board, relay-conversations, ...) plus the header-only Pane, and a library of one small
// filter would have to be linked into all of them.

#include <QAbstractScrollArea>
#include <QChar>
#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QMouseEvent>
#include <QObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <utility>

namespace relay {

// Off by default, as the terminal's has always been.
inline bool copyOnSelectEnabled()
{
    return QSettings().value(QStringLiteral("terminal/copy_on_select"), false).toBool();
}

// What is highlighted in one read-only text surface, or an empty string when nothing is -- or
// when the widget is editable. An editable field's selection is never copied on highlight: the
// prompt box, the composer and the settings inputs are where the user is typing, and taking
// their clipboard while they select something to overtype would lose what they had copied.
inline QString copyOnSelectText(const QWidget *widget)
{
    const auto plain = [](const QString &text) {
        // QTextDocument separates blocks with U+2029, which is not what anybody wants to paste.
        return QString(text).replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    };
    if (const auto *edit = qobject_cast<const QTextEdit *>(widget))   // QTextBrowser is one of these
        return edit->isReadOnly() ? plain(edit->textCursor().selectedText()) : QString();
    if (const auto *edit = qobject_cast<const QPlainTextEdit *>(widget))
        return edit->isReadOnly() ? plain(edit->textCursor().selectedText()) : QString();
    if (const auto *edit = qobject_cast<const QLineEdit *>(widget))
        return edit->isReadOnly() ? edit->selectedText() : QString();
    if (const auto *label = qobject_cast<const QLabel *>(widget))
        return label->selectedText();   // a QLabel is never an input
    return QString();
}

// To PRIMARY where the platform has one and to the clipboard, which is what the terminal does
// between TerminalView::mouseReleaseEvent (PRIMARY) and Pane::copySelection (clipboard).
inline void copyOnSelectPut(const QString &text)
{
    if (text.isEmpty())
        return;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;
    if (clipboard->supportsSelection())
        clipboard->setText(text, QClipboard::Selection);
    clipboard->setText(text, QClipboard::Clipboard);
}

// Copies a read-only widget's selection when the left button is released over it. Parented to
// the widget it watches, so it dies with the pane.
//
// No Q_OBJECT: nothing in this tree has one, and the filter neither declares signals nor is
// looked up by type (installCopyOnSelect finds it by object name).
class CopyOnSelectFilter : public QObject
{
public:
    static QString markerName() { return QStringLiteral("relayCopyOnSelect"); }

    CopyOnSelectFilter(QWidget *target, std::function<void(const QString &)> notify)
        : QObject(target), m_target(target), m_notify(std::move(notify))
    {
        setObjectName(markerName());
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
            && copyOnSelectEnabled() && m_target) {
            // After the widget itself has handled the release, as the terminal's filter does:
            // this runs before the text view has settled its cursor.
            QPointer<QWidget> target = m_target;
            std::function<void(const QString &)> notify = m_notify;
            QTimer::singleShot(0, target, [target, notify] {
                if (!target)
                    return;
                const QString text = copyOnSelectText(target);
                if (text.isEmpty())
                    return;
                copyOnSelectPut(text);
                if (notify)
                    notify(text);
            });
        }
        return QObject::eventFilter(object, event);
    }

private:
    QPointer<QWidget> m_target;
    std::function<void(const QString &)> m_notify;
};

// Make highlighting text in `widget` copy it, when the setting is on. Safe to call twice.
//
// `notify` is for a surface that says so out loud -- the pane toasts the character count. Most
// panes have nowhere to say it and pass nothing, which is also what the terminal does on PRIMARY.
inline void installCopyOnSelect(QWidget *widget, std::function<void(const QString &)> notify = {})
{
    if (!widget)
        return;
    if (widget->findChild<QObject *>(CopyOnSelectFilter::markerName(), Qt::FindDirectChildrenOnly))
        return;
    auto *filter = new CopyOnSelectFilter(widget, std::move(notify));
    // A scroll area never sees the mouse itself; its viewport does. A label does.
    if (auto *area = qobject_cast<QAbstractScrollArea *>(widget))
        area->viewport()->installEventFilter(filter);
    else
        widget->installEventFilter(filter);
}

}   // namespace relay
