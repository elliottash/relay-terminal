// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A combo box that hugs the *current* row's text (owner, 2026-09-19: "shrink the model selector
// to the text length of the selected model; when uncollapsed the list can get wider"). The
// default AdjustToContents sizes the collapsed box to the widest row, which is width the strip
// never has; here the collapsed chip is no wider than the model it names, and the open list
// grows to its widest row instead, which is where the width is wanted.
//
// Shared by the terminal pane's model box (Pane) and the Switchboard pane's (#BRD3).
// Deliberately at global scope, where Pane.h defined it before it moved here: Pane is itself
// a global-scope class, and its unqualified `new CurrentTextComboBox` must keep finding it.
#include <QAbstractItemView>
#include <QComboBox>
#include <QScrollBar>
#include <QStyleOptionComboBox>
#include <QStyle>

#include <algorithm>
#include <functional>

class CurrentTextComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;
    QSize sizeHint() const override { return hintFor(currentText()); }
    // A squeeze clips the text, as it always has in a narrow pane: the strip's Ignored policy
    // keeps the box off the pane's own minimum width (#G152).
    QSize minimumSizeHint() const override { return hintFor(QStringLiteral("MM")); }
    // Called just before the list is drawn, so a box whose rows depend on something that may
    // still be arriving can put them right first (#PK5Q: a helper panel's box borrows the
    // catalog from a terminal pane until its own worker has spoken). Refilling here is safe —
    // nothing is shown yet.
    std::function<void()> onBeforePopup;
    void showPopup() override {
        if (onBeforePopup) onBeforePopup();
        view()->setMinimumWidth(std::max(width(), view()->sizeHintForColumn(0)
            + 2 * view()->frameWidth() + view()->verticalScrollBar()->sizeHint().width()));
        QComboBox::showPopup();
    }
private:
    // The size the style wants for a combo showing `text`: the content plus the frame, the
    // stylesheet's padding and the drop-down arrow it draws around the text.
    QSize hintFor(const QString &text) const {
        const_cast<CurrentTextComboBox *>(this)->ensurePolished();
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QSize content(fontMetrics().horizontalAdvance(text) + 2, fontMetrics().height());
        return style()->sizeFromContents(QStyle::CT_ComboBox, &option, content, this);
    }
};
