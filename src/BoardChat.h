// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Switchboard page agent's panel (#8YQ9, protocol 19.18): the conversation about the whole
// board, pinned to the bottom of the list page.
//
// The panel itself moved to `src/HelperChat.h` on 2026-09-20 (#FEJQ). It is one surface now —
// the Switchboard agent and the helpers in Options, Actions and Sessions are "a single joint
// system" (owner) talking to one worker per tab — and what is left here is the board's name for
// it: the pane name the panel takes, and the extras (survey, queue, Check) it is the flag for.
// The wording a clicked problem drafts into its composer went with the panel — `board::fixRequest`
// is still spelled that way, and still lives beside the findings list that uses it.
#include "HelperChat.h"

#include <QWidget>

namespace relay {

// The helper panel as the Switchboard has it: the `switchboard` pane, which is the one that keeps
// the survey offer, the queue box and the Check button, and the one that is never collapsed. The
// subclass carries no code — the extras are a flag inside `HelperChatPanel`, for the reason its
// header gives — but it is the name `BoardView` and the board's tests build, and it says at every
// call site which of the four panels this is.
class BoardChatPanel : public HelperChatPanel {
public:
    explicit BoardChatPanel(QWidget *parent = nullptr)
        : HelperChatPanel(helperpane::switchboard(), parent) {}
};

}  // namespace relay
