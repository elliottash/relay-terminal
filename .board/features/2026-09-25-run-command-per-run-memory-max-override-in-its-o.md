---
id: WBDX
type: work
status: needs-verification
labels: [feature, isolation, tools]
assignee: agent
implemented_by: glm/glm-5.3
session: bdee80e5-afec-4716-ac57-b9f6bf76e173
blocked_by: [ZPWT]
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: terminal pane, this session
links: {plans: [], commits: ['0bfe33ee8163', 342c7280246b], evidence: [docs/qa_evidence/2026-09-25-zpwt-pane-memory-caps/], related: [], github: null}
---
# run_command per-run memory_max override in its own scope

## Issue
Add an optional memory_max argument to the agent run_command tool: the worker starts that one command in a child scope under app-relay.slice with the requested MemoryMax, so a job such as model training can request its own bound without escaping the machine ceiling. Agent panes keep DBUS_SESSION_BUS_ADDRESS removed (per #Y4RX), so the scope is created by the worker, not by the command.

> Part of the approved plan on #ZPWT: \"Step 3 is its own card\" — a per-run memory bound for agent-run commands.
> — elliott · [session:9a7210193ea2463396bd027ff72b34f1](relay://session/9a7210193ea2463396bd027ff72b34f1) · 2026-09-25

## Execution Summary
- Implemented together with #ZPWT in `0bfe33ee` (see that card's summary; splitting the commits would have duplicated the jobs.py/tools.py plumbing).
- `run_command` accepts `memory_max` (\d+[KMGT]? or infinity, local commands only): `backend/relay_core/jobs.py:scoped_argv()` runs that one command under `systemd-run --user --scope --quiet --collect --slice=app-relay.slice -p MemoryMax=<bound> -p OOMPolicy=continue --`, so the bound stays inside the machine ceiling; `--scope` execs in place, keeping the worker's pipes and process group. `ssh` jobs refuse it (the bound would be meaningless remotely). Sizes are validated at prepare time (`tools.py`) and again at spawn; bad sizes surface as ordinary tool errors before any preview.
- No D-Bus address is restored to the pane: the gate on #ZPWT proved sd-bus reaches the user manager through `$XDG_RUNTIME_DIR/bus`, so the worker creates the scope itself — the #Y4RX escape surface is unchanged.
- A SIGKILL exit on a `memory_max` run is reported as `killed_for_memory` naming the requested bound; the same counter check distinguishes pane-cap kills on plain runs.
- Windows and machines without systemd-run get a clear refusal, not a silent fallthrough.

## Tests
- `python3 -m pytest tests/test_isolation.py tests/test_jobs.py`: 34 passed, shared tree and clean export of `0bfe33ee` alike. This card's cases: `scoped_argv` wraps with the slice + bound + `OOMPolicy=continue` (mocked `shutil.which`), rejects non-sizes (`banana`, `-1G`, `8GB`), prepare-time normalization/refusal, and the live test `run_command memory_max=1G cat /proc/self/cgroup` asserting membership of `app-relay.slice`.
- Gate probe 4 on #ZPWT: a scope with its own `-p MemoryMax=300M` inside the capped slice runs a 250M job — own limit and slice ceiling coexist (`docs/qa_evidence/2026-09-25-zpwt-pane-memory-caps/gate.md`).
- Not run (left to QA): a live OOM inside a `memory_max` scope from a shipped pane; Windows refusal path (no Windows here).
- Amendment (Try-it staging): per-run scopes pin `MemorySwapMax=0` (342c7280) — staging caught a 900M hog surviving a 600M `memory.max` by swapping past it. New live test `test_memory_max_kills_only_the_command_when_passed` (700M hog under `memory_max=400M` → exit −9, `killed_for_memory`, note names the bound); 35 passed after the fix. Live Try-it capture: `docs/qa_evidence/2026-09-25-tryit-ZPWT/01-tryit-run.txt` scenario B.
