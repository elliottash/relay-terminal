# Uncap turn limits by default; loop detection, recitation, LLM double-check (#2CZP)

Owner, 2026-09-20: *"i think i really want it uncapped by default. people want to have long agent
runs over night now in the age of astra"* — and *"i agree with all 3 layers you proposed"*.

The turn caps stop counting. What ends a turn that has stopped making progress is a detector, and
it nudges before it stops.

## What changed

- **`backend/relay_core/agent.py`** — `DEFAULT_MAX_STEPS` 256 → 500, `DEFAULT_MAX_TOOL_CALLS`
  150 → 2000: `validate_turn_options`' own clamp maxima, unchanged, so the settings are now a
  backstop fuse for a runaway turn rather than the working stop. New `RECITE_STEPS` 25,
  `RECITE_TOOL_CALLS` 50, `LOOP_CHECK_TIMEOUT_S` 20, `LOOP_CHECK_RECENT` 12. The turn's `ctx`
  carries a `Detector`, the parked `loop_pattern`, the nudge count and the recent-call lines;
  `_observe_call`, `_handle_loop`, `_loop_verdict` and `_recitation` are the four new members, and
  `_stop_at_limit` takes a `pattern=` for the loop stop.
- **`backend/relay_core/loopdetect.py`** (new, pure) — four patterns over *consecutive*
  observations: `repeat` (same tool, args and result, 4×), `error` (same tool and args, failing,
  3×), `cycle` (the last 6 calls repeat a 2- or 3-call cycle), `monologue` (3 identical model
  messages with no tool call). Arguments and results are hashed canonically with volatile fields
  (`ms`, `pid`, `elapsed`, …) dropped, so a result that differs only in how long it took is the
  same result. Also the nudge and stop wording, and `CHECK_SYSTEM` / `run_check` for the
  double-check. Its docstring argues why consecutiveness *is* the productive-repetition whitelist.
- **`backend/relay_core/roles.py`** — new `loop_check` role on the Lite tier, mirrored in
  `src/Pane.h` (`roleIds`, `roleLabel`).
- **`src/Pane.h`** — the GUI defaults become 500/2000 (clamps unchanged); `loop_detected` prints a
  line and `recitation` sets a status.
- **`src/RelayWindow.h`** — the two Options › Security rows default to 500/2000 and say they are a
  backstop, not the normal stop; the settings search finds them under "uncapped", "loop", "stuck".
- **`src/RequestLedger.{h,cpp}`** — `limitLine` gains a `which == "loop"` branch (the counts it
  would otherwise print are nowhere near their limits) and a new `loopLine` for the nudge.
- **Docs** — `docs/AGENT-SESSIONS-PROTOCOL.md` 12.1, 12.2, 12.6, 12.9 and a new 12.10, plus
  `loop_check` in 13.1; `docs/ARCHITECTURE.md` around the reminder machinery. Three stale copies
  of the old defaults (12.1 said 50/150, 12.9 said 50/150, ARCHITECTURE said 256/150) are fixed.

The escalation deliberately reports as the **existing** stop: `stop_reason` stays `"limit"` with
`limit.which == "loop"`, so the pane's Continue path and the ledger line keep working.

## Evidence

Everything here runs against `stub-provider.py`, a loopback-only OpenAI-compatible endpoint. No
account, no network, `RELAY_KEYRING=off` throughout.

### The real worker, over the protocol — `python3 drive-worker.py`

- `logs/worker-options.txt` — `configure` with no `max_steps`/`max_tool_calls` in the request;
  the `configured` event reports **500 / 2000**.
- `logs/worker-sweep.txt` — one turn, 30 distinct reads, 31 model steps: **one** `recitation` at
  step 25, no `loop_detected`, and a plain `done` with no `stop_reason`. A batch operation across
  files is not a loop, and past the old 150-tool-call cap nothing stops it.
- `logs/worker-loop.txt` — the same failing read for ever: `loop_detected` ×3 (nudge 1, nudge 2,
  then `stopping: true`) and `done {stop_reason: "limit", limit: {which: "loop", pattern: "error",
  tool: "read_file", count: 3, nudges: 2}}` at 9 steps of a 500 limit. The `loop_check` events
  show `verdict: "unknown"` — the stub cannot answer `loop`/`productive`, which is exactly the
  fallback path: an unreadable double-check leaves the deterministic verdict standing and never
  blocks the turn.

### The app, under Xvfb — `./drive.sh`

- `implementer-a-loop.png` — the repeating turn in the pane: three failing reads, then
  `↻ read_file 3 times with the same result · asked to change approach (1/2)`, three more, `(2/2)`,
  three more, `↻ … · stopping this turn`, then
  `‖ Stopped: repeating itself (read_file 3 times with the same result) · unfinished tasks stay open`
  and `▸ Continue (Ctrl+click · Ctrl+Return or /continue)` — the stop names the repetition, and
  Continue is still offered.
- `implementer-b-sweep.png` — the sweep turn beneath it: 30 tool calls, finished, no stop.
- `implementer-c-options.png` — Options › Security: *Step limit per turn* **500**, "Backstop for a
  runaway turn; then it stops with Continue".
- `implementer-notes.txt` is the OCR of each crop; `relay.log` is the run's log.

### Tests — targeted, per WARP.md

- `logs/test_loopdetect.txt` — 61 tests: every pattern at and below its threshold, five long
  whitelist runs (batch ops, incremental edits, retry with variation, build-after-edit, polling)
  each asserting its own observation count so it cannot pass vacuously, hashing, detector
  lifecycle, and the nudge/stop/parse helpers.
- `logs/test_agent.txt` — 40 tests, 8 of them new: the defaults are the maxima; a 300-step turn
  runs past the old 256/150 with no `loop_detected`; the nudge-nudge-stop path with its message
  ordering checked (no note ever separates an assistant's tool calls from their results); the
  double-check confirming, forgiving, failing and answering unreadably; the cadence at the step
  mark and at the tool-call mark; and silence on a short turn and in a subagent.
- `logs/test_roles.txt` — 58 tests, `loop_check` on the Lite tier and its step-down.
- `logs/test_requests.txt` — 43 tests; `test_default_limits` now expects 500/2000.
- `logs/ctest-requests.txt` — `requests` and `panestate` pass (the `limitLine` loop branch).
- `scripts/relay-build` green, 48 s.

## QA checklist

- [ ] With nothing stored, Options › Security shows **500** and **2000** and calls them a backstop.
- [ ] A turn that repeats one call shows two `↻ … asked to change approach (n/2)` lines and then
      `‖ Stopped: repeating itself (…)`, with `▸ Continue` still offered; Continue works.
- [ ] A long, *productive* turn (a batch of edits or reads across many files) never shows a `↻`
      line and runs past 150 tool calls.
- [ ] A turn past 25 model steps shows the "Still working · … recapping what is open" status, and a
      short turn never does.
- [ ] Options › Models › Advanced lists **Loop check** on the Lite tier, and changing it is
      honoured (the `loop_check` role reaches the worker).
- [ ] An explicit `agent/max_steps` of, say, 20 still stops the turn at 20 with the old wording —
      the fuse is configurable, only its default moved.
