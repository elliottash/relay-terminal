---
id: B9V4
type: work
status: needs-verification
assignee: agent
priority: 1
rank: zzzzzzzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: ['9628872d'], evidence: ['docs/qa_evidence/2026-09-20-model-switch-continues/'], related: [], github: null}
---
# bug: when i switched models from claude to glm, it stopped relaying and i had to…

## Issue
bug: when i switched models from claude to glm, it stopped relaying and i had to type continue

session:
eb2828d4f51d456191c55380bf2a620b

## Plan
**Goal** — A model switch accepted while a turn runs (claude → glm) must not end the turn: the model taking over is told it is taking over an unfinished request, and that request holds the turn open, so the owner never has to type "continue" by hand.

**Findings** (backend/relay_core/agent.py unless said)
- A mid-turn switch lands between steps: `Agent.ask`'s loop calls `apply_pending_model(turn_id, steps + 1, "step")` (~line 1345); `_land_switch` (:923) then swaps the provider (`set_model` :696), converts the history (`adapt_history` :2682 — GLM style needs `reasoning_content` on assistant tool-call messages) and emits `model_applied {at: "step"}`. Native→native switching is tested and works (`tests/test_model_switch.py::CrossProviderTests`).
- **Nothing tells the new model it is taking over.** The conversation it inherits ends in the old model's tool results; GLM's first reply can be a plain text wrap-up, and a text-only reply ends the turn (:1358–1371, `done` via `finish_turn`/`_end_turn`) unless the completion check finds open items.
- The completion check only fires on Relay todos: `_open_items` (:2080) counts a request mid-turn only when an open todo links to it, and a turn with no todo list is never checked (`todos.py:247`; glm-5.3 is measured to skip the list, card D8VN). So nothing holds the turn open — the pane stops relaying and the user types "continue". This matches the report.
- Reading also found a **second defect on the guest path**: a switch away from `guest:claude` mid-turn defers (`defer_model` :784 — `_pending_model` carries no follow-up), so the landing's `set_model` cannot replace an injected provider (:702 `elif not self._injected_provider`) and `guest_harness_provider.detach` (`guest_harness_provider.py`) never runs — `apply_now`'s detach (`session_protocol.py::_set_model`) is dropped on the mid-turn path. Config would say GLM while the claude harness still serves the pane.

**Steps**
1. **Diagnose the recorded session first.** `find ~/.local/share/relay/sessions -name 'eb2828d4*'` → read that JSON (`models`, the messages around the switch, the last turn) and `~/.local/share/relay/logs/worker.log` lines `model_applied` / `turn_end` for `session=eb2828d4…`. This settles whether the pane was the Anthropic preset or the Claude Code guest, and how the turn ended. If it was the guest, step 4 replaces step 3.
2. **Handoff note at the mid-turn landing.** Capture `apply_pending_model`'s return in `ask`'s loop; when it landed at `at == "step"`, `add()` a user note (`relay_kind: "note"`, house style of `_completion_reminder` / `CONTEXT_OPEN`): the model was switched mid-task (`from_model` → `model`), the request above is still open — continue it with tools until done, do not merely summarise. Add the note *after* the landing so `adapt_history` has already run.
3. **Hold the turn open across the takeover.** Mark the turn (ctx) when a switch lands at `"step"`; `_open_items` then also counts that turn's open unlinked `requires_completion` requests, so a wrap-up answer draws a completion check (≤ `MAX_COMPLETION_REMINDERS` = 2, :58) instead of ending the turn.
4. *(Guest diagnosis only)* Carry the guest detach through the deferred landing: `defer_model` stores the follow-up and `_land_switch` runs it — `guest_harness_provider.detach(agent)` before `set_model` (and `attach` for a switch onto a guest). If diagnosis says native, split this into its own bugs card instead of doing it here.
5. **Protocol doc**: one sentence in §13's switch landing bullet (docs/AGENT-SESSIONS-PROTOCOL.md ~line 42) — a switch landing at `"step"` adds a Relay handoff note and the turn's open request counts for the completion check.
6. Land per house rules: `python3 scripts/land.py begin <me> <paths>` before editing, targeted tests, `land.py commit`; evidence under `docs/qa_evidence/2026-09-20-model-switch-continues/` with a QA checklist, card to needs-verification.

**Risks**
- Steps 2–3 change when a turn may *end*, but only in turns where a switch landed mid-flight. A user who switched intending to stop may see up to two completion checks before the turn ends — recommendation: accept it (the check names what is open and Esc still stops); say so if not.
- `defer_model`/`_land_switch` changes touch the 3ES1 machinery shared with role swaps and failover — keep the follow-up additive (absent ⇒ exactly today's behaviour) and covered by the existing `ModelSwitchMidTurnTests`.
- Mid-turn switches *onto* a guest have their own defects (harness started while a turn runs; `harness://` config reaching `_provider_for` on the deferred path) — out of scope here; file a separate card if diagnosis leads there.
- No blocking question. If the owner remembers whether that pane was the Claude Code guest or the Anthropic API, it short-cuts step 1 — but the session file is authoritative either way.

**Verify**
- Extend `tests/test_model_switch.py` (blocked-first-step pattern): the new provider's first request carries the handoff note (user message, `relay_kind: "note"`); a text-only wrap-up from the new model emits `completion_check` with non-empty `open` and the turn does not end; an idle switch and a turn_end landing add no note. Guest variant (if step 4): provider replaced, harness closed, one `model_applied`.
- Run targeted: `XDG_DATA_HOME=$(mktemp -d) PYTHONPATH=$PWD/backend RELAY_KEYRING=off python3 -m unittest tests.test_model_switch -v`.
- Replay the report with the stub provider: switch mid-turn claude → glm and watch the busy line keep relaying with no typed "continue".

## Outcome
**Diagnosis (step 1)** — the recorded pane was the **Claude Code guest** (`claude -p --model sonnet`, 02:30:39), not the Anthropic preset. Turn 1 (R1, `requires_completion`) ended 02:31:10 on claude's text-only card summary with `open_items=0` — no todos, so no completion check: that is the "stopped relaying". The switch to glm landed **idle** 02:31:13 (`guest_harness_closed`, no `model_applied` — `apply_now` emits none); "continue" (R2, 02:31:24) started turn 2 on glm. So the plan's steps 2–4 all applied: the guest defect is real in code on the deferred path, and the takeover fix is what stops the same click a few seconds earlier from ending the turn. Steps 2, 3 and 4 were all done (step 4's "replaces step 3" reading would have left the card's goal unmet on guest panes — after the detach fix, glm's wrap-up text would still have ended the turn without step 3). Mid-turn switches *onto* a guest stay out of scope: noted in `issues/bug_intake.txt`.

**Landed** — a landing at `"step"` now adds a Relay handoff note after `adapt_history` has run, marks the turn so `_open_items` counts its open `requires_completion` requests (wrap-up text draws the completion check, ≤ 2, Esc still stops), and a switch off a live guest harness ends the harness at the landing (`pre_land` follow-up threaded `request_model` → `defer_model` → `_land_switch`; absent ⇒ exactly today's behaviour, so role swaps and failover are untouched). Protocol §2 records it. Tests: 17/17 `tests.test_model_switch` (incl. the stub replay of this report and the guest landing), 185/185 neighbours. Evidence: `docs/qa_evidence/2026-09-20-model-switch-continues/`.

## QA checklist
- [ ] Switch mid-turn claude → glm while a native pane is relaying: the busy line keeps relaying, the new model continues the work, no "continue" typed.
- [ ] The transcript shows `model_applied` and a short Relay takeover note.
- [ ] A plain-text wrap-up right after the switch draws a completion check naming the open request; the turn ends after at most two checks; Esc still stops at once.
- [ ] On a Claude Code guest pane, switch mid-turn to a native model: `guest_harness_closed` at the landing, the new provider serves the rest of the turn, one `model_applied {at: "step"}`, the live-guest tail stops.
- [ ] Idle switch and a turn_end landing: no note, no completion check, `applies: "now"/"turn_end"` as before.
- [ ] A role swap (Main ↔ Flash) mid-turn still lands with its own follow-up (no guest detach).
