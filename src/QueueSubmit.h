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

// The two lanes a pane's queue is scheduled by (card #XCXD). Every queued entry belongs to
// exactly one resource: the agent, or this pane's terminal — a shell command, or a guest-TUI
// line, which shares the guest's serialized input channel and so is held by the terminal
// lane's pause while never being delivered to the native shell itself. Each lane has its own
// pause, its own active reservation and its own FIFO; one lane keeps flowing while the other
// is paused, stopped or busy.
enum class ItemKind { Agent, Shell, Guest };

// One queued entry, reduced to what the scheduler needs: which lane it belongs to, and — for
// a guest-TUI line — whether the guest's serialized channel can take it right now (the named
// guest is the front surface and its channel is free). False holds the terminal lane.
struct ItemView {
    ItemKind kind = ItemKind::Shell;
    bool guestReady = false;
};

// Everything firstRunnable() needs to know about the lanes, stated as plain facts so the
// rule stays testable without a Pane (card #XCXD): no widget, no worker, no clock.
struct LaneFacts {
    // The agent lane: a turn running, the just-started reservation that holds the slot until
    // the busy report arrives, the pane's own pause for this lane, an open question (the
    // agent's question holds the agent's queue; the terminal lane keeps flowing), and the
    // selection hold when the highlighted head entry belongs to this lane.
    bool agentBusy = false, agentStarting = false, agentPaused = false;
    bool agentQuestionOpen = false, agentHeld = false;
    bool agentConfigured = true;
    // The terminal lane: a command already running under it, the pane's own pause for this
    // lane, the same selection hold for a terminal head, and whether the native shell can
    // take a command right now.
    bool terminalActive = false, terminalPaused = false, terminalHeld = false;
    bool terminalIdle = false;
};

// The first entry, in list order, whose lane can take it right now — the head of that lane's
// FIFO — or -1 when every lane is blocked. The rule is strict within a resource and free
// between them: the first item of a lane is that lane's head, and a head that cannot run
// holds the whole lane — nothing later in the same lane passes it, whichever surface it was
// headed for. Only the other lane is stepped over, which is what lets it keep flowing. The
// caller delivers the entry it is handed, closes that lane, and calls again: one entry per
// lane per pump, the oldest first.
int firstRunnable(const ItemView *items, int count, const LaneFacts &facts);

// The empty-Enter sequence's decision, as a pure function of its state (card #XCXD). The
// state is the step the sequence is on and whether a non-empty draft has appeared since; the
// answer is what the next empty Enter does. The delivered step is a tombstone: the sequence
// stays pinned to the prompt that was delivered — repeated Enter answers "already has it"
// and never falls through to the next queued prompt — until a new draft or a take-back.
enum class EnterStep { Idle, Steered, Delivered };
enum class EnterAction { SteerOldest, SendNow, AlreadyDelivered, Idle };

inline EnterAction enterStepAction(EnterStep step, bool draftSeen) {
    if (draftSeen) return EnterAction::SteerOldest;   // the new draft reset the sequence
    switch (step) {
    case EnterStep::Idle: return EnterAction::SteerOldest;
    case EnterStep::Steered: return EnterAction::SendNow;
    case EnterStep::Delivered: return EnterAction::AlreadyDelivered;
    }
    return EnterAction::Idle;
}

inline EnterStep enterStepAfter(EnterStep step, EnterAction action) {
    switch (action) {
    case EnterAction::SteerOldest: return EnterStep::Steered;
    case EnterAction::SendNow: return EnterStep::Idle;   // escalated and sent: the sequence is done
    case EnterAction::AlreadyDelivered: return EnterStep::Delivered;   // stays pinned
    case EnterAction::Idle: return step;
    }
    return step;
}

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
