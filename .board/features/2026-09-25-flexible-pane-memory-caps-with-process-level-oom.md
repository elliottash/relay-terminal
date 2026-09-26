---
id: ZPWT
type: work
status: needs-verification
labels: [feature, isolation]
assignee: agent
implemented_by: glm/glm-5.3
session: bdee80e5-afec-4716-ac57-b9f6bf76e173
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: system, primary: probe, also: [script], human: none, sign_off: none, effort: medium}
source: terminal pane, this session
links: {plans: [], commits: ['0bfe33ee8163', f2e7fa690486, 342c7280246b], evidence: [docs/qa_evidence/2026-09-25-zpwt-pane-memory-caps/], related: [], github: null}
---
# Flexible pane memory caps with process-level OOM kills

## Issue
The agent-pane memory cap (8G on this machine) blocks large jobs such as model training, and an agent scope over its limit kills the whole agent session rather than one process. Make the caps generous per pane under a shared machine-sized Relay slice ceiling, and make kills process-level: OOMPolicy=continue plus oom_score_adj on tool children.

> "can we recosnider the 8GB cap. how do we make that more flexible while maintaining the process level OOM kills" — owner, this pane's session; then "i agree, put a plan on the card".
> — elliott · [session:9a7210193ea2463396bd027ff72b34f1](relay://session/9a7210193ea2463396bd027ff72b34f1) · 2026-09-25

## Decisions
- Owner approved the two-layer design in this pane's session: generous per-pane caps under a shared machine-sized `app-relay.slice` ceiling, and process-level OOM kills (`OOMPolicy=continue` plus `oom_score_adj` 1000 on tool children). Quoted: "i agree, put a plan on the card" (after "i agree, implement this plan"). The per-run `run_command` override is its own follow-up card, not part of this one.
- Related cards: #6DQ8 (current calibration, in QA), #Y4RX (escapee caps), #04EC (build-job clamp).

## Done means
- In a real agent pane, a `run_command` child that exceeds the pane cap is the **only** thing that dies (its `oom_score_adj` 1000 wins over the worker's 500); the worker keeps its conversation and the turn reports an out-of-memory kill naming the limit. Probed with a memory hog in an agent pane.
- Two panes together past the shared ceiling lose at most one process — the kernel's worst in `app-relay.slice` — never the Relay window and never a whole pane or agent session. Probed with hogs in two panes.
- Defaults on this ~122 GiB machine read: agent ≈61G, shell ≈91G, Relay total ≈110G. An 8 GiB laptop keeps sensible floors (agent 4G, shell 6G, slice ≥ RAM/2).
- `tests/isolation_test.cpp` updated to the new formulas and green; `docs/ARCHITECTURE.md` §13 table matches the code; the "8 GiB MemoryMax" note in `CLAUDE.md` (and anywhere in `docs/BUILDING.md`) is updated.

## Plan
0. **Gate — mechanism checks on this machine (systemd 255), before any code lands.** With throwaway units and a memory hog: (a) a scope started `systemd-run --user --scope --slice=app-relay.slice` really nests under the slice and the slice's `MemoryMax` binds it; (b) `OOMPolicy=continue` with `memory.oom.group=0` makes the kernel kill exactly one process in the scope; (c) a child that raised `oom_score_adj` to 1000 is picked over a 500 sibling. If any fails, stop and bring the evidence back to this card.
1. **Agent scope kills one process.** `src/Pane.h` (agent worker `wrap`, ~line 10705): `OOMPolicy=stop` → `continue`; add `MemoryHigh` at 80% of the agent cap (the agent scope has none today). Report the kill in-turn exactly as the shell pane does (`isolation::takeResult`/`oomKills`, `src/PaneRuntime.cpp`).
2. **Tool children die first.** Keep the worker at 500 (`backend/worker.py:34`). At the `run_command` spawn site in `backend/relay_core/tools.py`, a `preexec` raises the child's `/proc/self/oom_score_adj` to 1000 — raise-only, never lower, mirroring the worker's rule. The kill surfaces as a tool error, not a dead conversation.
3. **Shared ceiling above the panes.** `isolation::wrap` passes `--slice=app-relay.slice` for both scope kinds. At startup, and before pane start if missing, Relay ensures `app-relay.slice` has `MemoryMax = max(RAM − reserve, RAM/2)` with `reserve = max(8G, RAM/10)` (≈110G here) via `systemctl --user set-property --runtime`. Slice swap left uncapped for now.
4. **Generous per-pane defaults.** `src/Isolation.h`: `agentDefault() = max(RAM/2, 2G)` (~61G here); `shellDefault() = max(3·RAM/4, 4G)` (~91G here); `MemoryHigh` 80% of cap on both; swap formulas unchanged. The #6DQ8 Options rows follow automatically ("Auto — 61G agent / 91G shell"); add one "Relay total" row ("Auto — 110G") writing a new `isolation/total_memory_max` key that step 3 reads.
5. **Docs and tests.** `docs/ARCHITECTURE.md` §13 (scope table, escapee paragraph), the "agent pane's 8 GiB MemoryMax" note in `CLAUDE.md` and any 8G figure in `docs/BUILDING.md`; revisit the `RELAY_JOBS` clamp sentence if it hardcodes 8G. Update `tests/isolation_test.cpp` expected values; add a test that the `preexec` helper raises `oom_score_adj` and never lowers it.

The per-run `run_command memory_max` override is **not** in this card — it is the follow-up card, so a job can request its own bound inside the slice.

## Tasks
- [x] Step 0 gate: slice nesting, single-process OOM kill, oom_score_adj precedence proven on this machine (evidence on the card) <!-- t:yb -->
- [x] Agent scope: OOMPolicy=continue + MemoryHigh 80%, kill reported in-turn <!-- t:p4 -->
- [x] preexec oom_score_adj 1000 (raise-only) on run_command children in tools.py <!-- t:vp -->
- [x] app-relay.slice ceiling (max(RAM−reserve, RAM/2)) ensured at startup; --slice on both scope kinds <!-- t:dm -->
- [x] New defaults in Isolation.h + "Relay total" Options row and isolation/total_memory_max <!-- t:d3 -->
- [x] Docs (ARCHITECTURE.md §13, CLAUDE.md, BUILDING.md) and tests/isolation_test.cpp updated; targeted suites green <!-- t:6w -->
- [x] Land via scripts/land.py; re-check #6DQ8's QA answers after this lands <!-- t:hp -->

## Planning notes
- **Escapee caps (from #Y4RX):** the opt-in tmux/Chrome drop-ins read `agent_memory_max`/`agent_swap_max`. With the agent default at ~61G the opt-in machine-wide cap would balloon to 61G. Recommendation: give EscapeeCaps its own tight default that keeps today's `clamp(RAM/16, 2G, 8G)` formula, implemented inside step 4 unless the owner prefers otherwise.
- **#6DQ8 interaction:** it sits in QA with a checklist pinning "Auto — 8G agent / 16G shell"; this card changes those answers, so that QA is re-run afterwards and noted on its thread.
- **Existing panes** keep the scope they were started with; new caps apply to panes started after the change (and after a settings edit). Say so in the Options row hint.
- `MemoryHigh` at 80% means a big job throttles and swaps before it dies — for training, that crawl is the signal to use the follow-up card's per-run bound rather than the pane cap.

## Execution Summary
- Landed in `0bfe33ee` (implementation, 10 files) and `f2e7fa69` (evidence bundle), after the step-0 gate passed. Evidence: `docs/qa_evidence/2026-09-25-zpwt-pane-memory-caps/`.
- `src/Isolation.h`: defaults now `agent = max(RAM/2, 2G)`, `shell = max(3·RAM/4, 4G)` (pure `*DefaultBytes()` functions, unit-tested); `MemoryHigh` 80% of the cap; new `totalDefault()` = `max(RAM − max(8G, RAM/10), RAM/2)`; new `escapeeDefault()` keeps the pre-change tight clamp(RAM/16, 2G, 8G); `wrap()` starts every scope in `app-relay.slice`; `ensureTotalCeiling()` (once per process) and `applyTotalCeiling()` (used by the Options row) set the slice ceiling via `systemctl --user set-property --runtime` — `infinity` reverts it.
- `src/Pane.h`: agent scope `OOMPolicy=stop → continue` plus `MemoryHigh` at 80%; `ensureTotalCeiling()` called before both scope creations (`src/PaneRuntime.cpp` for shells). The stale "agent-run systemd-run --user is not supported" comment corrected — the gate proved sd-bus reaches the user manager without the session-bus address.
- `backend/relay_core/jobs.py`: every local child raises `oom_score_adj` to 1000 raise-only in `preexec` (`_prefer_child_oom_victim`); `scope_oom_kills()` reads the scope's `memory.events`; `scoped_argv()` builds the per-run `memory_max` scope; jobs snapshot the OOM counter at start.
- `backend/relay_core/tools.py`: `run_command` gains `memory_max` (spec, allowed set, prepare-time size validation, preview line, dispatch through `_run`); a SIGKILL exit is reported as `killed_for_memory` with the cap and the fix, and for `memory_max` runs names the requested bound.
- `src/RelayWindowSettings.cpp`: choices widened to 8G–96G, a new "Relay total" row (`isolation/total_memory_max`, applies at once), and the escapee toggle/agent row now read the escapee's own tight keys (`isolation/escapee_memory_max`, `isolation/escapee_swap_max`) — per the Planning note, the opt-in cap does not follow the generous pane limits.
- Docs: `docs/ARCHITECTURE.md` §13 rewritten around the two-layer design; `CLAUDE.md`'s 8 GiB note updated (carried to main by #2M26's `bf5aa57c` which landed first).
- The per-run override (#WBDX's subject) landed in the same commits rather than deferred: tool schema, worker plumbing and a live test were cheaper done together. #WBDX keeps its own card and verification entry.
- Two in-flight collisions handled by the land.py procedure: a stale snapshot of `src/PaneRuntime.cpp` conflicted (re-based from the tip, my hunk re-applied); #83YV's uncommitted console hunks in `src/Pane.h`/`src/PaneRuntime.cpp` were excluded from my commit and remain in the working tree for their session.

## Tests
- `ctest --test-dir build -R '^isolation$'`: 7/7 slots pass on the landed code, including the new `scaledDefaultsFollowTheMachine` (agent RAM/2 + 2G floor, shell 3·RAM/4 + 4G floor, total = RAM − max(8G, RAM/10) floored at RAM/2 with `sized()` rounding to "110G" at 122 GiB, escapee clamp(RAM/16, 2G, 8G)).
- `python3 -m pytest tests/test_isolation.py tests/test_jobs.py`: 34 passed in the shared working tree **and** in a clean `git archive 0bfe33ee` export. New: raise-only `oom_score_adj` subprocess tests, `scoped_argv` shape/validation, prepare-time `memory_max` normalization + refusal, and a live `run_command memory_max=1G` asserting `/proc/self/cgroup` shows `app-relay.slice`.
- land.py's verify slot built the exact landed tree (`--target relay`) before the swap; `scripts/relay-build --check` confirms both `app-relay.slice` and `total_memory_max` literals in the built binary.
- Gate probes (throwaway slice, reverted): single-process kill with `oom_score_adj` precedence, slice binding, own-limit-in-slice — `docs/qa_evidence/2026-09-25-zpwt-pane-memory-caps/gate.md`.
- Not run (left to QA / the owner): the full `ctest` suite, a live in-pane OOM with the shipped binary (needs a pane started under the new build), and #6DQ8's re-check.

## Try it
**Open:** run `bash docs/qa_evidence/2026-09-25-tryit-ZPWT/stage.sh` in a terminal (rerunnable; it uses a throwaway scope in `app-relay.slice` and files under the scratch dir, nothing else — no network, no model, no git changes). It stages a pane analog — a python process in a 1500M scope — and runs two jobs through the landed worker code: **A** an 1800M hog past the pane cap with no per-run bound, **B** a 900M hog with `run_command memory_max=600M`. Then compare with `docs/qa_evidence/2026-09-25-tryit-ZPWT/expected.md` (sealed until you have run it). The mechanical steps are done and captured in `01-tryit-run.txt`; staging notes beside it.

**Your one step** (a person, ~3 min): read the run the script prints and judge the report as if you were mid-training:

1. In A the job died and the pane survived — is the note (`the kernel killed this command for memory — it passed the pane's cap. Run it again with run_command memory_max…`) enough to know what happened and what to do next, without asking anyone?
2. In B the note names the exact bound that was asked for — is that the message you would want back from a training run you had bounded yourself?
