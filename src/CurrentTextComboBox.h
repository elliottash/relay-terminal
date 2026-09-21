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
#include <QStylePainter>
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
    QSize sizeHint() const override { return hintFor(displayText()); }

    // ----- the collapsed chip says something else than the row (card #MDL1, design 5.3) -------
    // "The collapsed box is **the model alone**", on every mode (owner, 2026-09-21: "no need to
    // show the model class in the pane header"), while the row it sits on carries the "via" column
    // and sits under its class's header. This class exists precisely so the two can differ: the
    // override is what the chip draws, what it is measured for, and what the phone is told
    // (Pane::remoteState). Empty puts the current row's text back.
    void setCollapsedText(const QString &text) {
        if (m_collapsed == text) return;
        m_collapsed = text;
        updateGeometry();
        update();
    }
    QString displayText() const { return m_collapsed.isEmpty() ? currentText() : m_collapsed; }
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

    // The rows the list drops open with, when they are not simply the combo's own (the model box:
    // its rows carry class headers, and Right rebuilds them while the list is open; card #MDL1,
    // design 5.3). `current` is an index into the list that comes back. Unset — which it is for
    // the level box and every other box — the list is the combo's own model, exactly as before.
    // A pick is then answered by the row's `data` through `onPickedData`, because an index into
    // that list is not an index into the combo.
    std::function<QList<relay::FilterRow>(int *current)> onRows;
    std::function<void(const QString &data)> onPickedData;
    // Left / Right on a row: see relay::FilterPopup::onExpandKey. The owner answers by calling
    // `replaceRows` and returning true.
    std::function<bool(const QString &group, int delta)> onExpandKey;
    // The open list's rows again, from inside `onExpandKey`. The popup keeps the filter line and
    // puts the highlight back on the row it was on.
    void replaceRows(const QList<relay::FilterRow> &rows, int current) {
        if (m_popup != nullptr) m_popup->setRows(rows, current);
    }

    void showPopup() override {
        if (onBeforePopup) onBeforePopup();
        if (onRows) {
            int current = -1;
            const QList<relay::FilterRow> rows = onRows(&current);
            if (!rows.isEmpty()) {
                filterPopup()->setRows(rows, current);
                filterPopup()->openFor(this);
                return;
            }
        }
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
    // The chip's text, when it is not the current row's (see setCollapsedText). QComboBox paints
    // QStyleOptionComboBox::currentText, so that is the one field to say it in.
    void paintEvent(QPaintEvent *) override {
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        option.currentText = displayText();
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.drawControl(QStyle::CE_ComboBoxLabel, option);
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
            row.trailing = itemData(i, relay::kTrailingItemRole).toString();
            row.group = itemData(i, relay::kGroupItemRole).toString();
            row.header = itemData(i, relay::kHeaderItemRole).toBool();
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
            if (index < 0 || index >= m_popup->rows().size()) return;
            if (onPickedData) { onPickedData(m_popup->rows().at(index).data); return; }
            if (index >= count()) return;
            setCurrentIndex(index);
            emit activated(index);
        };
        m_popup->onExpandKey = [this](const QString &group, int delta) {
            return onExpandKey ? onExpandKey(group, delta) : false;
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

    QString m_collapsed;
    relay::FilterPopup *m_popup = nullptr;
    QPointer<QWidget> m_focusBefore;
    Qt::FocusReason m_lastReason = Qt::OtherFocusReason;
};
