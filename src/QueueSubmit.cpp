// SPDX-License-Identifier: AGPL-3.0-or-later
#include "QueueSubmit.h"

namespace relay::queuesubmit {

Decision decide(const State &state) {
    // The agent alone decides. A turn that is running, or one that just left the queue and has
    // not been reported as running yet, is the only thing an agent prompt waits behind — and it
    // waits behind it as a queued item, never by cancelling or reordering it.
    if (state.agentBusy || state.agentTurnStarting) {
        // One turn can still be talked to without interrupting it: one parked waiting for
        // background agents or a background job (#T4VK). It has no step boundary of its own, so
        // the worker ends the wait for a steered message (Queue.peek_steer) and the message
        // joins the conversation right after the wait's tool result. Queue order is kept: only
        // the queue's head steers, and with older prompts queued the submit is not that head —
        // the empty-Enter steer delivers the first of them instead.
        if (state.agentBusy && state.agentWaitingBackground && state.agentQueueEmpty) return Decision::Steer;
        return Decision::Queue;
    }
    return Decision::StartNow;
}

int firstRunnable(const ItemView *items, int count, const LaneFacts &facts) {
    // Walk the list once, in order. The first item of each lane is that lane's head; the
    // first head whose lane is open is delivered. A head that cannot run — whatever stopped
    // it, including a guest line whose channel is busy — holds its whole lane: later items of
    // the same lane are skipped, never passed, so when the lane opens its oldest entry still
    // goes first. Items of the other lane are simply stepped over until its own head.
    bool agentSeen = false, terminalSeen = false;
    for (int i = 0; i < count; ++i) {
        const ItemView &item = items[i];
        if (item.kind == ItemKind::Agent) {
            if (agentSeen) continue;
            agentSeen = true;
            if (facts.agentBusy || facts.agentStarting || facts.agentPaused
                || facts.agentQuestionOpen || facts.agentHeld || !facts.agentConfigured)
                continue;
            return i;
        }
        if (terminalSeen) continue;
        terminalSeen = true;
        if (item.kind == ItemKind::Guest) {
            // The guest channel is serialized: a line for another guest, a guest that is not
            // in front, or a busy channel holds the terminal lane's guest head — and with it
            // the shell commands queued behind it in the same lane.
            if (!item.guestReady) continue;
            if (facts.terminalPaused || facts.terminalHeld) continue;
            return i;
        }
        if (facts.terminalActive || facts.terminalPaused || facts.terminalHeld) continue;
        if (!facts.terminalIdle) continue;
        return i;
    }
    return -1;
}

}
