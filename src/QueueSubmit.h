// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::queuesubmit: when a submitted agent prompt starts its turn at once, and when it queues
// (#N8VK).
//
// Agent prompts and shell commands share one FIFO list per pane, but they contend for different
// resources: an agent prompt waits on the agent alone, a shell command on the terminal. Strict
// FIFO made an agent prompt wait behind shell work it never needed — from Ctrl+Enter, the one
// key whose own description promises "send now". The Pane owns the state; this function owns the
// rule, so it can be tested without a shell or a worker. What is *not* in the State is the rule:
// neither a running shell command nor items queued ahead of it can hold an agent prompt back.
#pragma once

namespace relay::queuesubmit {

// What a submitted agent prompt does right now.
enum class Decision {
    StartNow,   // the agent is free: begin the turn at once, bypassing the queue; queued items keep their order
    Queue,      // the agent already has a turn: join the back of the queue
};

// The pane facts that can change the answer. Everything else — a shell command running, shell or
// agent items queued ahead, the queue paused — deliberately cannot (see #N8VK).
struct State {
    bool agentBusy = false;         // the worker reports a turn running
    bool agentTurnStarting = false; // an agent entry left the queue, and the worker's "busy" report has not arrived yet
};

Decision decide(const State &state);

// TUI delivery has a gap between writing Return and receiving a busy hook. Do not
// release that reservation on an idle snapshot: it may predate the submitted text.
class GuestDelivery {
public:
    bool available(bool busy) const { return !busy && !m_pending; }
    void sent() { m_pending = true; }
    void observe(bool busy) {
        if (busy) m_pending = false;
    }
    void reset() { m_pending = false; }
private:
    bool m_pending = false;
};

// A queued launch owns the shell until the TUI exits, not the guest's prompt slot.
inline bool queueResourceAvailable(bool guestHead, bool activeEntry) {
    return guestHead || !activeEntry;
}

}
