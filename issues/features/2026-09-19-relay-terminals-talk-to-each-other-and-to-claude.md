---
id: R5TC
type: work
status: inbox
labels: [feature]
component: [gui, worker, router]
milestone: beta
workstream: agent
assignee: agent
rank: zzzzzzj
created: '2026-09-19'
acceptance: the owner decides the shape first; if adopted, one pane's agent can address another pane — in this Relay or a paired one — and a `claude` or `codex` guest in a pane, and gets a turn result back rather than a screenshot
source: 'issues/feature_intake.txt, 2026-09-19: "allow relay terminals to talk to each other, and even better to claude and codex agents"'
links: {plans: [], commits: [], evidence: [], related: [W5N2, JQ7R, T4BS, GT7X, C1HH, 2JY7, SSRQ], github: null}
---
# Let Relay terminals talk to each other, and to the claude and codex agents in them

## Issue

allow relay terminals to talk to each other, and even better to claude and codex agents

## What already exists (surveyed 2026-09-19, so this card is only the gap)

Not reproducible in the usual sense — nothing is broken. What follows is what is already built, so
that what is new is not built twice.

**Relay ↔ Relay, human-driven: built.** Two desktops already share a pane end to end. The transport
is Noise `IK_25519_AESGCM_SHA256` (`remote/noise.py`, `app/noise.js`) over the rendezvous server
(`rendezvous/server.py`), with the hub in `remote/host.py` and a Python sidecar on each side
(`remote/gui_host.py` for the sharer, `remote/viewer.py` for the joiner). `src/RemotePane.h:3` is
"a pane shared by another desktop, opened in this Relay as the owner's own device", and it has a
real composer: `RemotePane::compose()` (`src/RemotePane.cpp:1816-1830`) sends `{"t":"compose"}`,
which `remote/host.py:1954-1982` hands to the pane exactly as its own composer would
(`src/Pane.h:9940-9953`). Joining is `/join CODE` plus a PIN over CPace (`remote/meetcode.py`,
#97EG, #JQ7R), or whole-tab sharing (#T4BS).

**Guest `claude` / `codex` in a pane: built (#GT7X, `needs-qa-llm`).** Relay knows when one is in the
foreground, reads its turn state from sanctioned sources only — claude hooks and the statusline
shim (`backend/relay_core/guest_hook.py`), codex `notify` plus the rollout tail
(`backend/relay_core/guest_codex.py`) — and serves claude an IDE bridge over WebSocket MCP
(`backend/relay_core/guest_bridge.py`). A person can type into the guest from Relay's own composer
(`typeIntoGuest()`, `src/Pane.h:9975-9994`). The posture is stated in `docs/ARCHITECTURE.md:1786`:
Relay observes, and touches a guest only through the guest's own sanctioned surfaces.

**Agent ↔ agent: built, but only inside one worker.** `agent`, `agent_message`, `agent_wait`
(`backend/relay_core/subagents.py:59`) address subagent threads of the same pane's worker, depth 1.

## The gap

Every crossing is human-driven. No agent is an addressable party:

- `backend/relay_core/tools.py` has no tool that names another pane, another Relay or a guest.
  There is no `pane_*` family. The only cross-pane channel today is indirect and file-based — the
  Switchboard, several panes' agents writing the same cards under `flock`.
- The remote direction is one-way: a joiner can type into the sharer's pane; a sharer has no way to
  push anything into a peer's pane, no peer list, and every owner-side verb is `OWNER_ONLY` /
  `NEVER_FROM_CLIENT` (`remote/wire.py:96,108`) — refused when it arrives over the wire even from
  the owner's own phone.
- Relay's agent can only *blind-type* into a guest, through `type_into_program`
  (`backend/relay_core/program_input.py:70`) under a one-turn human delegation grant (#C1HH), and
  then read a screen snapshot. There is no ask-and-await: the structured harness that would give a
  turn result back (claude `stream-json`, codex `app-server`) is #GT7X's task `t:x2`, **deferred by
  the owner**.
- The guest channel is deliberately withheld from the wire (`remote/wire.py:395`), so nothing about
  a guest reaches a remote participant today.

## Decisions needed (owner) — this card is `inbox` until they are made

1. **Scope.** Which of the three does this mean, and in what order: pane → pane inside one Relay;
   Relay → paired Relay; agent → `claude`/`codex` guest? They share a shape but nothing else — the
   first needs no transport at all, the second needs an outbound direction that does not exist, the
   third needs #GT7X's deferred harness.
2. **Consent for a machine-originated prompt.** A guest's prompt from a person goes through
   `ask_owner_about_prompt` (`docs/REMOTE-PROTOCOL.md:921`). A prompt from another *agent* has no
   person behind it. Either it is approved per message — which cuts against the standing "no
   per-action approvals" decision (`docs/ROADMAP.md:43`) — or a pane is opted in once to being
   addressable, and by whom. This is the decision the rest waits on.
3. **Whether the sharer may push outbound at all**, or whether "talking" stays pull-only: the peer
   asks, this Relay answers. Pull-only needs no new direction and no peer directory.
4. **Whether the guest harness is un-deferred.** Without it "talk to claude and codex" can only mean
   typing at a TUI and reading the screen back, which is the thing #GT7X's posture deliberately
   avoids. With it, an agent gets a turn result — and Relay starts owning a guest process, which is
   the line #GT7X drew.

## Tasks

- [ ] Owner: answer the four decisions above <!-- t:v3 -->
- [ ] Then: a design note beside `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` for the addressing and <!-- t:w8 -->
      consent model — how an agent is named, who may address it, what it may be asked, and what
      comes back — before any protocol verb or tool is added
