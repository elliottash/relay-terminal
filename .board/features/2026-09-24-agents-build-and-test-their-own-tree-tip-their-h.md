---
id: 76QW
type: work
status: needs-verification
labels: [feature, land, build, tests, architecture]
assignee: agent
implemented_by: anthropic/claude-fable-5-1 via claude-code
session: 987d2a1a-45ad-4dcd-8743-d17c68af8241
rank: zzzzzzzzzzzzzzzzzzzzz
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [da4294d6aac9, e491e05532dd, f5896f3d20d2, d88d10bc7da3, 4d2c6e23d229, 488ff0c853f7], evidence: [docs/qa_evidence/2026-09-25-land-try/], related: [SZHQ, 234Z, 8ABD, WNKN, FYEY, V52P], github: null}
---
# Agents build and test their own tree (tip + their hunks) before committing, not the shared build/: same-file overlap is continuous

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence: same-file overlap is the normal case here, not the exception.** The land registry held 750 session records on 2026-09-24. `src/Pane.h` was claimed by 93 sessions (19–26 a day on 09-21–09-24), `src/RelayWindow.h` by 78 (up to 31 a day), `docs/AGENT-SESSIONS-PROTOCOL.md` by 55, `CMakeLists.txt` by 24. On this card's own commit, `CMakeLists.txt` had four other live claimants.

**What the shared tree cost one session** (`3f4a20ad`, #234Z, roughly 40–45% of its 19.5M tokens and ~25 of 57 minutes):
- its test build broke on another session's half-written `Pane.h` edit;
- its debug prints were stripped from `Pane.h` mid-run by another session's cleanup;
- Python test failures had to be traced to another session's uncommitted `Keymap.h` work;
- the shared `build/relay` was stale at Try-it.

After landing, its tests on the landed tree (land.py's verify dir) passed first time.

**How to address.**
1. `land.py try <me> [--tests REGEX] [--target T]`: materialise tip + *this session's* hunks (the same merge `commit` does, without the swap) into a verify slot (#SZHQ pool), build and run the named tests there, and print the binary paths. Nothing lands. The slots are warm, so this is incremental.
2. Agents use `try` for their own testing instead of `build/`. The deliver skill and CLAUDE.md say so, and Relay agents get it as a tool so the build output is theirs alone.
3. Try-it staging launches the binary from the slot the card's commit built, never `build/relay` (this also covers the stale-binary lesson).
4. Slot count: with ~20 agents, 2 slots will queue. Size the pool from free disk and RAM (`RELAY_LAND_VERIFY_SLOTS` auto = min(4, free_disk/3 GB, RAM/8 GB)), and have `who` show slot wait times so the number is chosen from data.
5. Owner question, not a blocker: whether an agent's working edits should also live in a private overlay until landing. Private trees reintroduce the invisibility problem #CLAUDE.md's no-worktree rule fixed; `try` gets most of the benefit without it.

**Done means.** `land.py try` exists with tests. A session whose tree holds another session's broken edit can still build and test its own change. The deliver skill tells agents to use it.

## Done means
`python3 scripts/land.py try <me> [--tests REGEX] [--target T] [--commit SHA]` builds and tests the tree `commit` would land (tip + this session's hunks) in a #SZHQ verify slot, prints the binary paths and the tip it built against, and lands nothing — with a test in `tests/test_land.py` proving a session whose working tree holds another session's broken *unclaimed* edit still gets a green build of its own change. Agents are pointed at it: the bundled deliver skill and `CLAUDE.md` tell a session that has `begin`-claimed paths to test through `try` rather than the shared `build/`, and Try-it staging (`backend/relay_core/board_tryit_brief.md`) builds and launches the card's landed commit, not `build/relay`. Failure would show as sessions still debugging other people's edits in `build/`, a Try-it run on a stale `build/relay`, or slot queues nobody can see in `land.py who`.

## Plan
**Goal.** Let every agent session build and test exactly what its `land.py commit` would land — tip + its own hunks — in a verify slot from the #SZHQ pool, and point all agent guidance (deliver skill, `CLAUDE.md`, `RELAY.md`, Try-it) at it, so nobody's change is judged in the shared working tree or `build/` (the 40–45% of #234Z's session that overlap cost).

**Findings (re-read 2026-09-25 evening, after #HRF6, #ZPWT and #2M26 landed; the 04:00 plan's file facts hold, with these corrections and additions).**
- `scripts/land.py` has every piece `try` needs: `plan_path()` (~1265, base=snapshot / ours=tip / theirs=working copy), `build_tree()` (~1319), `materialise_tree()` (~1067, refreshes only changed blobs), the `verify_slot` flock pool (~1179, `<root>/verify-slots/<repo>-<sha8>-<n>`, `VERIFY_SLOTS` = `RELAY_LAND_VERIFY_SLOTS` or 2 at ~135), `run_verify()`/`_run_verify_in()` (~1163/1223). Reachable only inside `cmd_commit()`'s attempt loop (~1366–1640), after the digest/held-for-review gate. `cmd_who()` (~1954) prints the slot count and disk, no holders.
- **The slot configure does not match the developer build.** `_run_verify_in` runs a bare `cmake -S src -B build`; `CMakeLists.txt` sets no default `CMAKE_BUILD_TYPE`, while `scripts/relay-build` configures `-DCMAKE_BUILD_TYPE=RelWithDebInfo` (relay-build:362). Different compile flags mean ccache (#V52P) shares nothing between `build/` and the slots, every verify build is a cold compile of an unoptimised, debug-less tree, and a `try` binary would not be the kind the developer or Try-it runs. `relay_build_jobs()` (~1139) already `exec`s the relay-build script to borrow `jobs_for_environment`; the configure arguments can be borrowed the same way.
- Guidance: `WARP.md` is now `RELAY.md` (#2M26, bf5aa57c); its "Build" bullet (line 18) sends every session to `scripts/relay-build` and "Commits" (line 28) to land.py. `CLAUDE.md` § "Build through `scripts/relay-build`". The bundled deliver skill (`backend/relay_core/skills_bundled/deliver/SKILL.md`) never names land.py; `.board/POLICY.md` is generated from it by `relay_core.board.policy_text` (board.py ~3031) and must not be hand-edited.
- Try-it: `tryit_protocol._app_binary()` (tryit_protocol.py:719) returns `repo/build/relay` on Linux, app-bundle candidates on macOS, `.exe` candidates on Windows; `_platform_brief()` (737) rewrites the literal "`build/relay`" for the other platforms; `board_tryit_brief.md` lines 90–91 say "otherwise `build/relay` of this checkout". `tests/test_tryit_protocol.py` ~205–247 asserts the "build/relay is not built" wording. `stage.sh` is authored by the **verifying** session under `docs/qa_evidence/<date>-verify-<ID>/` and Try-it reuses it (brief § 2; protocol § 31.10), so the binary rule must reach the verifier's instructions as well as the Try-it brief.
- Tests: `tests/test_land.py` `Verify` (fake `TINY_CMAKE` project, `--verify-cmd` / `--verify-target tiny`); `test_many_sessions_share_a_bounded_pool_of_build_slots` asserts ≤ 2 slots with no env override, so pool auto-sizing must be pinned in `LandCase.setUp`.

**Steps.**
1. `scripts/land.py`: extract the per-attempt tree computation from `cmd_commit()` — the `for path in paths:` body through the conflict check — into `session_tree(repo, root, session, meta, args, tip) -> (plans, infos, conflicts)`; it keeps `safety()`/`excluded()` refusals and honours `--paths`/`--whole`. `cmd_commit()` calls it with **no behaviour change**; `LandingHunks`, `Selection`, `Contested`, `Verify` pass unmodified before step 2 starts.
2. Subcommand `try <me>`: default mode = tip + this session's claimed hunks via `session_tree()`, `py_compile_landed()` on `.py` paths, then `run_verify()` in a slot (`--verify-cmd`, `--tests REGEX` → `--verify-tests`, `--target T`, `--paths`). Contested or stale paths are **built anyway** (nothing lands) but the per-path stat line is printed so the session sees what it is building. `--commit <sha>` / `--tree <sha>` skips the merge and materialises that revision (post-land checks, Try-it). Prints the slot `src`/`build` paths, the binaries and the tip merged against; `--print-binary` prints only the binary path. Moves no refs, touches neither the shared index nor the working tree. Exit 5 on a failed build, 6 on failed tests (both documented in the EPILOG).
3. **Slot configure = developer configure.** `_run_verify_in` configures with the arguments relay-build uses (expose `CONFIGURE_ARGS = ["-DCMAKE_BUILD_TYPE=RelWithDebInfo"]` in `scripts/relay-build`, read through the same `exec` as `relay_build_jobs()`, literal fallback). A slot whose `CMakeCache.txt` carries another `CMAKE_BUILD_TYPE` is reconfigured once. This is what makes ccache hits cross between `build/` and the slots and makes `try`'s binary the same kind the developer and Try-it run.
4. Pool sizing and holders: `VERIFY_SLOTS` → `verify_slots()` — env override wins, else `max(1, min(4, free_disk_GB // 3, MemAvailable_GB // 8))` (`shutil.disk_usage` on the land root, `/proc/meminfo`, platform fallback 2; #ZPWT caps an agent pane at RAM/2, so the RAM term is read from the cgroup limit when there is one, the way `relay_build_jobs()` does). `verify_slot.__enter__` writes `{pid, session, since, tree}` JSON into `<slot>.lock`; `cmd_who()` prints per-slot holder, hold age and last tree built. `LandCase.setUp` sets `RELAY_LAND_VERIFY_SLOTS=2`.
5. Guidance: `CLAUDE.md` § "Build through `scripts/relay-build`" and `RELAY.md`'s "Build" bullet — once a session has `begin`-claimed its paths it builds and tests through `land.py try`; `scripts/relay-build` stays for unclaimed or tree-wide builds. Deliver skill step 5 gains a "build and test your own tree" bullet before the landing bullets; regenerate `.board/POLICY.md` from it.
6. Try-it never serves a stale binary: `_app_binary(repo, commit=None)` — when the card's newest `links.commits` entry is known, the binary is the path `land.py try --commit <sha> --print-binary` returns (a warm slot, incremental); `build/relay` stays the named fallback and the staging note says which was used. Brief lines 90–91 and the verifier-side staging instructions (find them with `rg "verify-<ID>" backend/relay_core/*.md docs/AGENT-SESSIONS-PROTOCOL.md`) say "the card's landed commit built by `land.py try --commit`, else `build/relay`"; `stage.sh` resolves the binary on its first line the same way; `_platform_brief()` rewrites and `tests/test_tryit_protocol.py` assertions follow.
7. Tests: a `Try` class in `tests/test_land.py` — (a) the #234Z scenario: the working tree holds a broken edit in a path this session never claimed, and `try`'s fake-compile verify is green; (b) `--commit <sha>` builds that revision; (c) `--tests` drives `ctest -R`; (d) refs, shared index and working tree byte-identical after `try`; (e) intake paths refused; (f) a slot configured with the wrong build type is reconfigured once; (g) `--print-binary` prints exactly one path — plus a `verify_slots()` unit test with injected disk/RAM values, `Who` assertions for slot-holder lines, and the brief-string assertion in `tests/test_tryit_protocol.py`. Update `scripts/land.py --help`.
8. Land through the tool this card extends (`begin`, then `commit` naming #76QW); evidence under `docs/qa_evidence/<date>-land-try/` (a real `try` run on this checkout, `who`'s slot lines, `ccache -s` before/after showing hits from `build/`).

**Orchestration.** Steps 1–4 and the `tests/test_land.py` half of 7 are one file plus its tests: sequential, main agent only. Steps 5–6 are prose plus `tryit_protocol.py`, files land.py never touches: one docs subagent in parallel once step 2 fixes the CLI shape; it also writes the `tests/test_tryit_protocol.py` assertion.

**Risks.**
- Extracting from `cmd_commit()` can regress landing itself — step 1's no-behaviour-change rule plus the untouched existing tests are the guard.
- Step 3 reconfigures every existing slot once, which costs one full build per slot the first time (two today). One-off, and ccache from `build/` now helps rather than hurts.
- A `try` result describes one tip; another session's landing invalidates it minutes later. Printing the tip sha makes staleness visible; it cannot eliminate it.
- Four warm slots ≈ 2× the disk of two (#SZHQ cleaned up 153 GB); auto-sizing from free disk plus `who`/`gc` accounting keeps it bounded.
- Try-it's `stage.sh` now builds before launching; on a cold slot that is minutes. Name the wait in the section.
- **Owner question 1** (planning note 5): private working overlays until landing? Recommendation: **no** — `try` isolates building and testing, and private trees reintroduce the invisibility the no-worktree rule fixed.
- **Owner question 2**: first-class Relay agent tool (`land_try` in `backend/relay_core/tools.py`) now, or skill text first? Recommendation: text first; file the tool separately if sessions fumble the CLI.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_land tests.test_tryit_protocol` green; a real `python3 scripts/land.py try <me> --tests land` on this checkout after `begin`-claiming `scripts/land.py`; `grep CMAKE_BUILD_TYPE <slot>/build/CMakeCache.txt` shows RelWithDebInfo; `python3 scripts/land.py who` shows slot holders and the auto-chosen count; a follow-up `land.py commit` still lands normally (the extraction changed nothing).

## Decisions
- 2026-09-25, owner: **no private working overlays** ("1 no separate trees"). Agents edit the shared checkout; `land.py try` is where they build and test.
- 2026-09-25, owner: **`land_try` is a first-class Relay agent tool now** ("2 tool"), not text first. Step 9 below.

## Tests
- `tests/test_land.py` — 113 passed (class `Try`: broken unclaimed edit still builds, `--commit`, ctest failure exits 6 vs compile 5, refs/index/working tree byte-identical, intake refused, wrong build type reconfigured once, `--print-binary` one line; `verify_slots()` unit tests; `Who` slot lines)
- `tests/test_tryit_protocol.py` — 37 passed (binary from `land.py try --commit` with a landed commit; nothing shells out without one)
- `tests/test_tools.py` — 57 passed (`LandTryTests`: argv, session default, parsed fields, exit 5/6, refusals, tool listed)
- `manual: docs/qa_evidence/2026-09-25-land-try/README.md`

## Execution Summary
Four commits by Opus subagents of pane 987d2a1a, all landed through `scripts/land.py`, plus two evidence commits.

- **`f5896f3d` land.py** (`scripts/land.py`, `scripts/relay-build`, `tests/test_land.py`): `cmd_commit`'s per-attempt merge is extracted into `session_selection()` + `session_tree()` with no behaviour change (100 tests unchanged before the next step). New `land.py try <session> [--paths] [--tests REGEX] [--target T] [--verify-cmd] [--commit SHA | --tree SHA] [--print-binary]` builds tip + this session's claimed hunks (or a given revision) in a verify slot and prints `tip`, per-path stat lines, `src`, `build`, `binary` (only the binary with `--print-binary`); exits 5 on a failed build, 6 on failed tests; moves no refs and never touches the working tree. The slot configure now uses `scripts/relay-build`'s `CONFIGURE_ARGS` (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`, read through the same exec as the job cap) and a slot configured with another build type is reconfigured once, so ccache hits cross between `build/` and the slots and `try`'s binary is the developer's kind. `VERIFY_SLOTS` became `verify_slots()` (env override, else `max(1, min(4, free_disk_GB // 3, mem_GB // 8))` with the cgroup limit when one applies); each `<slot>.lock` records `{pid, session, since, tree}` and `who` prints holder, hold age and last tree per slot.
- **`da4294d6` guidance and Try-it**: `CLAUDE.md` § "Build through scripts/relay-build" and `RELAY.md`'s Build bullet send `begin`-claimed work to `land.py try`; the deliver skill's Run step gains "build and test your own tree" and "land your claims before the move" (for #FYEY's gate), and `.board/POLICY.md` is regenerated from it. `tryit_protocol._app_binary(repo, commit)` builds the card's newest landed commit with `land.py try tryit --commit <sha> --print-binary` and launches that path, falling back to `build/relay` and saying which; the Try-it brief, the verifier-side staging rule in `docs/AGENT-SESSIONS-PROTOCOL.md` and `stage.sh`'s first line follow.
- **`e491e055` `land_try` agent tool** (`backend/relay_core/tools.py`): `{session?, tests?, target?, paths?, commit?}`; session defaults to the pane's `RELAY_SESSION_TOKEN` (its auto-begin land session from #WNKN); runs `land.py try` through the run_command job path with `command_env()` and returns `{ok, exit_code, tip, src, build, binary, output}`.
- Owner decisions honoured: no private working overlays; the tool now rather than text first.

Evidence: `docs/qa_evidence/2026-09-25-land-try/README.md` — a real `try qw76-land --tests land` on this checkout (exit 0, 267 s, `ctest -R land` green, slot lines in `who`), and the whole-card test runs on a clean export of 68776e05. Known, not this card's: the tool list is 455 bytes over the size budget in `tests/test_system_prompt.py` after `land_try` (noted on #K54A).
