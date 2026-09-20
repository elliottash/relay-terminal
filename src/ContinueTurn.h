// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::continueturn: when Ctrl+Enter on an empty prompt box sends the agent `Continue` (#SXF1).
//
// Ctrl+Enter is the send-now key: with text in the box it reaches the agent, and while the agent is
// busy it interrupts and sends at once. With the box EMPTY and the agent idle it sends the ordinary
// prompt `Continue` — always, whatever the last turn did (owner, 2026-09-20: "ctrl+enter in an empty
// prompt should always send agent prompt 'continue'"). It used to be gated on the two states that
// plainly ask for it — a turn stopped at its step or tool-call limit, and a pane restored from a
// session whose last turn was cut off mid-flight — and to scold ("Type a prompt first.") for every
// other empty box; the owner hit that scold after an ordinary turn, so the rule is the plain one.
// The Pane owns the state; this function owns the rule, so it can be tested without a worker or a
// terminal. (A password prompt is the one place the Pane overrides it: the masked field stands in
// for the prompt box there.)
#pragma once

namespace relay::continueturn {

// The pane facts that can change the answer. What is deliberately NOT here: anything the terminal
// is doing, the queue — an empty send-now has nothing to send past them — and the pane's own
// limit/cut-off marks (`Pane::limitReached`, `Pane::turnCutOff`), which no longer gate the key:
// they still decide the "▸ Continue" line a stopped turn prints.
struct State {
    bool agentBusy = false;      // the worker reports a turn running
    bool textEmpty = true;       // the prompt box has nothing typed
};

// True when the empty box's send-now means "send Continue" instead of "type a prompt first". With
// the agent busy or text in the box, Ctrl+Enter keeps every meaning it had.
inline bool sendNowContinues(const State &state) {
    return state.textEmpty && !state.agentBusy;
}

}
