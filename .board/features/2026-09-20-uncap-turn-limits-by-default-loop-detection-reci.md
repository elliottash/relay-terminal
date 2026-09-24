---
id: 2CZP
type: work
status: needs-verification
labels: [feature, agent]
assignee: agent
rank: zzzzzzzzzzzy
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [2d2a7103], evidence: [docs/qa_evidence/2026-09-20-uncap-turn-limits], related: [], github: null}
---
# Uncap turn limits by default; loop detection, recitation reminders, LLM double-check

## Issue
i would uncap both. can you research, i think there is work /advice that says, if an agent is doing a long chain of thought, long tool calls, or long turns, you could give it regular reminders that could break bad / recursive looping.

yes, file as a card. and i think i really want it uncapped by default. people want to have long agent runs over night now in the age of astra.

i agree with all 3 layers you proposed

## Decisions
- Uncap `agent/max_steps` and `agent/max_tool_calls` **by default** (overnight-long agent runs; owner, 2026-09-20). Keep the settings as a configurable backstop fuse at the clamp maxima (500 steps / 2000 tool calls), not as the working default.
- Replace the hard-stop-at-limit UX with three layers (owner agreed to all three, 2026-09-20):
  1. **Deterministic loop detection**, checked every step, zero model cost. Track `(tool, normalized-args-hash, result-hash)`; thresholds from OpenHands/Gemini CLI: same call + same result 4×, same error 3×, alternating ping-pong cycle 6×, monologue 3× (model-role messages only). On trigger, inject a short user-role message naming the pattern ("last 4 calls were X with the same error — change approach or report blocked"). After 2 ignored nudges, end the turn cleanly via the existing `stop_reason: limit` + Continue path.
  2. **Cadence recitation** every ~25 model steps or 50 tool calls, whichever first: compact reminder with the original request, open ledger items, todos, one line of recent progress (Manus todo.md recitation pattern; the ledger/todos already hold this state). Silent when nothing is open.
  3. **LLM double-check on detector trigger only** (not on a timer): a Lite-role model with a Gemini-style diagnostic prompt, including the productive-repetition whitelist (batch ops across files, incremental same-file edits, retry with variation, re-running builds after edits are NOT loops).
- Every layer stays silent when nothing is wrong (same rule as the existing 8-step stale-todo nudge, `docs/ARCHITECTURE.md:1825`).
- Not adopted: every-8192-thinking-token reminders (can't inject mid-generation; step/tool cadence covers it); timer-based LLM checks (cost, mostly say "fine").

## Tasks

- [x] Raise default max_steps/max_tool_calls to clamp maxima (or treat as backstop only); keep Options > Security rows and protocol 12.1 semantics for explicit values <!-- t:6v -->
- [x] Worker-side deterministic loop detection over (tool, args-hash, result-hash) with OpenHands/Gemini thresholds; nudge injection, 2 ignored nudges -> clean limit stop <!-- t:w7 -->
- [x] Cadence recitation reminder (~25 steps / 50 tool calls) rendered from request ledger + todos <!-- t:tx -->
- [x] LLM double-check on detector trigger via Lite role with productive-repetition whitelist prompt <!-- t:z9 -->
- [x] Update docs/AGENT-SESSIONS-PROTOCOL.md section 12 (limits + new reminder events) and docs/ARCHITECTURE.md <!-- t:e4 -->
- [x] Tests: detector unit tests (loop vs. productive batch ops), cadence reminder timing, nudge-escalation path <!-- t:k8 -->

## QA checklist

Evidence: `docs/qa_evidence/2026-09-20-uncap-turn-limits/` (README, `drive-worker.py` and `drive.sh`
reproduce it; `logs/` and `implementer-*.png` are the run).

- [ ] With nothing stored, Options › Security shows **500** and **2000** and calls them a backstop
      for a runaway turn rather than the normal stop.
- [ ] A turn that repeats one call shows two `↻ … · asked to change approach (n/2)` lines, then
      `↻ … · stopping this turn` and `‖ Stopped: repeating itself (…) · unfinished tasks stay open`,
      with `▸ Continue` still offered — and Continue picks the work back up.
- [ ] A long *productive* turn (a batch of reads or edits across many files) shows no `↻` line and
      runs past 150 tool calls and 256 steps without stopping.
- [ ] A turn past 25 model steps shows the "Still working · … recapping what is open" status; a
      short turn never does, and neither does a subagent.
- [ ] Options › Models › Advanced lists **Loop check** on the Lite tier, and a model set there
      reaches the worker (the `loop_check` role).
- [ ] An explicit `agent/max_steps` of, say, 20 still stops the turn at 20 with the old wording:
      the fuse is still configurable, only its default moved.

## Plan
### Goal
Make long turns the default — `agent/max_steps` / `agent/max_tool_calls` left at the protocol's clamp maxima as a backstop fuse, not a working limit — and replace the hard stop with the three agreed layers: deterministic loop detection with nudges, cadence recitation, and an LLM double-check on trigger. This is the ordering the card's `## Decisions` settled; develop on the fast model, hand the diff forward.

### Findings
- `backend/relay_core/agent.py:56-57` `DEFAULT_MAX_STEPS = 256`, `DEFAULT_MAX_TOOL_CALLS = 150`; `validate_turn_options` (`:131-140`) clamps `1..500` / `1..2000`; `Agent.__init__` (`:336-342`) takes both and `options()` (`:593`) reports them.
- The turn loop is `Agent.ask` (`agent.py:1246-2298`): per-turn `ctx` at `:1269-1270` (`turn_id, requests, opening, todos_touched, since_todos, no_list_note, takeover`), `steps`/`calls_used`/`over_budget_steps` at `:1303-1305`, `while True:` at `:1321`, limit check at `:1324`, the reminder injections at `:1342-1388` (`add({... "relay_kind": "note"})`), the model call at `:1369`, the tool batch at `:1398-1445`.
- Existing reminder machinery to build on: `STALE_TODO_STEPS = 8` (`:59`), `NO_LIST_TOOL_CALLS = 4` (`:63`), `MAX_COMPLETION_REMINDERS = 2` (`:58`), `todo_tool.reminder_text` / `no_list_reminder_text` (`backend/relay_core/todos.py`), `_open_items` (`agent.py:2102`), `_completion_reminder` (`:2154`).
- The hard stop: `_stop_at_limit` (`agent.py:2029-2045`) appends a note, `requests.finish_turn`, and emits `done {stop_reason: "limit", limit: {which: "steps"|"tool_calls", …}}`. The GUI keys on `stop_reason == "limit"` (`src/Pane.h:8488`, `:8491`; Continue at `:897` via `continueturn::sendNowContinues`) and `src/RequestLedger.h:127` renders the line.
- The side-call pattern for the double-check already exists: `_start_audit` (`agent.py:2160-2194`) — `side_provider(cheap=True, role="audit", max_tokens=AUDIT_MAX_TOKENS)` + `role_model(role)` on a daemon thread, emitting `request_audit`. Roles live in `backend/relay_core/roles.py` (`ROLES`, `ROLE_TIERS`, where `audit`/`chores` are the `lite` tier) and are mirrored in the GUI (`src/Pane.h:1023-1045`, `src/ModelSettings.cpp:887`).
- GUI settings: `src/Pane.h:4023-4024` sends `max_steps` (default 256, clamp 1..500) and `max_tool_calls` (150, 1..2000); the rows are in Options › Security, `src/RelayWindow.h:2497-2501` (`numberRow` at `:1473`). Docs: `docs/AGENT-SESSIONS-PROTOCOL.md` 12.1 table (`:360-361`, whose default column is already stale — 50/150 against the code's 256/150), 12.2 (`:385-397`), 12.6 (`:536-545`); `docs/ARCHITECTURE.md:1815-1830` and `:1893`.

### Steps
1. **Defaults = the fuse.** `agent.py:56-57` → `DEFAULT_MAX_STEPS = 500`, `DEFAULT_MAX_TOOL_CALLS = 2000`, commented as the backstop maxima; leave the `validate_turn_options` clamps and explicit values alone. `src/Pane.h:4023-4024` defaults 256/150 → 500/2000 (clamps unchanged). Reword the two Options rows (`src/RelayWindow.h:2497-2501`) to say they are a fuse for a runaway turn, not the normal stop.
2. **`backend/relay_core/loopdetect.py`** — new pure module (shape of `security.py` / `approvals.py`): `Call(tool, args_hash, result_hash, error)`, `Detector.observe(call)` → matched pattern or None, `nudge_text(pattern)`. Constants: same call + same result 4×, same call + error 3×, alternating cycle 6×, monologue 3× (model-role messages with no tool call). Normalize args (canonical JSON, sorted keys) and results (trimmed; volatile fields such as pids, timestamps, durations dropped) before hashing. Docstring whitelist: batch ops across files, incremental same-file edits, retry with variation, re-running a build after edits are not loops.
3. **Wire the detector into the loop.** `ctx["loop"] = loopdetect.Detector()` beside `:1269-1270`; observe each tool result in the batch (`:1398-1445`) and each assistant message with no tool call. On a trigger append the nudge (`relay_kind: "note"`, as at `:1344-1346`) and emit a `loop_detected` event; a differing call clears the pattern. Two ignored nudges → `_stop_at_limit` with `which: "loop"`, its text naming the repeated call instead of the turn limit; `stop_reason` stays `"limit"` so the Continue path and the GUI are untouched.
4. **Cadence recitation.** `RECITE_STEPS = 25`, `RECITE_TOOL_CALLS = 50`; in the same block as `:1342-1388`, once per cadence, skipped when `_open_items(ctx)` and `self.todos.open_items(include_delegated=False)` are both empty and entirely when `track_requests` is off (subagents). Text = original request, open ledger items, open todos, one line of recent progress; `relay_kind: "note"`, `recitation` event.
5. **LLM double-check on trigger.** `_start_loop_check(ctx, pattern)` beside `_start_audit` (`:2160-2194`), same daemon thread and `side_provider(cheap=True, role="loop_check", max_tokens=…)` shape; new role `loop_check` in `roles.py` (`ROLES`, `ROLE_TIERS` → `lite`, `LABELS`, `ACTIONS`) mirrored in `src/Pane.h:1023-1045` and `src/ModelSettings.cpp:887`. The prompt carries the whitelist and answers loop / productive: `loop` nudges or escalates, `productive` clears the detector, any error or timeout falls back to the deterministic verdict — a check never blocks a turn. Triggered only, never on a timer.
6. **Docs.** `docs/AGENT-SESSIONS-PROTOCOL.md` 12.1 table (defaults → 500/2000, "backstop" wording), 12.2 (limits plus the `which: "loop"` payload), and a new 12.x for the three layers naming the new `loop_detected` / `recitation` events; `docs/ARCHITECTURE.md:1815-1830` and `:1893`.
7. **Tests.** New `tests/test_loopdetect.py`: each of the four patterns fires at its threshold and not one below; the four whitelist cases do not fire; hashes are stable across key order and volatile fields. `tests/test_agent.py` (its `FakeProvider`): the cadence note appears at the 25th step and 50th call and not before, is silent with nothing open, and never reaches a subagent; a repeating fake provider gets the nudge and then `done {stop_reason: "limit", limit.which: "loop"}` after the second ignored nudge; the double-check is monkeypatched (no network) for both verdicts and for a failing call; a long turn on defaults runs past 256 steps and 150 calls. `tests/test_roles.py`: `loop_check` resolves to the Lite tier and falls back to main without a key.
8. **Evidence.** `docs/qa_evidence/2026-09-20-uncap-turn-limits/` with the driver and logs, a `## QA checklist` in the card body, and the land through `python3 scripts/land.py` (WARP.md).

### Orchestration
One main agent for steps 1-5 (they all touch `agent.py`, `roles.py` and the GUI files). A subagent may take step 6 (docs only) once steps 1-5 have fixed the wording, and one may take `tests/test_loopdetect.py` once step 2's module lands; nothing else runs in parallel, because the rest of the diff meets in `agent.py`.

### Risks
- Two choices the plan assumes — say so if you want them otherwise (asked as a question on this card):
  1. **Which model double-checks:** a new `loop_check` role (Lite tier, its own roles-modal row) rather than reusing `audit`. Recommended: a separate role — the two jobs answer different prompts, and one row would stand for both.
  2. **How the escalation reports:** keep `stop_reason: "limit"` with `limit.which: "loop"` (recommended — Continue at `src/Pane.h:897` and the `RequestLedger.h:127` line keep working unchanged) rather than a new `stop_reason` value, which needs GUI changes in two files.
- False positives on legitimate long runs (mirrors, builds, sweeps) are the real cost; the nudge comes first, the whitelist and double-check guard the escalation, and only two ignored nudges stop the turn.
- `_stop_at_limit`'s text is built from `which`: the new `"loop"` branch must keep the `limit` payload shape the GUI reads (`src/Pane.h:8488`).
- Recitation and the existing todo reminders both inject user-role notes; keep their `relay_kind` values distinct so compaction and the transcript render them apart, and keep detector state separate so a cadence note never counts as a nudge.
- Uncapped defaults leave a runaway turn bounded at 500/2000 until a nudge escalates. `docs/AGENT-SESSIONS-PROTOCOL.md:360` already disagreed with the code (50 against 256), so step 6 fixes stale text rather than changing behaviour.

### Verify
- `PYTHONPATH="$PWD/backend" python3 -m unittest tests.test_loopdetect tests.test_agent tests.test_roles -v` (targeted; the full `scripts/test.sh` only if the owner asks, WARP.md).
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: with no stored `agent/max_steps` the `options` event reports 500/2000; a scripted turn past 50 tool calls shows one recitation note and no stop; a fake provider repeating one call shows the nudge and then `done {stop_reason: "limit", limit.which: "loop"}` with Continue still offered.
- Artifacts in `docs/qa_evidence/2026-09-20-uncap-turn-limits/` (driver, event logs) with a `## QA checklist` in the card body, in the same commit as the change.
