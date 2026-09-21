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

#include "AgentContext.h"   // relay::agent::Context: what the agent here is *about* (#AGNT step 2)
#include "AgentHost.h"

#include <QJsonObject>
#include <QObject>

namespace relay {

class AgentConsole final : public QObject {
public:
    // The host outlives the console: it owns it by value. Nothing here may call into the host
    // during construction -- a Pane builds its console as a member, before its own body has run.
    explicit AgentConsole(agent::Host &host) : m_host(host) {}
    // The context outlives the console (the host owns both, and declares the context first), so
    // the callback it is holding on our behalf has to go before we do.
    ~AgentConsole() override { if (m_context) m_context->onChanged = nullptr; }

    agent::Host &host() { return m_host; }
    const agent::Host &host() const { return m_host; }

    // ----- the context: what the agent is about here (#AGNT step 3) ---------------------------
    //
    // The host owns it and outlives the console, so this is a bare pointer and never an owning
    // one. A console with no context is a legal, quiet state -- every call below answers as if
    // nothing had been set -- because the waves that move the agent block run one at a time and
    // a half-moved console must not start refusing to draw.
    void setContext(agent::Context *context) {
        if (m_context == context) return;
        if (m_context) m_context->onChanged = nullptr;
        m_context = context;
        if (m_context)
            m_context->onChanged = [this] { if (onContextChanged) onContextChanged(); };
        if (onContextChanged) onContextChanged();
    }
    agent::Context *context() const { return m_context; }

    // What the worker is told. Read fresh every time rather than cached, so a host may answer
    // with today's workspace, today's role and today's brief without having to push anything.
    agent::ContextSpec spec() const {
        return m_context ? m_context->spec() : agent::ContextSpec();
    }
    // The `context` block of `configure` (protocol 30.7), or `{}` when there is no context --
    // and a `configure` with no block behaves exactly as one sent before this existed.
    QJsonObject configureBlock() const {
        return m_context ? m_context->spec().toJson() : QJsonObject();
    }
    // What rides on each `ask`: `surface` always, `screen` when there is one, `readonly` only
    // when true. `{}` with no context, which is what the pane sent before.
    QJsonObject askFields() const {
        return m_context ? m_context->spec().askFields() : QJsonObject();
    }

    // The row of things that need no typing, with the letters already made unique. Empty for a
    // terminal pane, which is why the row costs the terminal nothing.
    QList<agent::Action> actions() const {
        return m_context ? agent::withUniqueLetters(m_context->actions()) : QList<agent::Action>();
    }
    // A link activated in the transcript, offered to the context **before** the console's own
    // handling. True means "handled, stop".
    bool resolveLink(const relay::links::Target &target) {
        return m_context && m_context->resolveLink(target);
    }
    // A turn that has ended. Most contexts want nothing -- the answer is already in the
    // transcript; a card is the exception that writes it to the card's thread.
    void turnFinished(const agent::TurnRecord &record) {
        if (m_context) m_context->turnFinished(record);
    }
    // The grey text in the empty composer, or empty when the surface has not said.
    QString placeholder() const { return m_context ? m_context->placeholder() : QString(); }

    // The context said something `spec()` or `actions()` would now answer differently has moved:
    // a card going busy, Options swapping mode, a project attaching to the tab. The host rebuilds
    // the action row and re-sends the block from here.
    std::function<void()> onContextChanged;

private:
    agent::Host &m_host;
    agent::Context *m_context = nullptr;

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
