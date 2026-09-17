# Model roles: implementer evidence (2026-09-17)

Not a QA verdict. Implemented by Claude Opus 5 (Claude Code) in the model-roles worktree.

## Automated

- `./scripts/test.sh`: **298 tests, all pass** (273 before). New file `tests/test_roles.py` (25 tests):
  role-table validation, the fast-agent default per main provider (GLM/GLM Coding/OpenRouter/Kimi/Kimi
  Code, GLM with thinking disabled), unknown endpoints keeping every role on main, chores preferring
  OpenRouter then the fast agent, vision and route-assist defaults, the main key being reused without a
  keyring lookup, configured roles with preset/custom endpoint/effort, a missing key falling back to main
  with exactly one warning, summaries carrying no key material, `rebase`/`set_roles`, subagent inheritance
  through the `subagent` role, role names as subagent model specs, `side_provider(role=…)`, and three
  worker runs (`configure {roles, agent_role}`, `set_agent_role`, `set_agent_options {roles}`, and a bad
  role table reporting `error`).
- No test touches the real keyring or the network: role tests use dict key lookups, `relay_core.keystore.lookup`
  and `route_assist.router_provider` are patched where an audit could reach them, and `scripts/test.sh` now
  exports `RELAY_KEYRING=off`, which the worker subprocesses spawned by tests inherit.
- `cmake --build build`: clean, no new warnings. `ctest --test-dir build`: 6/6 pass.

## Live run (real keys from the keyring, never printed)

Driver: the worker protocol over stdio, main preset **Z.AI Coding Plan `glm-5.3`**. Raw events in
`subagent-role.jsonl`, `fast-pane.jsonl`, `main-pane.jsonl` (deltas and tool output stripped);
`summary.json` has the extracted results. Keys appear nowhere in the captures.

1. **Subagent on a different model than main** — `configure {roles: {subagent: {preset: "kimi"}}}`, then a
   prompt asking the main agent to run the `prober` subagent in the foreground:
   - `configured.roles.subagent` = `{model: "kimi-k3", preset: "kimi", source: "configured"}` while
     `configured.model` = `glm-5.3`.
   - `subagent_started {id: "a1", type: "prober", model: "kimi-k3"}`, then
     `subagent_finished {outcome: "done", summary: "SUBAGENT-OK", elapsed_ms: 7447}`.
   - The main agent (GLM) answered: "The prober subagent replied: `SUBAGENT-OK`" (turn 11.9 s).
2. **Fast-agent pane** — `configure {agent_role: "fast"}` with the same main preset:
   - `configured` = `{agent_role: "fast", model: "glm-5.3-flash"}`, `roles.fast.source` = `default`
     (the per-provider default with thinking disabled).
   - "Reply with exactly the word READY" answered `READY` three times in **4.66 / 4.70 / 4.24 s**.
   - Control, same prompt on the main agent (`glm-5.3`): `READY` in **3.44 / 4.13 / 3.64 s**.

   Observation to flag for QA: on this one-word prompt the GLM fast default was **not** faster than
   `glm-5.3` from this machine (both dominated by round-trip and the Coding Plan endpoint). The
   per-provider default is applied exactly as the owner specified; the latency win in the issue's table
   was measured on longer prompts, and the Kimi/OpenRouter defaults were not re-measured here.

## Live GUI (Xvfb :97/:98, isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`, this worktree's `build/relay`)

Driven with `xdotool`; the pane configured itself from the keyring (Kimi K3 in this profile).

- `implementer-roles-menu.png`, `implementer-roles-list.png` — Actions › Agent options › **Model roles**
  lists all seven settable roles, each "Same as main agent" with the effective model in brackets:
  Subagent `(kimi-k3)`, Fast agent `(kimi-k2.7-cod…)`, Chores `(google/gemini-3…)`, Route assist
  `(google/ge…)`. These come from `configured.roles`, so the per-provider defaults and the OpenRouter
  chores/route-assist defaults resolved against the real keyring.
- `implementer-fast-agent-action.png` — the palette action "Fast agent for this pane".
- `implementer-fast-agent-chip.png` — after running it: the status line reads
  "Fast agent: kimi-k2.7-code-highspeed · conversation kept", i.e. `set_agent_role` round-tripped live.
- No crash, no output on stderr, and the isolated config held (`~/.config/RelayTerminal` untouched).

## Not covered by this evidence

- Not screenshotted: per-role preset/model/effort editing, the "New panes use the fast agent" toggle and
  the chip tooltip listing every role (exercised by unit tests and code review only). QA should drive
  those in the GUI.
- The Switchboard role is stored and resolved but has no consumer yet.
- Vision role: resolved and reported, but no image turn uses it yet.
