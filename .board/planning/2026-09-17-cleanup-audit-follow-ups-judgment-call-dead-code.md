---
id: 265N
type: work
status: needs-verification
labels: [bug]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 131947ca-655f-4eef-8401-b4970c2f0a2d
rank: zzzi
created: '2026-09-17'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [6bb47ddfe4fd, a99f092e0dc2, d4b6658acc4a, b37a4777be7f, 7b2140b081a6], evidence: [docs/qa_evidence/2026-09-25-265n-cleanup/], related: [ZR3N, ZR2M], github: null}
---
# Cleanup audit follow-ups: judgment-call dead code, deprecated spike alias, unscanned areas

## Request
look for cleanup opportunities

## Done means
- Quitting Relay while remote sharing or always-on is running sends the sidecar `stop` and EOF and waits for it, so no `tailscale serve` route or tunnel outlives the GUI; failure looks like a surviving `gui_host.py` pid or a `QProcess: Destroyed while process` warning after quit.
- `RELAY_BUILD_ENGINE_SPIKE` is gone from CMake and docs, and the engine harness still builds with `-DRELAY_BUILD_ENGINE=ON`.
- `docs/ENGINE-SPIKE.md` is gone (or the owner kept it), and no live doc links it.
- The four verified no-op tasks are ticked with evidence, and the second audit pass is reported on the thread, with a card per real fault.

## Tasks

- [x] BoardModel::allLabels()/allIds() are both used in BoardPane.cpp; keep them <!-- t:6d -->
- [x] Wire RemoteShare shutdown into app quit so the sidecar can remove sharing routes and exit cleanly <!-- t:fj -->
- [x] Remove the deprecated RELAY_BUILD_ENGINE_SPIKE CMake alias; keep the harness binary name unless deliberately renamed <!-- t:yk -->
- [x] Repoint live documentation links to ENGINE.md and remove the ENGINE-SPIKE.md redirect stub <!-- t:p5 -->
- [x] PtyUnix post-EOF waitpid loop already has a 50 ms select backoff <!-- t:6s -->
- [x] Voice QProcess error and finish paths are connected and guarded against double completion <!-- t:tp -->
- [x] Run the bounded second audit pass over current RelayWindow, engine, settings, includes and remote Python paths; report findings and file real faults <!-- t:v6 -->

## Plan
**Goal** — Close out the seven 2026-09-17 audit follow-ups against the tree as it is on 2026-09-25: three are now verified no-ops, one is a real quit-path fault to wire, two are small build/docs removals, and the audit pass is a bounded, report-only sweep whose findings become their own cards.

**Findings** (re-checked 2026-09-25; the 2026-09-20 plan's line numbers and two of its conclusions are stale)

1. `t:6d` — **nothing to remove any more.** `Model::allLabels()` (`src/BoardModel.cpp:1689`, `src/BoardModel.h:449`) is called at `src/BoardPane.cpp:6267`; `Model::allIds()` (`src/BoardModel.cpp:1700`, `src/BoardModel.h:450`) is now called at `src/BoardPane.cpp:6685` (the file-watcher's "executing cards" set). `grep -rn "allIds\|allLabels" src tests` shows those two callers. The old plan's step "delete `allIds()`" would break the build.
2. `t:fj` — **still a real fault, and worse than filed.** `RemoteShare::stopAll()` (`src/RemoteShare.cpp:610`) now has one caller, `setAlwaysOn(false)` (`:523`), but quit still never reaches it: the `aboutToQuit` handler (`src/main.cpp:454-465`) only logs `gui_quit` and saves; `RemoteShare` is a function-local static (`src/RemoteShare.cpp:60-64`) with `~RemoteShare() = default` (`:93`), so its child `QProcess` is destroyed during static destruction, after `QApplication` is gone, and `~QProcess` SIGKILLs the sidecar. The sidecar's orderly path — `main_async()`'s `finally: await sidecar.stop()` (`remote/gui_host.py:1617-1621`), which on stdin EOF or a `{"t":"stop"}` line runs `Sidecar.stop()` (`:1574`) → `drop_tailnet()` / `drop_cloudflare()` / `host.stop()` — therefore never runs on quit. Since #PH0N (always-on remote service) that means a `tailscale serve` route or Cloudflare tunnel can outlive Relay pointing at a dead port, and guests see a frozen pane until the hub times out.
3. `t:yk` — alias still present: `CMakeLists.txt:651` (`option(RELAY_BUILD_ENGINE_SPIKE …)`), `engine/CMakeLists.txt:126` (`if(RELAY_BUILD_ENGINE OR RELAY_BUILD_ENGINE_SPIKE)`), `docs/ENGINE.md:22`. `grep -rln ENGINE_SPIKE scripts .github tests` is empty — nothing passes it. Binary `relay-vterm-spike` is named at `engine/CMakeLists.txt:10,130,131`, `CMakeLists.txt:650` (option description), `engine/main.cpp:2,4,79`, comments in `engine/scripts/gui/{folds,perf-cat,scenarios}.sh`, and docs `docs/ENGINE.md:35,53,395`, `docs/ENGINE-PERF.md:62-63,94-95,116,118`, `docs/ARCHITECTURE.md:3609`. `docs/ENGINE.md:53` says the name is "kept for the old scripts" — i.e. someone already chose to keep it once.
4. `t:p5` — `docs/ENGINE-SPIKE.md` is still a 4-line redirect stub. `docs/ROADMAP.md` no longer links it; live inbound links today are `docs/TERMINAL-ENGINE-OPTIONS.md:3`, `docs/VALIDATION.md:276`, `docs/VALIDATION.md:327`, plus the `## Superseded` paragraph in `docs/README.md:70-73` that exists only to explain the stub. The target section exists: `docs/ENGINE.md:509` "History: the spike (2026-09-17 morning)".
5. `t:6s` — **no change needed.** `engine/pty/PtyUnix.cpp:416-431`: the post-EOF `waitpid(WNOHANG)` loop blocks in `select(m_wake[0]+1, …, 50 ms)` between polls and returns early on `m_stop`.
6. `t:tp` — **no change needed.** `src/Voice.cpp:299` connects `finished`, `:301` connects `errorOccurred`; the error lambda and `Capture::finish()` (`:381-382`) both return early unless `m_recording`, so an error+finished pair cannot double-finish.
7. `t:v6` — not done; no later card or commit ran it (`rg --hidden -il "cleanup audit|unused include|dead code" .board` finds only this card and the 2026-09-17 quick-wins card). Paths in the task are stale: RelayWindow methods now live in `src/RelayWindow.h` (8.5k lines) and `src/RelayWindow{,Core,Models,Settings}.cpp`; the largest files are `src/Pane.h` (17.2k), `src/BoardPane.cpp` (10.0k), `src/RelayWindow.h`; engine internals are `engine/core/GhosttyCore.cpp`, `engine/session/TerminalSession.cpp`, `engine/view/TerminalView.cpp`; there is no `backend/remote/rendezvous` — the Python remote code is `remote/*.py` (29 modules). `vulture` is not installed.

**Steps**

1. Tick `t:6d`, `t:6s`, `t:tp` with the evidence in Findings 1, 5, 6 (a `note` comment each, or one comment). No code. *(parallel with everything)*
2. `t:fj`: add `RemoteShare::shutdown()` in `src/RemoteShare.{h,cpp}` — if `m_process` is running: `stopAll()`, `m_process->closeWriteChannel()` (EOF, so the sidecar's `finally` runs even if the `stop` line is lost), `waitForFinished(<cap>)`, and only then `kill()` if still running. Call `relay::RemoteShare::instance().shutdown();` from the `aboutToQuit` lambda in `src/main.cpp`, **after** `saveLayoutNow()` so a slow network teardown cannot cost the save. Log one line (`remote_shutdown clean=… ms=…`). *(parallel with 3–5; touches `src/main.cpp`, `src/RemoteShare.*` only)*
3. Add a test for step 2: in `tests/test_remote_gui_host.py`, assert that closing stdin runs `Sidecar.stop()` (drop_tailnet/drop_cloudflare called) — this pins the sidecar half. For the C++ half, extend the `sharing` or `remotesettings` ctest with a fake sidecar script (via the env var `ensureSidecar` already honours for its root, if any — check `sidecarRoot()`), call `shutdown()`, and assert the fake saw `{"t":"stop"}` then EOF and exited before the cap.
4. `t:yk` alias: delete `CMakeLists.txt:651`, drop `OR RELAY_BUILD_ENGINE_SPIKE` at `engine/CMakeLists.txt:126`, delete the alias clause in `docs/ENGINE.md:22`. Rename of `relay-vterm-spike` **only if the owner says so** (Risk Q1): then rename target + `engine/main.cpp` usage/title + script comments + the live docs listed in Finding 3, leaving `docs/qa_evidence/` and ENGINE-PERF's historical result rows as they were measured. *(parallel)*
5. `t:p5`: repoint `docs/TERMINAL-ENGINE-OPTIONS.md:3` and `docs/VALIDATION.md:276,327` to `ENGINE.md#history-the-spike-2026-09-17-morning`, remove the `## Superseded` paragraph in `docs/README.md`, delete `docs/ENGINE-SPIKE.md`. *(parallel)*
6. `t:v6`, report-only, one sub-step per area (each independent, can be split across sessions): (a) uncalled `RelayWindow` methods — list every member declared in `src/RelayWindow.h`, grep each name across `src/` and `tests/`; (b) unused `#include`s in `src/Pane.h`, `src/BoardPane.cpp`, `src/RelayWindow.h` — per include, remove it in a scratch copy and compile that TU alone; (c) unused settings keys — collect every `QSettings` key literal written in `src/` and every one read, list write-only and read-only keys; (d) engine: uncalled functions in `GhosttyCore`, `TerminalSession`, `TerminalView`; (e) Python: `pip install --user vulture` into a scratch venv (not the project env) and run `vulture remote/ --min-confidence 80`, confirming each hit by grep. Post the findings as one `note` comment on this card; file a bugs-tab card per real fault; fix nothing in this pass.
7. Build, targeted tests, land each group through `scripts/land.py` (one commit per step 2+3, 4, 5), move to needs-verification with evidence.

**Risks** — questions for the owner

- **Q1. Rename `relay-vterm-spike`?** It was deliberately kept "for the old scripts" (`docs/ENGINE.md:53`). Recommendation: keep the name, remove only the CMake alias — the rename touches ~10 files for cosmetics and breaks the name people type. Say "rename to relay-engine-harness" to override.
- **Q2. Quit wait cap for the sidecar.** `Sidecar.stop()` runs `tailscale serve` teardown and closes a Cloudflare tunnel, which can take seconds. Recommendation: 2 s cap, then SIGKILL (a leftover route is worse than a 2 s slower quit, and quit only waits when sharing/always-on is actually running). Acceptable?
- **Q3. Delete `docs/ENGINE-SPIKE.md`** (repointing 3 links) or keep the stub? Recommendation: delete; nothing is lost, the content is in `ENGINE.md`.
- Risk: step 2 runs on the GUI thread during quit; with no sidecar running it is a no-op.

**Verify**

- `scripts/relay-build` succeeds; `ctest --test-dir build -R "sharing|remotesettings|boardpane"` passes; `python3 -m pytest tests/test_remote_gui_host.py -q` passes, including the new stdin-EOF test.
- Step 2 live: start Relay under Xvfb with an isolated `XDG_CONFIG_HOME`, turn remote always-on (local-only, no tailnet), quit; the log shows `gui_quit` then `remote_shutdown clean=1`, the sidecar pid is gone, and stderr has no `QProcess: Destroyed while process … is still running`.
- Steps 4–5: `scripts/relay-build --cmake-arg=-DRELAY_BUILD_ENGINE=ON` (or a scratch build dir) builds the harness; `ctest --test-dir build -R relay-engine-tests` passes; `grep -rn ENGINE_SPIKE CMakeLists.txt engine docs` and `grep -rn "ENGINE-SPIKE.md" docs --exclude-dir=qa_evidence` are empty.
- Step 6: this card's thread holds the findings note, and each real fault has its own card linked in `links.related`.

## Decisions
Owner, 2026-09-25: “Keep relay-vterm-spike”; “2 seconds”; “Delete stub”. Keep the existing harness executable name and remove only the deprecated CMake alias. On quit, wait up to two seconds for the remote sidecar to finish route/tunnel teardown before forcing it to exit. Repoint the three live ENGINE-SPIKE links to ENGINE.md and delete the stub.

## Execution Summary
- t:fj: `RemoteShare::shutdown()` runs stopAll(), closes the sidecar's stdin (EOF) and waits inside a 2 s cap before killing; `main.cpp` calls it from aboutToQuit after the saves and logs `remote_shutdown clean=… ms=…` (a99f092e). Test: new `remoteshutdown` ctest case (in relay-consolemode-tests, the only test target that compiles RemoteShare.cpp) plus a stdin-EOF-runs-Sidecar.stop() pin in tests/test_remote_gui_host.py.
- t:yk: `RELAY_BUILD_ENGINE_SPIKE` option and its OR clause removed; `relay-vterm-spike` keeps its name (d4b6658a).
- t:p5: three live links repointed to ENGINE.md's history section; `docs/ENGINE-SPIKE.md` and README's Superseded paragraph deleted (b37a4777).
- t:6d / t:6s / t:tp: verified no-ops (see Plan findings 1, 5, 6).
- t:v6: bounded audit posted on the thread; #ZR3N filed (3 dead RelayWindow methods); #ZR2M found and closed as fixed by a99f092e.

The work was done by subagents a2 then a4; a4 failed before writing up the audit and ticking the card, and the parent session reran the audit scripts on a clean export and wrote the evidence.

## Tests
- `land.py try phone-265n --commit HEAD --tests "remoteshutdown|sharing|remotesettings|boardpane"` on main c926b960: whole tree builds, 4 suites pass (try-landed-main.txt).
- `pytest tests/test_remote_gui_host.py -q`: 52 passed.
- Clean export with `-DRELAY_BUILD_ENGINE=ON`: `relay-vterm-spike` builds (engine-alias-build.txt).
- Live quit under Xvfb, isolated XDG_CONFIG_HOME, always-on remote: `remote_shutdown clean=1 ms=985`, no surviving sidecar, no QProcess-destroyed warning (quit-check-xvfb.txt, by a2 before landing).
