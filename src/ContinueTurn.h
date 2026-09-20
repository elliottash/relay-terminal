// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::continueturn: when Ctrl+Enter on an empty prompt box continues the agent's stopped
// turn (#SXF1).
//
// Ctrl+Enter is the send-now key: with text in the box it reaches the agent, and while the agent
// is busy it interrupts and sends at once. With the box EMPTY and the agent idle it used to do
// nothing but scold. Two states make "continue" the right reading of an empty send: the last turn
// stopped at its step or tool-call limit, or the pane was restored from a session whose last turn
// was cut off mid-flight (Relay closed, the pane restarted — the worker reports it as
// `state_loaded {turn_open: true}`). The Pane owns the state; this function owns the rule, so it
// can be tested without a worker or a terminal. A turn the user stopped with Esc ended cleanly,
// so it never qualifies; neither does a conversation that simply finished.
#pragma once

namespace relay::continueturn {

// The pane facts that can change the answer. What is deliberately NOT here: anything the terminal
// is doing, and the queue — an empty send-now has nothing to send past them.
struct State {
    bool agentBusy = false;      // the worker reports a turn running
    bool textEmpty = true;       // the prompt box has nothing typed
    bool limitReached = false;   // the last turn ended at its step or tool-call limit
    bool turnCutOff = false;     // a resumed session whose last turn never ended
};

// True when the empty box's send-now means "continue the stopped turn" instead of "type a prompt
// first". With the agent busy or text in the box, Ctrl+Enter keeps every meaning it had.
inline bool sendNowContinues(const State &state) {
    return state.textEmpty && !state.agentBusy && (state.limitReached || state.turnCutOff);
}

}
