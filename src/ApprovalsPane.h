// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The first-launch choice Relay asks before anything else (card #K2FV): allow everything —
// the recommendation, and what a smooth Relay has always done — or pick the checklist of
// what must ask first. A view a ToolPane hosts (RelayWindow::openApprovalsPane splits it
// beside a pane on the first configure of an installation that has not answered yet), not a
// floating dialog, per the owner's standing preference for panes over overlays.
//
// It offers no way out of its own: no close button, and Esc does nothing, because the choice
// is what makes "allow everything" a choice rather than a silent default. The pane chrome's
// × (or Ctrl+W) still closes the pane — nothing may trap a window — and then the choice is
// simply still unanswered, so the cautious set stays in force and the next configure brings
// the pane back. Header-only, like the other hosted views' interface (src/PaneView.h);
// nothing here has Q_OBJECT, so it needs no moc output.
#include "PaneView.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

namespace relay {

namespace approvals {

// `approvals.CAUTIOUS` (backend/relay_core/approvals.py): what asks before the first-launch
// choice is answered. One list shared by the checklist rows (RelayWindow::approvalRow), a card's
// "Always allow" (Pane::rememberAlwaysAllowed) and this pane's choose button, because the three
// must never disagree about what an unanswered Relay asks. Written here rather than asked of the
// worker because the settings page can be drawn with no worker running, and it changes only with
// the checklist itself.
inline QStringList cautious() {
    return {QStringLiteral("edit"), QStringLiteral("delete_or_move"), QStringLiteral("read_outside"),
            QStringLiteral("terminal"), QStringLiteral("program")};
}

}  // namespace approvals

// The screen itself: three sentences, two buttons. The window owns the answers — what each
// button writes (security/approvals_ask, security/approvals_chosen) and what opens after
// (Options › Security, for the checklist button) is RelayWindow's knowledge, wired through
// the two callbacks when the pane is made, exactly as the ⓘ pane's links are.
class ApprovalsView final : public QWidget, public relay::PaneView {
public:
    explicit ApprovalsView(QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 14, 18, 18);
        layout->setSpacing(0);
        // The pane chrome's buttons sit over the top-right of the first row; the interface's
        // inset is honoured with a spacer so nothing is put under them.
        auto *top = new QHBoxLayout;
        top->addStretch(1);
        m_inset = new QWidget;
        m_inset->setFixedSize(1, 1);
        top->addWidget(m_inset);
        layout->addLayout(top);
        layout->addStretch(3);
        const char *const lines[] = {
            "Relay's agent runs commands and edits files without stopping to ask.",
            "That is what makes it fast, and it is what we recommend.",
            "It runs with your user's permissions, inside the pane's workspace.",
        };
        for (const char *const line : lines) {
            auto *label = new QLabel(QString::fromUtf8(line));
            label->setFont(relay::theme::legible(font(), relay::theme::BodyPt));
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::NoTextInteraction);
            layout->addWidget(label, 0, Qt::AlignTop);
            layout->addSpacing(6);
        }
        layout->addSpacing(10);
        // What is in force while the choice is unanswered: the cautious set, not allow-all —
        // "you have to explicitly pick that" only means something if not picking differs.
        auto *note = new QLabel(
            QStringLiteral("Until you choose, the cautious set asks first: edits, deletions, "
                           "reads outside the workspace, your terminal and your programs."));
        note->setObjectName(QStringLiteral("approvalsNote"));
        note->setFont(relay::theme::legible(font(), relay::theme::SecondaryPt));
        note->setWordWrap(true);
        note->setTextInteractionFlags(Qt::NoTextInteraction);
        layout->addWidget(note, 0, Qt::AlignTop);
        layout->addSpacing(14);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(10);
        m_allow = new QPushButton(QStringLiteral("Allow everything — recommended"));
        m_allow->setToolTip(QStringLiteral("No approval cards; what Relay has always done"));
        connect(m_allow, &QPushButton::clicked, this, [this] { if (onAllowEverything) onAllowEverything(); });
        buttons->addWidget(m_allow);
        m_choose = new QPushButton(QStringLiteral("Choose what needs approval"));
        m_choose->setToolTip(QStringLiteral("The cautious set ticks the checklist in Options › Security"));
        connect(m_choose, &QPushButton::clicked, this, [this] { if (onChooseChecklist) onChooseChecklist(); });
        buttons->addWidget(m_choose);
        layout->addLayout(buttons);
        layout->addStretch(4);
    }

    // The answers, wired by the window when the pane is made (RelayWindow::openApprovalsPane).
    std::function<void()> onAllowEverything;
    std::function<void()> onChooseChecklist;

    QString paneTitle() const override { return QStringLiteral("Approvals"); }
    void focusView() override { if (m_allow) m_allow->setFocus(); }
    void setHeaderRightInset(int pixels) override { if (m_inset) m_inset->setFixedSize(pixels, 1); }

private:
    QWidget *m_inset = nullptr;
    QPushButton *m_allow = nullptr, *m_choose = nullptr;
};

}  // namespace relay
