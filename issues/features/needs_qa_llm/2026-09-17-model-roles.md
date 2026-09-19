---
id: 4WHD
type: work
status: needs-qa-llm
labels: [feature]
component: [worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, model-roles worktree), 2026-09-17
rank: mn
created: '2026-09-17'
acceptance: '`tests/test_roles.py` (25 tests, `./scripts/test.sh` 298 total), live worker and GUI runs in `docs/qa_evidence/2026-09-17-model-roles/`'
source: '`issues/features/needs_qa_llm/2026-09-17-model-roles-and-fast-agent.md` (owner in chat, 2026-09-17), `docs/AGENT-SESSIONS-PROTOCOL.md` section 13'
links: {plans: [], commits: [], evidence: [], related: [], github: null, merged_from: [KH72]}
---
# Model roles: a configurable model per job, fast agent in panes

> **Renamed since this card was written.** The pane-agent role `fast` is called `flash` as of
> 2026-09-18, and with it `agent.fastAgent` → `agent.flashAgent`, QSettings `agent/panes_fast` →
> `agent/panes_flash` and `roles/fast/*` → `roles/flash/*`; the visible label is "Flash agent".
> Wherever this card says `fast`, read `flash`. Old names are still accepted on the way in
> (`roles.DEPRECATED_ROLES`) and settings are migrated once at startup, so the behaviour described
> below is unchanged — only the spelling is. See `#C6YX`
> (`issues/changes/needs_qa_llm/2026-09-18-model-tier-commands-and-flash-naming.md`).

## Behavior

Backend: `backend/relay_core/roles.py` (new), with call sites in `agent.py`, `subagents.py`,
`session_protocol.py`, `observe_protocol.py`, `keystore.py` and `worker.py`. GUI: `src/main.cpp`
(blocks marked "model roles").

Roles and defaults

- Roles: `main`, `terminal_use`, `subagent`, `switchboard`, `fast`, `chores`, `vision`, `route_assist`.
  Every role is "same as the main agent" until configured. `switchboard` is stored and resolved now and
  gets a consumer when the Switchboard lands.
- Fast agent by main provider: GLM/GLM Coding → `glm-5.3-flash` with `{"thinking": {"type": "disabled"}}`;
  OpenRouter → `deepseek/deepseek-v4.1-flash`; Kimi → `kimi-k2.7-code-highspeed`; Kimi Code →
  `kimi-for-coding-highspeed`; custom endpoints → the main agent.
- Chores: `google/gemini-3.8-flash` on OpenRouter when a key is stored, else the fast agent. Vision:
  `glm-5.3-flash` on GLM, else main. Route assist keeps `google/gemini-3.5-flash-lite` (its own fast model),
  now overridable as a role.
- Per-role config: a preset id (optionally with a different model id) or a custom `base_url` + `model`,
  plus optional `extra` and effort. Keys resolve through the keystore (env var, then the desktop keyring)
  and the main agent's in-memory key is reused when a role lands on the main preset, so switching roles
  inside one provider never hits the keyring.
- A role whose key is missing falls back to the main agent with a one-line warning in `model_roles`
  (`Subagent: no stored key for glm; using the main agent.`). Never an error, never a refused configure.

Where roles are used today

- Subagents that do not name a model use the `subagent` role (`SubagentFactory.base()`); a definition's own
  model still wins, and role names (`fast`, `chores`, …) are accepted as subagent model specs.
- Side calls: compaction summaries, recaps and suggestions use `fast`; the request audit uses `chores`
  (previously always the route-assist model); routing assist uses `route_assist`; instruction synthesis
  stays on main.
- Panes: `configure {agent_role}` and `set_agent_role {role}` run the pane's own agent on a role, keeping
  the conversation. New panes default to the fast agent (Agent options toggle, on by default); the first
  pane of a window keeps the main agent, and a restored layout keeps each pane's saved role.

Protocol (section 13, additive)

- `configure` / `set_agent_options` accept `roles`; `configure` also accepts `agent_role`.
- `configured` gains `agent_role` and `roles` (every role: model, preset, base_url, effort, source, label).
- `model_roles {roles, agent_role, warnings}` after `set_agent_options {roles}`, after `set_model`, and
  after `configure` only when a role fell back (so existing event ordering is unchanged).
- `set_agent_role {role}` → `model_changed {…, agent_role, warning?}` + `context`.
- `RELAY_KEYRING=off` skips the desktop keyring (tests, headless).

GUI

- Actions › Agent options › **Model roles** → one submenu per role: "Same as main agent" (default, showing
  the effective model in the detail line), each stored preset, "Model id…", "Reasoning effort". Changes are
  sent live with `set_agent_options {roles}`.
- Actions › Agent options › **New panes use the fast agent** (on by default).
- Actions › **Fast agent for this pane** (`agent.fastAgent`, Alt+F), with the usual shortcut hint when it is
  run from the palette.
- Model chip: shows `Fast agent · <model>` when the pane runs a role, lists every role's effective model in
  its tooltip, and picking a preset from the chip puts the pane back on the main agent.
- QSettings: `roles/<role>/{preset,model,effort}`, `agent/panes_fast`.

## QA checklist

1. **Defaults.** With only one provider key stored, open Agent options › Model roles: every role says
   "Same as main agent", and the fast/chores/route-assist lines show the effective model in brackets.
   Check the fast model matches the provider table above (GLM → `glm-5.3-flash`, Kimi → `kimi-k2.7-code-highspeed`).
2. **Fast pane.** Alt+F (or the palette action). The chip shows `Fast agent · <model>`, the status line says
   "Fast agent: … · conversation kept", earlier turns are still in the transcript, and a new prompt is
   answered by the fast model. Alt+F again returns to the main agent.
3. **New panes.** Split a pane (Ctrl+P): the new pane starts on the fast agent, the first pane does not.
   Turn the Agent options toggle off and split again: the new pane is on the main agent. Restart Relay and
   check each pane comes back on the role it had.
4. **Subagent role.** Set Subagent to a different stored provider, then ask the main agent to run a subagent
   (`/agents`, or a prompt that uses the `agent` tool). The running-agents list and `subagent_started` must
   show the role's model, not the main model.
5. **Missing key.** Set a role to a provider with no stored key. The pane prints one warning line
   ("…no stored key…; using the main agent") and keeps working; nothing errors, and the role reads back as
   the main model.
6. **Effort.** Give a role effort `low` and confirm the request uses the provider's mapping (section 3),
   e.g. GLM keeps `thinking.enabled` with `reasoning_effort: low`.
7. **Keys.** Confirm no key material appears in any event, log, screenshot or `~/.config/RelayTerminal/relay.conf`.
8. **Route assist.** With an OpenRouter key stored, routing assist still answers in well under a second
   (section 11); setting the Route assist role to another provider changes the model used.

## Known gaps

- Difficulty-based routing between the main and fast agent is intentionally not implemented (owner: "later").
- The `vision` and `switchboard` roles are resolved and reported, but nothing consumes them yet (no image
  turns, no Switchboard).
- The per-role editor offers stored presets plus a model id and effort; a fully custom `base_url` for a role
  is supported by the protocol but not yet by the GUI.
- A pane running the fast agent resolves roles that "follow main" against the fast model (the `subagent`
  role and subagent inheritance still use the configured main model).
- On this machine the GLM fast default was not measurably faster than `glm-5.3` on a one-word prompt
  (4.2–4.7 s vs 3.4–4.1 s); see the evidence notes.

## Merged in
### #KH72 — Model roles in settings: main, fast, terminal-use, subagent, Switchboard, chores, vision (2026-09-19)

Merged from `issues/features/needs_qa_llm/2026-09-17-model-roles-and-fast-agent.md` (needs-qa-llm): Same request, one delivery: #KH72 is the owner's model-roles ask with the measurements, #4WHD the implementation record KH72's own body points to (same worktree, same day, same evidence folder); 4WHD carries the QA checklist.

#### Roles
| Role | Default |
|---|---|
| Main agent | the chosen preset |
| Terminal-use agent (drives programs, fixes commands) | main |
| Subagent | main |
| Switchboard agent (card threads) | main |
| Fast agent (default in panes) | per provider, below |
| Chores (Switchboard duplicate checks, labels, titles, note scans) | `google/gemini-3.8-flash` on OpenRouter; without an OpenRouter key, the fast agent |
| Vision (image turns on non-vision presets) | GLM: GLM 5.3 Flash; otherwise unset |
| Route assist | `google/gemini-3.5-flash-lite` (unchanged, latency budget under 1 s) |

Fast agent defaults by main provider: GLM → `glm-5.3-flash`; OpenRouter → `deepseek/deepseek-v4.1-flash` (alternative
`google/gemini-3.8-flash`); Kimi → owner decision (2026-09-17): **K2.7 Code HighSpeed for now** (`kimi-k2.7-code-highspeed` on the `kimi`
platform preset, `kimi-for-coding-highspeed` on the `kimi-code` preset). Upgrade to K2.8 (`kimi-for-coding`) when it is
available through the Coding Plan API key; measure it at `reasoning_effort: low` first.

Later: route between main and fast agent in the main terminal by estimated task difficulty.

#### Measurements (2026-09-17, 3 short prompts each, streaming, from this machine)
| Model | First token | Total |
|---|---|---|
| OpenRouter `deepseek/deepseek-v4.1-flash` | 0.5–1.6 s | 1.2–4.8 s (124–195 tok/s; OpenRouter picked a different host each call) |
| OpenRouter `google/gemini-3.8-flash` | 2.3–4.9 s (thinks before answering) | 2.6–4.9 s |
| OpenRouter `google/gemini-3.5-flash-lite` | 0.5–0.6 s | 0.7–1.0 s |
| Kimi `kimi-k2.7-code-highspeed` | 1.0–1.1 s | 1.0–2.3 s |
| Kimi `kimi-k3`, effort low | 3.2–3.8 s | 4.3–6.7 s |
| Z.AI coding `glm-5.3-flash`, thinking on | 3.0–3.1 s | 3.1–19.2 s |
| Z.AI coding `glm-5.3-flash`, thinking off | 1.7–3.4 s | 1.7–5.0 s |
| Z.AI coding `glm-5-turbo` | 2.7–3.6 s | 3.9–15.9 s |

#### Implemented (2026-09-17)

Landed as described above, except difficulty-based routing. Implementation notes, protocol summary and the
QA checklist: `issues/features/needs_qa_llm/2026-09-17-model-roles.md`. Protocol:
`docs/AGENT-SESSIONS-PROTOCOL.md` section 13. Evidence: `docs/qa_evidence/2026-09-17-model-roles/`.

Deliberately left for later (owner: "later on we can add routing between the main agent and fast agent in
the main terminal based on estimated task difficulty"): no difficulty estimate and no automatic main↔fast
routing. A pane is on one agent at a time; the user switches with Alt+F or the palette, and new panes start
on the fast agent. When that routing is picked up, it should build on `roles.RoleResolver.resolve("fast")`
and the existing `set_agent_role` command rather than a second resolution path.
