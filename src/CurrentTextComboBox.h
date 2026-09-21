// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A combo box that hugs the *current* row's text (owner, 2026-09-19: "shrink the model selector
// to the text length of the selected model; when uncollapsed the list can get wider"). The
// default AdjustToContents sizes the collapsed box to the widest row, which is width the strip
// never has; here the collapsed chip is no wider than the model it names, and the open list
// grows to its widest row instead, which is where the width is wanted.
//
// Since 2026-09-21 the list it drops open is relay::FilterPopup, not QComboBox's own: the
// current row is highlighted where Qt's was invisible under the Fusion style Relay forces, and
// what you type filters the rows instead of jumping between them (owner: "when you use alt+m or
// alt+e, your current selection should be highlighted. then you should be able to select with
// up/down arrows, and also filter with text typing (like warp's model picker)"). The rows are
// read out of the combo's own model — text, data, tooltips, separators, disabled state — so
// nothing that fills a box has to know the list is drawn by something else, and `activated` is
// emitted for a pick exactly as before.
//
// Shared by the terminal pane's model box (Pane) and the Switchboard pane's (#BRD3).
// Deliberately at global scope, where Pane.h defined it before it moved here: Pane is itself
// a global-scope class, and its unqualified `new CurrentTextComboBox` must keep finding it.
#include "FilterPopup.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QScrollBar>
#include <QStyleOptionComboBox>
#include <QStyle>

#include <algorithm>
#include <functional>

class CurrentTextComboBox final : public QComboBox {
public:
    explicit CurrentTextComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        // Alt+M focuses the box before it opens the list (Pane::openModelBox), so the widget that
        // had the caret is remembered here — Escape then puts it back, rather than leaving the
        // next keystroke to a combo box nobody is looking at.
        // Only a shortcut's focus is remembered, and it is remembered until it is used: closing
        // the list hands focus back to this box first, and reading *that* focus change as "the
        // user came here on purpose" was what left the caret in the combo after Escape.
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
            if (now == this && m_lastReason == Qt::ShortcutFocusReason && old != nullptr) m_focusBefore = old;
        });
    }
    QSize sizeHint() const override { return hintFor(currentText()); }
    // A squeeze clips the text, as it always has in a narrow pane: the strip's Ignored policy
    // keeps the box off the pane's own minimum width (#G152).
    QSize minimumSizeHint() const override { return hintFor(QStringLiteral("MM")); }
    // Called just before the list is drawn, so a box whose rows depend on something that may
    // still be arriving can put them right first (#PK5Q: a helper panel's box borrows the
    // catalog from a terminal pane until its own worker has spoken). Refilling here is safe —
    // nothing is shown yet.
    std::function<void()> onBeforePopup;
    // A chord pressed while the list is open. Unset — which it is everywhere today — the key is
    // offered to the event filters sitting on view() instead, which is where Pane installs its
    // PopupHotkeys: Alt+M closes the box, any other chord closes it and goes on to the window.
    std::function<bool(QKeyEvent *)> onChordKey;

    void showPopup() override {
        if (onBeforePopup) onBeforePopup();
        if (count() == 0) return;
        filterPopup()->setRows(rowsFromModel(), currentIndex());
        filterPopup()->openFor(this);
    }
    void hidePopup() override {
        if (m_popup != nullptr && m_popup->isVisible()) m_popup->dismiss();
        QComboBox::hidePopup();
    }

protected:
    void focusInEvent(QFocusEvent *event) override {
        m_lastReason = event->reason();
        QComboBox::focusInEvent(event);
    }

private:
    // Whatever the box holds, as the popup wants it. Qt marks a separator by its accessible
    // description, and a row switched off by its flags; both are carried through so the list can
    // step over them.
    QList<relay::FilterRow> rowsFromModel() const {
        QList<relay::FilterRow> rows;
        rows.reserve(count());
        for (int i = 0; i < count(); ++i) {
            relay::FilterRow row;
            row.text = itemText(i);
            row.data = itemData(i).toString();
            row.tooltip = itemData(i, Qt::ToolTipRole).toString();
            row.separator = itemData(i, Qt::AccessibleDescriptionRole).toString()
                            == QLatin1String("separator");
            row.enabled = model()->index(i, modelColumn(), rootModelIndex()).flags().testFlag(Qt::ItemIsEnabled)
                          && !row.separator;
            rows << row;
        }
        return rows;
    }

    relay::FilterPopup *filterPopup() {
        if (m_popup != nullptr) return m_popup;
        m_popup = new relay::FilterPopup(this);
        m_popup->onPicked = [this](int index) {
            restoreFocus();
            if (index < 0 || index >= count()) return;
            setCurrentIndex(index);
            emit activated(index);
        };
        m_popup->onCancelled = [this] { restoreFocus(); };
        m_popup->onChordKey = [this](QKeyEvent *key) {
            if (onChordKey) return onChordKey(key);
            // No hook set: offer it to view()'s filters, which is where the pane's chord handling
            // has lived since 2026-09-20 (Pane::passHotkeysThrough). Such a filter answers by
            // calling hidePopup() on this box, so "the list is no longer open" is the reliable
            // signal that it took the key — sendEvent's own answer is not, because the view's
            // type-ahead accepts any chord that carries text.
            QKeyEvent copy(QEvent::KeyPress, key->key(), key->modifiers(), key->text());
            QCoreApplication::sendEvent(view(), &copy);
            return m_popup == nullptr || !m_popup->isVisible();
        };
        return m_popup;
    }

    void restoreFocus() {
        if (m_focusBefore.isNull() || !m_focusBefore->isVisible() || !m_focusBefore->isEnabled()) return;
        m_focusBefore->setFocus(Qt::OtherFocusReason);
        m_focusBefore.clear();
    }

    // The size the style wants for a combo showing `text`: the content plus the frame, the
    // stylesheet's padding and the drop-down arrow it draws around the text.
    QSize hintFor(const QString &text) const {
        const_cast<CurrentTextComboBox *>(this)->ensurePolished();
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QSize content(fontMetrics().horizontalAdvance(text) + 2, fontMetrics().height());
        return style()->sizeFromContents(QStyle::CT_ComboBox, &option, content, this);
    }

    relay::FilterPopup *m_popup = nullptr;
    QPointer<QWidget> m_focusBefore;
    Qt::FocusReason m_lastReason = Qt::OtherFocusReason;
};
