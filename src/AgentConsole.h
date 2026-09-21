// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// relay::AgentConsole -- the agent surface: the prompt box and everything that belongs to it.
//
// The owner, 2026-09-20 (card #AGNT): "an agent interface is the prompt box. it has a set of
// options and tools that vary according to the setting/task, but in general they are shared
// systems." So the composer frame and its chips, the queue strip, the transcript with its
// thinking bubbles, tool rows and folds, the Activity hook, prompt history, voice, the model
// picker, the hints, Esc/Stop, the worker connection and the persisted conversation are one unit
// -- this one -- and not a feature of the terminal that other surfaces copy.
//
// It reaches whatever it is drawn on through relay::agent::Host (src/AgentHost.h), and nothing
// else: it never names Pane, RelayWindow or a Switchboard. A context -- what the agent is about
// -- arrives separately (src/AgentContext.h, #AGNT step 2) and specialises the console without
// fencing it: it has no tool list, because "agents have access to all systems and can work
// across panes and contexts".
//
// The extraction from Pane runs in waves (#AGNT step 1, scripts/split-agent-console.py). This
// file is the destination; each wave's block says which region of src/Pane.h it came from, and
// Pane keeps every public member it had as a forwarder, so every existing test compiles
// unchanged. That is the property that makes the move reviewable -- and the terminal pane
// behaving exactly as it does today is the one thing this step must not break.

#include "AgentHost.h"

#include <QObject>

namespace relay {

class AgentConsole final : public QObject {
public:
    // The host outlives the console: it owns it by value. Nothing here may call into the host
    // during construction -- a Pane builds its console as a member, before its own body has run.
    explicit AgentConsole(agent::Host &host) : m_host(host) {}

    agent::Host &host() { return m_host; }
    const agent::Host &host() const { return m_host; }

private:
    agent::Host &m_host;

    // Everything between these two lines is written by scripts/split-agent-console.py: one block
    // per member moved out of class Pane, each carrying the line it followed there, so --check
    // can put them all back and prove the rebuild is the original src/Pane.h to the byte. Nothing
    // below is edited by hand. `split-base` is the commit whose src/Pane.h the rebuild is
    // compared with: the tip the extraction started from.
    //
    // split-base: cbf06cc0d6681d8c1d2d82ed92def89af397af10
    // ===== moved out of class Pane by scripts/split-agent-console.py =====
    // ===== end of the moved blocks =====
};

}  // namespace relay
