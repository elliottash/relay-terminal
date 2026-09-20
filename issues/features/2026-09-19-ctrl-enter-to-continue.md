---
id: SXF1
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzz
created: '2026-09-19'
links: {commits: [d9cde60524af4b8def16916aa505875d321299de, 472ae1a210a4e0577d690ce6effcc396982c9cfc, 78416e396b1cf6db5eebe1b45efab4014de01fda], evidence: [docs/qa_evidence/2026-09-20-ctrl-enter-continue/], github: null, plans: [], related: []}
---
# ctrl + enter to continue

## Issue
press ctrl + enter to continue, eg if you ran out of turns, or you closed and restarted in progress.

## Plan
**Goal.** Ctrl+Enter on an empty prompt box continues a stopped agent turn — both when the turn hit its step/tool-call limit and when Relay was closed (or the pane restarted) mid-turn and the conversation was resumed. Non-empty box, and busy agent, behave exactly as today.

**Findings (verified).**

- Continue already exists and is agent-native: `Pane::continueTurn(bool slowPath)` (`src/Pane.h:8296`) sends the prompt `Continue` via `submitAgent()` and clears `m_limitReached`. Reached today from `/continue` (`src/Pane.h:7593`), the `relay://continue` terminal link (`src/Pane.h:2134`, `src/WindowManagerImpl.h:258`) and palette `agent.continue` (`src/RelayWindow.h:1139`). `agent.continue` has **no default key** (`src/Keymap.h:359`).
- Ctrl+Enter is bound to `agent.interrupt` (`src/Keymap.h:291–292`: Ctrl+Return, Ctrl+Enter, Ctrl+Alt+Return, Ctrl+Alt+Enter) → `Pane::interruptAgentWithPrompt()` (`src/Pane.h:906`). On an **empty** box it only prints a status ("Type a prompt first.", `Pane.h:910`; busy variant at 914) — the idle-empty slot is free, no rebinding needed.
- Limit case: `m_limitReached` is set on `done {stop_reason: "limit"}` (`Pane.h:8319`), cleared on `ready` (`Pane.h:8348`) and in `continueTurn` (`Pane.h:8298`). Busy predicate: `agentBusy()` (`Pane.h:674`).
- Restart case: a restored pane sends `resume {session_id}` (`Pane.h:4099–4108`); the worker replies `state_loaded` (handled at `Pane.h:6500`). The session file already records what is needed: each turn has a checkpoint item whose `ended` stamp is written by **every** end path (`CheckpointStore.end_turn`, `backend/relay_core/checkpoints.py:56`, called from `Agent._end_turn` for done/cancelled/failed — `backend/relay_core/agent.py:1365/1436/1458/2164`), and a running turn autosaves every 10 s (`MID_TURN_SAVE_S`, `agent.py:50`). So a turn killed by a close is in the file with no `ended`. `conv_index.session_unfinished()` (`backend/relay_core/conv_index.py:700–709`) already implements "last checkpoint lacks `ended`, in a session that carries `ended` at all" (legacy guard) — but ORs open todos in, so the pure predicate must be split out. The worker's one-time `ready` (`backend/worker.py:99`) precedes `state_loaded`, so clearing on `ready` cannot wipe the new flag during restore.

**Steps.**

1. **Worker, predicate:** factor the cut-off condition out of `conv_index.session_unfinished()` into `turn_left_open(data)` (exact condition at `conv_index.py:709`: `items and any(item.get("ended") is not None …) and items[-1].get("ended") is None`); `session_unfinished` keeps its semantics by ORing it in.
2. **Worker, event:** `Agent.resume()` (`backend/relay_core/agent.py:2506–2512`) adds `turn_open: turn_left_open(data)` to the `state_loaded` payload, computed from the loaded dict before `_apply_session`. Document the field in `docs/AGENT-SESSIONS-PROTOCOL.md` beside `resume`/`state_loaded` (§5, ~line 90, and the `state_loaded` field list at ~line 432).
3. **GUI, flag:** new member `m_turnCutOff` beside `m_limitReached` (`src/Pane.h:15211`). Set it in the `state_loaded` handler (`Pane.h:6500`) from `event.value("turn_open")`; clear it everywhere `m_limitReached` clears — `continueTurn` (`Pane.h:8298`), `ready` (`Pane.h:8348`) — and set it false on every `done` (`Pane.h:8319`).
4. **GUI, routing:** in `interruptAgentWithPrompt()` (`Pane.h:906`), before the existing empty-box status: empty text, `!agentBusy()` and `(m_limitReached || m_turnCutOff)` → `continueTurn(); return;`. The busy-empty and non-empty branches are untouched (only caller: `RelayWindow.h:1143`).
5. **Surfaces tell the truth (WARP.md hint rule):** `agent.interrupt`'s Keymap description (`Keymap.h:291`) gains "on an empty box, continue a stopped turn"; `agent.continue`'s (`Keymap.h:359`) names the empty-box path (it stays unbound, so the binding is not duplicated). The slow-path hint in `continueTurn` (`Pane.h:8300–8305`, id `continue.slow`) currently falls back to "Next time: /continue in the prompt box" because `agent.continue` has no key — build it from `agent.interrupt`'s first binding instead ("Next time: Ctrl+Enter on an empty prompt box"), so `/continue`, the ▸ Continue link and the palette row all teach the key. Match the palette fallback label at `RelayWindow.h:2905`.

**Risks.**

- Remote panes have their own send-now path (`src/RemotePane.cpp:1894`); this change is desktop-pane only — check remote Ctrl+Enter is unaffected.
- Guest harness panes: a resumed harness session goes through the same `resume`/`state_loaded`, and `Continue` is typed into the guest TUI exactly as `/continue` does today (protocol §26); no special-casing, but verify once by hand.
- A turn stopped with Esc has `ended` stamped (cancelled end path), so it will not offer continue — intended. Sessions older than the `ended` field never mark (legacy guard) — intended.
- Scope decision (no owner input needed): the key continues only a *stopped/cut-off* turn, not any finished conversation; otherwise the empty-box status stays "Type a prompt first."

**Verify.**

- Build with `scripts/relay-build` (never bare cmake).
- Python: extend the unfinished-session tests (`tests/test_conv_index.py:158–175`) for `turn_left_open`; add a resume test near the session tests — save a session whose last checkpoint item lacks `ended` (fixture pattern at `test_conv_index.py:164`), `resume()` → `state_loaded["turn_open"] is True`; completed and legacy sessions → false. Run targeted: `python3 -m pytest tests/test_conv_index.py -k unfinished` plus the new resume test.
- C++: drive a Pane with the existing harness pattern (`tests/subagents_test.cpp:21`, `tests/requests_test.cpp`): feed `done {stop_reason: "limit"}` with an empty editor, call `interruptAgentWithPrompt()`, assert a `Continue` submit was sent and no "Type a prompt first." status; same after feeding `state_loaded {turn_open: true}`; and assert nothing is sent after a plain `done {stop_reason: "end"}`. Run that one case with `ctest --test-dir build -R <its name>`.
- Live, under Xvfb with an isolated `XDG_CONFIG_HOME`: run a turn into the step limit, empty the box, Ctrl+Enter → it continues; kill Relay mid-turn, reopen (layout restore), Ctrl+Enter on the empty box → `Continue` is sent; `/continue` once → the "Next time: Ctrl+Enter on an empty prompt box" hint appears.

## QA checklist
Verified by the implementer (see `docs/qa_evidence/2026-09-20-ctrl-enter-continue/README.md`); a verifier re-checks on a fresh build:

- [ ] Step-limit stop: empty prompt box, Ctrl+Enter → the prompt `Continue` is sent (no "Type a prompt first.").
- [ ] Cut-off restart: kill Relay mid-turn (after the turn's first tool step, so the autosave wrote it), relaunch, wait for the "Session loaded" line → Ctrl+Enter on the empty box sends `Continue`.
- [ ] By-design negatives: a normally finished turn, an Esc-stopped turn, and a turn killed before its first autosave all keep "Type a prompt first." — only stopped/cut-off turns continue.
- [ ] Non-empty box unchanged: Ctrl+Enter sends now (and interrupts a busy agent), never continues.
- [ ] Slow paths teach the key: `/continue`, the ▸ Continue link and the palette row name `Ctrl+Return`/`Ctrl+Enter` (the Keymap's first binding for the same key).
- [ ] A remote pane's Ctrl+Enter send-now is unaffected (remote path untouched; spot-check).
- [ ] A resumed guest-harness pane: Ctrl+Enter types `Continue` into the guest TUI as `/continue` does.
