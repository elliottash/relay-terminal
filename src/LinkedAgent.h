// SPDX-License-Identifier: AGPL-3.0-or-later
// An artifact's agent popped out into a linked shell pane (card #2FQ9).
//
// A card page or a file editor embeds an agent console (`RelayWindow::createAgentConsole`). Popped
// out, that same console — its conversation, its tab worker, its context — moves into a leaf of
// its own beside the artifact and gains a shell; docked back, it returns to the slot it came from
// and the shell stops. Two widgets make that visible, and neither knows what a `Pane` is:
//
//   - `LinkedAgentView` is what the linked leaf (`ToolPane::Kind::LinkedAgent`) holds: a one-line
//     bar that says whose agent this is with a "Dock back" button, and the console under it.
//   - `LinkedAgentPlaceholder` stands in the console's slot in its host meanwhile, with the same
//     button and a "Show" that goes to the linked leaf.
//
// The view also remembers the slot, so the console always has a way home: `putBack()` puts it in
// the placeholder's place, and the view's destructor does it too — a tab closed with both leaves
// in it, or the linked leaf deleted by anything other than Dock back, never deletes a console
// its host still holds a handle to.
#pragma once

#include "PaneView.h"

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace relay {

// The box layout under `root` (nested layouts included) that holds `child` directly, and where.
inline QBoxLayout *boxLayoutHolding(QLayout *root, QWidget *child, int *index) {
    if (!root) return nullptr;
    for (int i = 0; i < root->count(); ++i) {
        QLayoutItem *item = root->itemAt(i);
        if (item->widget() == child) {
            auto *box = qobject_cast<QBoxLayout *>(root);
            if (box && index) *index = i;
            return box;
        }
        if (QBoxLayout *inner = boxLayoutHolding(item->layout(), child, index)) return inner;
    }
    return nullptr;
}

class LinkedAgentPlaceholder final : public QWidget {
public:
    explicit LinkedAgentPlaceholder(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("linkedAgentPlaceholder"));
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 6, 10, 6);
        row->setSpacing(8);
        m_text = new QLabel(QStringLiteral("✦ The agent is in its linked shell pane"), this);
        m_text->setObjectName(QStringLiteral("linkedAgentPlaceholderText"));
        row->addWidget(m_text, 1);
        m_show = new QToolButton(this);
        m_show->setObjectName(QStringLiteral("linkedAgentShow"));
        m_show->setText(QStringLiteral("Show"));
        m_show->setToolTip(QStringLiteral("Go to the linked pane"));
        m_show->setCursor(Qt::PointingHandCursor);
        m_show->setFocusPolicy(Qt::NoFocus);
        row->addWidget(m_show, 0);
        m_dock = new QToolButton(this);
        m_dock->setObjectName(QStringLiteral("linkedAgentDockBack"));
        m_dock->setText(QStringLiteral("⤵ Dock back"));
        m_dock->setToolTip(QStringLiteral("Bring the agent back here and stop its shell. The conversation is kept."));
        m_dock->setCursor(Qt::PointingHandCursor);
        m_dock->setFocusPolicy(Qt::NoFocus);
        row->addWidget(m_dock, 0);
        QObject::connect(m_show, &QToolButton::clicked, this, [this] { if (onShow) onShow(); });
        QObject::connect(m_dock, &QToolButton::clicked, this, [this] { if (onDockBack) onDockBack(); });
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    std::function<void()> onShow;
    std::function<void()> onDockBack;

private:
    QLabel *m_text = nullptr;
    QToolButton *m_show = nullptr, *m_dock = nullptr;
};

class LinkedAgentView final : public QWidget, public PaneView {
public:
    // Where the console came from, so it can go back: the placeholder that holds its place and
    // what the host had set on it (a card page caps its console's height; a leaf must not).
    struct Slot {
        QPointer<QWidget> placeholder;
        int stretch = 0;
        QSize minimum, maximum;
        QSizePolicy policy;
    };

    LinkedAgentView(QWidget *console, QWidget *host, const QString &title, const Slot &slot)
        : m_console(console), m_host(host), m_slot(slot), m_titleText(title) {
        setObjectName(QStringLiteral("linkedAgentView"));
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        auto *bar = new QWidget(this);
        bar->setObjectName(QStringLiteral("linkedAgentBar"));
        auto *row = new QHBoxLayout(bar);
        m_row = row;
        row->setContentsMargins(10, 4, 10, 4);
        row->setSpacing(8);
        m_title = new QLabel(bar);
        m_title->setObjectName(QStringLiteral("linkedAgentTitle"));
        m_title->setTextFormat(Qt::PlainText);
        // Clipped rather than pushing Dock back off the bar when the pane is narrow.
        m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_title->setToolTip(QStringLiteral("The artifact's own agent, with a shell. A line starting with ! (or Terminal "
                                           "mode on the chip) runs in the shell; everything else goes to the agent, "
                                           "exactly as it did in the artifact."));
        row->addWidget(m_title, 1);
        m_dock = new QToolButton(bar);
        m_dock->setObjectName(QStringLiteral("linkedAgentDockBack"));
        m_dock->setText(QStringLiteral("⤵ Dock back"));
        m_dock->setToolTip(QStringLiteral("Put the agent back in its artifact and stop this shell. The conversation is kept."));
        m_dock->setCursor(Qt::PointingHandCursor);
        m_dock->setFocusPolicy(Qt::NoFocus);
        row->addWidget(m_dock, 0);
        column->addWidget(bar, 0);
        if (console) {
            console->setMinimumSize(0, 0);
            console->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            console->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            column->addWidget(console, 1);
        }
        QObject::connect(m_dock, &QToolButton::clicked, this, [this] { if (onDockBack) onDockBack(); });
        refreshTitle();
    }
    ~LinkedAgentView() override { putBack(); }

    QWidget *console() const { return m_console; }
    QWidget *host() const { return m_host; }
    QWidget *placeholder() const { return m_slot.placeholder; }
    // True while the console is still in this view (not yet put back, not deleted).
    bool holdsConsole() const { return m_console && m_console->parentWidget() == this; }

    // The console back into its host's slot, in the placeholder's place; the placeholder goes.
    // With no placeholder left (the host is gone) the console stays where it is: whoever deletes
    // this view deletes it, and the host that owned its context went first.
    void putBack() {
        QWidget *console = m_console;
        QWidget *placeholder = m_slot.placeholder;
        if (!console || !placeholder || console->parentWidget() != this) return;
        QWidget *slotParent = placeholder->parentWidget();
        if (!slotParent) return;
        if (auto *splitter = qobject_cast<QSplitter *>(slotParent)) {
            const QList<int> sizes = splitter->sizes();
            splitter->replaceWidget(splitter->indexOf(placeholder), console);
            splitter->setSizes(sizes);
        } else {
            int index = -1;
            QBoxLayout *box = boxLayoutHolding(slotParent->layout(), placeholder, &index);
            if (!box || index < 0) return;
            box->insertWidget(index, console, m_slot.stretch);
            box->removeWidget(placeholder);
        }
        console->setMinimumSize(m_slot.minimum);
        console->setMaximumSize(m_slot.maximum);
        console->setSizePolicy(m_slot.policy);
        console->show();
        // The placeholder's end deletes the console while the agent is out (the host is going);
        // it is not going because the host is, so that hook goes first.
        placeholder->disconnect(console);
        placeholder->hide();
        placeholder->deleteLater();
        m_slot.placeholder = nullptr;
    }

    void setTitle(const QString &title) { m_titleText = title; refreshTitle(); }

    QString paneTitle() const override { return m_titleText.isEmpty() ? QStringLiteral("Agent + shell") : m_titleText + QStringLiteral(" · agent + shell"); }
    void focusView() override { if (onFocus) onFocus(); }
    // The pane's own corner buttons sit over the top right; the bar keeps clear of them.
    void setHeaderRightInset(int pixels) override { if (m_row) m_row->setContentsMargins(10, 4, 10 + qMax(0, pixels), 4); }

    std::function<void()> onDockBack;
    std::function<void()> onFocus;

private:
    void refreshTitle() {
        if (m_title)
            m_title->setText(QStringLiteral("✦ %1 · linked shell · ! runs a command")
                                 .arg(m_titleText.isEmpty() ? QStringLiteral("Agent") : m_titleText + QStringLiteral(" agent")));
    }

    QPointer<QWidget> m_console, m_host;
    Slot m_slot;
    QString m_titleText;
    QLabel *m_title = nullptr;
    QToolButton *m_dock = nullptr;
    QHBoxLayout *m_row = nullptr;
};

}  // namespace relay
