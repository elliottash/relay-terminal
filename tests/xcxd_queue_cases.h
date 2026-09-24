// SPDX-License-Identifier: AGPL-3.0-or-later
// The two-lane queue regressions for card #XCXD (independent shell/agent FIFO scheduling,
// per-resource pause, and the stable empty-Enter sequence). Pure scheduling logic only:
// relay::queuesubmit owns the rule, so these cases run without a Pane, a worker or a clock.
// Parent: include after the CHECK macros exist, call cases::xcxdQueueCases() from both the
// --xcxd-only and the default test main.
#pragma once

#include "../src/QueueSubmit.h"

namespace cases {

using relay::queuesubmit::EnterAction;
using relay::queuesubmit::EnterStep;
using relay::queuesubmit::ItemKind;
using relay::queuesubmit::ItemView;
using relay::queuesubmit::LaneFacts;

inline int runOf(int index, ItemView items[], int count, LaneFacts facts) {
    // One pump: deliver the entry the scheduler hands back, close its lane the way the
    // delivery does, and ask again until nothing runs. The queue shrinks as items go.
    int delivered = 0;
    bool guestChannelBusy = false;
    while (index >= 0) {
        ++delivered;
        const ItemKind kind = items[index].kind;
        if (kind == ItemKind::Agent) facts.agentStarting = true;   // the reservation holds the slot
        else if (kind == ItemKind::Guest) guestChannelBusy = true; // the serialized channel, alone
        else facts.terminalActive = true;                          // a shell command runs under it
        for (int j = index; j + 1 < count; ++j) items[j] = items[j + 1];
        --count;
        if (guestChannelBusy)
            for (int j = 0; j < count; ++j)
                if (items[j].kind == ItemKind::Guest) items[j].guestReady = false;
        index = relay::queuesubmit::firstRunnable(items, count, facts);
    }
    return delivered;
}

void xcxdQueueCases() {
    // A queued agent prompt no longer holds a shell command back: with the agent busy, the
    // terminal lane still delivers (card #XCXD route case).
    {
        ItemView items[] = { {ItemKind::Shell, false} };
        LaneFacts facts; facts.agentBusy = true; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 1, facts) == 0);
    }
    // And the mirror: a running shell command no longer holds an agent prompt back.
    {
        ItemView items[] = { {ItemKind::Agent, false} };
        LaneFacts facts; facts.terminalActive = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 1, facts) == 0);
    }
    // FIFO within a lane: the agent lane runs its oldest entry while the shell command ahead
    // of it in the list waits for its own lane — and nothing of the terminal's passes the
    // agent's head. One pump of each lane.
    {
        ItemView items[] = { {ItemKind::Agent, false}, {ItemKind::Shell, false},
                             {ItemKind::Agent, false}, {ItemKind::Shell, false} };
        LaneFacts facts; facts.agentBusy = false; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 4, facts) == 0);   // oldest agent first
        facts.agentStarting = true;   // its reservation now holds the agent lane
        CHECK(relay::queuesubmit::firstRunnable(items, 4, facts) == 1);   // then the oldest shell
        facts.terminalActive = true;  // …and it is running
        CHECK(relay::queuesubmit::firstRunnable(items, 4, facts) == -1);  // nothing passes a head
    }
    // The agent lane's pause holds only the agent lane: the shell keeps flowing.
    {
        ItemView items[] = { {ItemKind::Agent, false}, {ItemKind::Shell, false} };
        LaneFacts facts; facts.agentPaused = true; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 2, facts) == 1);
    }
    // The terminal lane's pause holds only the terminal lane: the agent keeps flowing.
    {
        ItemView items[] = { {ItemKind::Shell, false}, {ItemKind::Agent, false} };
        LaneFacts facts; facts.terminalPaused = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 2, facts) == 1);
    }
    // An open question is the agent's: it holds the agent queue alone, not the shell's.
    {
        ItemView items[] = { {ItemKind::Agent, false}, {ItemKind::Shell, false} };
        LaneFacts facts; facts.agentQuestionOpen = true; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 2, facts) == 1);
    }
    // The selection holds only the lane of the entry it is on.
    {
        ItemView items[] = { {ItemKind::Agent, false}, {ItemKind::Shell, false} };
        LaneFacts facts; facts.agentHeld = true; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 2, facts) == 1);   // shell flows
        LaneFacts other; other.terminalHeld = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 2, other) == 0);   // agent flows
    }
    // A blocked head holds its whole lane: a guest line whose channel is busy holds the
    // shell command queued behind it in the same lane (the parent-requested mixed case) —
    // while the agent lane behind them both keeps flowing.
    {
        ItemView items[] = { {ItemKind::Guest, false}, {ItemKind::Shell, false},
                             {ItemKind::Agent, false}, {ItemKind::Guest, true} };
        LaneFacts facts; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 4, facts) == 2);   // the agent, not the ready guest
        facts.agentStarting = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 4, facts) == -1);  // nothing in the terminal lane passes
    }
    // A ready guest head delivers, and closes the terminal lane behind it; a second guest
    // waits even if it is ready — the channel is serialized.
    {
        ItemView items[] = { {ItemKind::Guest, true}, {ItemKind::Guest, true}, {ItemKind::Shell, false} };
        LaneFacts facts; facts.terminalIdle = true;
        CHECK(relay::queuesubmit::firstRunnable(items, 3, facts) == 0);
        CHECK(runOf(0, items, 3, facts) == 1);
    }
    // A full pump of an interleaved queue delivers exactly one entry per lane, oldest first,
    // when both lanes are free.
    {
        ItemView items[] = { {ItemKind::Shell, false}, {ItemKind::Agent, false},
                             {ItemKind::Shell, false}, {ItemKind::Agent, false} };
        LaneFacts facts; facts.terminalIdle = true;
        CHECK(runOf(0, items, 4, facts) == 2);   // one per lane: shell[0] and agent[1]
    }
    // decide() unchanged for the agent lane: a running turn — or one just started from the
    // queue, before the busy report — is the only thing an agent prompt waits behind.
    {
        relay::queuesubmit::State state;
        CHECK(relay::queuesubmit::decide(state) == relay::queuesubmit::Decision::StartNow);
        state.agentTurnStarting = true;   // the lane's own reservation, terminal aside
        CHECK(relay::queuesubmit::decide(state) == relay::queuesubmit::Decision::Queue);
        state.agentTurnStarting = false; state.agentBusy = true;
        CHECK(relay::queuesubmit::decide(state) == relay::queuesubmit::Decision::Queue);
    }

    // The empty-Enter sequence: idle steers the oldest agent prompt, the next Enter sends
    // that same prompt now, and a delivered prompt stays pinned — the third and fourth Enter
    // answer "already has it" instead of falling through to the next queued prompt.
    {
        EnterStep step = EnterStep::Idle;
        CHECK(relay::queuesubmit::enterStepAction(step, false) == EnterAction::SteerOldest);
        step = relay::queuesubmit::enterStepAfter(step, EnterAction::SteerOldest);
        CHECK(step == EnterStep::Steered);
        CHECK(relay::queuesubmit::enterStepAction(step, false) == EnterAction::SendNow);
        step = EnterStep::Delivered;   // the worker delivered the steer between the presses
        CHECK(relay::queuesubmit::enterStepAction(step, false) == EnterAction::AlreadyDelivered);
        CHECK(relay::queuesubmit::enterStepAfter(step, EnterAction::AlreadyDelivered) == EnterStep::Delivered);
        // The tombstone holds: the 4th press (and every press after it) is still pinned.
        CHECK(relay::queuesubmit::enterStepAction(EnterStep::Delivered, false) == EnterAction::AlreadyDelivered);
        CHECK(relay::queuesubmit::enterStepAction(EnterStep::Delivered, false) == EnterAction::AlreadyDelivered);
        // A new draft lifts the pin: the next empty Enter starts a fresh sequence.
        CHECK(relay::queuesubmit::enterStepAction(EnterStep::Delivered, true) == EnterAction::SteerOldest);
        CHECK(relay::queuesubmit::enterStepAction(EnterStep::Steered, true) == EnterAction::SteerOldest);
        // A successful send-now ends the sequence outright.
        CHECK(relay::queuesubmit::enterStepAfter(EnterStep::Steered, EnterAction::SendNow) == EnterStep::Idle);
    }
}

}   // namespace cases
