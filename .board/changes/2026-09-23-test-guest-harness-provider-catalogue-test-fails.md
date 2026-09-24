---
id: JXFT
type: work
status: planned
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: pane 2, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [V1VM], github: null}
---
# test_guest_harness_provider catalogue test fails when run after the codex harness tests

## Issue
Unrelated fault noticed while landing #V1VM: tests.test_guest_harness_provider.CatalogueTests.test_the_first_presets_answer_does_not_wait_for_codex fails when the module runs together with tests.test_guest_harness_codex (both from tests/), but passes alone and in smaller combinations. Measured on the committed tree with no local changes: `git archive HEAD backend tests | tar -x -C /tmp/pristine && PYTHONPATH=/tmp/pristine/backend python3 -m unittest tests.test_guest_harness_provider tests.test_guest_harness_codex` → FAILED (failures=1) on 2026-09-24, main at ca195bfa: rows["guest:codex"]["models"] is non-empty where the test expects []. Looks like module-level state leaking across test modules (a cached installation/adapter or fake harness registry), i.e. test-order dependence, not a product fault.

## Done means
A background catalogue scan that was already in flight when `guest_harness_provider.reset_catalog()` runs can never land afterwards: it writes nothing to `_catalog`/`_login`, does not set `catalog_ready`, and does not fire the catalog listener. The reported failures are gone and stay gone: `python3 -m unittest tests.test_guest_harness_provider tests.test_guest_harness_codex` and `tests.test_guest_harness_provider tests.test_model_switch` pass 10/10 runs each on a clean export of main, where before they failed intermittently with `rows["guest:codex"]["models"]` non-empty (or a full real catalogue) in `test_the_first_presets_answer_does_not_wait_for_codex`. Failure looks like: that test (or a neighbour) again seeing catalogue rows it did not scan for, i.e. real `codex debug models` output reaching a test that mocks `_read_codex_catalog`.

## Plan
**Goal.** Make the module-global catalogue scan in `backend/relay_core/guest_harness_provider.py` safe against `reset_catalog()`: a scan thread started before a reset must never write its results afterwards. This removes the test-order dependence reported in the issue (and seen again while delivering #Q8TM) without changing any product behaviour.

**Findings.**
- `backend/relay_core/guest_harness_provider.py`: `start_catalog_scan()` (line ~492) spawns a daemon thread `scan()` that calls the real `_read_login_status()` / `_read_codex_catalog()` (subprocesses of a second or more each, `CODEX_CATALOG_TIMEOUT`) and then, unconditionally, writes `_login`, `_catalog["codex"]`, sets `catalog_ready` and calls `_catalog_listener`. `_account_login()` (line ~447) writes `_login` the same way.
- `reset_catalog()` (line ~544) clears `_catalog`, `_catalog_started`, `_login` and `catalog_ready` — but does nothing about a scan thread already running. Because `_catalog_started` is cleared, the next `preset_rows()`/`guest_models()` starts a *second* scan while the first is still in its subprocess.
- The leak: an earlier test (in `tests/test_guest_harness_provider.py` itself, or a scan left over from another module in the same process) starts a scan with a real or mocked installation; the thread reaches `_read_codex_catalog` after the test's `mock.patch` has exited, runs the real `codex debug models`, and lands its rows *after* the next test's `setUp` has called `reset_catalog()`. That is exactly the measured symptom: `rows["guest:codex"]["models"]` non-empty, once even `['gpt-6-sol', 'gpt-5.6-terra', 'gpt-6-astra', 'gpt-6-luna']` where the mock returns only `gpt-6-astra`.
- Nothing in `backend/` calls `reset_catalog()` outside tests (only tests and this card's target do), so a non-blocking fix is preferred over joining the scan thread (a join could stall a test for the full catalog timeout).

**Steps.**
1. Reproduce once on a clean export to confirm the current diagnosis: `git archive HEAD backend tests | tar -x -C /tmp/jxft && cd /tmp/jxft && PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider tests.test_guest_harness_codex` (repeat up to ~10 times; it is timing-sensitive).
2. In `backend/relay_core/guest_harness_provider.py` add a scan generation counter, guarded by the existing `_catalog_lock`:
   - Module global `_catalog_generation = 0` next to `_catalog_started`.
   - `start_catalog_scan()`: when it wins the `"scan"` slot, read `gen = _catalog_generation` under the lock and close over it.
   - In `scan()`: check `gen == _catalog_generation` under the lock (a) before calling `_read_codex_catalog`, and (b) around every write — the `_login.setdefault` loop, the `_catalog["codex"] = rows` write, `catalog_ready.set()` and the `_catalog_listener` call. A stale generation returns/discards silently.
   - `_account_login()`: take the generation as a parameter and apply the same check before its `_login.setdefault` (it is called from both `scan()` and `refresh_account_logins()`; for the refresh path pass the current generation read under the lock, keeping today's behaviour there).
   - `reset_catalog()`: increment `_catalog_generation` under the lock, before clearing. Keep the docstring, noting it now also strands any in-flight scan.
3. Add a deterministic regression test in `tests/test_guest_harness_provider.py` (in `CatalogueTests`): mock `installations` as installed and `_read_codex_catalog` with a function that blocks on an event; call `ghp.preset_rows()` to start the scan; wait until the mock was entered; call `ghp.reset_catalog()`; release the event; wait for the thread to finish (poll `_catalog`/join via a second event the mock sets on return); then assert `_catalog` has no `"codex"` entry, `catalog_ready` is not set, and a listener registered before the release was never called. Also assert a *fresh* scan started after the reset still lands normally (generation guard does not strand live scans).
4. Run the fix: `python3 -m unittest tests.test_guest_harness_provider` alone, then the two reported combinations 10 times each (`tests.test_guest_harness_provider tests.test_guest_harness_codex` and `tests.test_guest_harness_provider tests.test_model_switch`), all on the working tree, expecting 0 failures. Also run the other suites that touch this module: `tests.test_guest_harness_claude`, `tests.test_guest_accounts`, `tests.test_model_switch` (full module) once each.
5. Land with `scripts/land.py` (paths: `backend/relay_core/guest_harness_provider.py`, `tests/test_guest_harness_provider.py`, plus the card file), and note the reproduction counts as evidence on this card.

**Risks.**
- A tiny race remains between the pre-call generation check and the call itself (a stale thread descheduled exactly there can still enter a mocked `_read_codex_catalog` and e.g. append to a test's `seen` list). The post-write check makes it harmless to state; the new regression test does not depend on the pre-call check alone. Joining the thread in `reset_catalog` would close it fully but risks multi-second stalls in every test — not worth it.
- `refresh_account_logins()` shares `_account_login` and the listener; keep its generation handling so its writes are *not* stranded by a `reset_catalog` that did not target it (read the generation at call time, as today there is no reset semantics there).

**Verify.** `tests_run` on the new regression test plus `CatalogueTests`; then at the terminal the 10× repetitions of both failing combinations from `## Done means`, 0 failures. Evidence: the command lines and counts, posted on the card.
