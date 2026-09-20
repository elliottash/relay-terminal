---
id: 265N
type: work
status: planned
labels: [bug]
rank: zzzi
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Cleanup audit follow-ups: judgment-call dead code, deprecated spike alias, unscanned areas

## Request
look for cleanup opportunities

## Tasks

- [ ] Decide BoardModel::allLabels()/allIds() (src/BoardModel.cpp:404,415) — uncalled; keep as library API or remove <!-- t:6d -->
- [ ] RemoteShare::stopAll() (src/RemoteShare.cpp:228) has no call site — check whether quit leaks share sessions; wire it into shutdown or remove <!-- t:fj -->
- [ ] Remove RELAY_BUILD_ENGINE_SPIKE deprecated alias (CMakeLists.txt:198) and consider renaming the relay-vterm-spike binary (engine/CMakeLists.txt:107) <!-- t:yk -->
- [ ] Delete docs/ENGINE-SPIKE.md redirect stub if nothing links to it, or keep and leave as-is <!-- t:p5 -->
- [ ] Verify engine/pty/PtyUnix.cpp:~395 post-EOF waitpid(WNOHANG) loop has a sleep/backoff <!-- t:6s -->
- [ ] Verify src/Voice.cpp QProcess error-signal connections (Voice.h:118) <!-- t:tp -->
- [ ] Second audit pass over unscanned areas: RelayWindow methods in main.cpp, unused #includes in largest files, unused config keys, engine/ internals (GhosttyCore, TerminalSession, TerminalView beyond noted items), Python dead code in backend/remote/rendezvous <!-- t:v6 -->

## Plan
**Goal** — Close out the seven cleanup-audit follow-ups: two are already verified clean (no code change), three are small removals/wiring, one is a docs judgment call, and the last is a scoped second audit pass whose substantial findings become their own cards.

**Findings** (verified against the tree while planning)

1. `BoardModel::allLabels()` is **not** dead — the audit was stale: it is called at `src/BoardPane.cpp:3204` (label chips). `Model::allIds()` (`src/BoardModel.cpp:1347`, declared `src/BoardModel.h:338`) has no caller in `src/` or `tests/`. So: keep `allLabels`, delete `allIds`.
2. `RemoteShare::stopAll()` (`src/RemoteShare.cpp:490`) has no call site. Today quit does leak, softly: `RemoteShare` is a function-local static whose destructor is `= default`; the sidecar `QProcess` is killed by its destructor at static destruction, so no `unpane`/`stop` lines are ever sent and the hub learns only from the websocket dropping — guests watch a frozen pane until their timeout. The right fix is wiring, not removal: call `stopAll()` from the existing `aboutToQuit` handler in `src/main.cpp` (~line 328, where `gui_quit` is logged and layout is saved). Because `QProcess` buffers writes, `stopAll()` should flush — add `m_process->waitForBytesWritten(500)` (and optionally a short `waitForFinished`) after the `stop` line, or the graceful stop can be lost exactly when it matters.
3. `RELAY_BUILD_ENGINE_SPIKE` is declared at `CMakeLists.txt:435`, consumed at `engine/CMakeLists.txt:106` (`if(RELAY_BUILD_ENGINE OR RELAY_BUILD_ENGINE_SPIKE)`), and mentioned in `docs/ENGINE.md:22`. No script passes it. Safe to remove all three. The `relay-vterm-spike` binary name (`engine/CMakeLists.txt:110`) lives in `engine/main.cpp` (usage text, window title), comments in `engine/scripts/gui/{folds,perf-cat,scenarios}.sh` (the scripts take the launcher as an argument, so nothing breaks mechanically), and docs (`docs/ENGINE.md`, `docs/ARCHITECTURE.md:2725`, `docs/ENGINE-PERF.md`). Historical `docs/qa_evidence/` references stay as they are.
4. `docs/ENGINE-SPIKE.md` is a 4-line redirect stub, but it has five live inbound links: `docs/README.md:65`, `docs/ROADMAP.md:72`, `docs/TERMINAL-ENGINE-OPTIONS.md:3`, `docs/VALIDATION.md:267`, `docs/VALIDATION.md:318`. Recommendation: repoint all five to `ENGINE.md` (its "History: the spike" section) and delete the stub, rather than keeping a stub whose only job is being linked.
5. `engine/pty/PtyUnix.cpp` post-EOF `waitpid(WNOHANG)` loop (~line 417): **already has backoff** — it blocks in `select(m_wake[0]+1, …, 50 ms)` between polls and wakes early on stop, falling back to the destructor's `reapLater` (100 ms × 30, then SIGKILL). No change needed; tick task `t:6s` with this as the evidence.
6. `src/Voice.cpp` QProcess connections: **already correct** — `finished` (line 282) and `errorOccurred` (line 284) are both connected, and both lambdas plus `Capture::finish()` guard on `m_recording`, so the error+finished pair cannot double-finish. No change needed; tick task `t:tp`.
7. Second audit pass: open-ended. Scope it to read-only inspection plus reporting; do not fix inline.

**Steps**

1. Delete `Model::allIds()` from `src/BoardModel.cpp` and `src/BoardModel.h`. Tick task `t:6d` (text already says "keep or remove"; the keep half is answered by `BoardPane.cpp:3204` using `allLabels`).
2. In `src/RemoteShare.cpp`, make `stopAll()` flush (`waitForBytesWritten(500)` after the final sends). In `src/main.cpp`'s `aboutToQuit` handler, call `RemoteShare::instance().stopAll();` before the saves. Tick `t:fj`.
3. Remove the `RELAY_BUILD_ENGINE_SPIKE` option from `CMakeLists.txt`, drop `OR RELAY_BUILD_ENGINE_SPIKE` from `engine/CMakeLists.txt`, and remove the alias sentence from `docs/ENGINE.md:22`. Tick the alias half of `t:yk`.
4. Rename `relay-vterm-spike` → `relay-engine-harness` (target, `engine/main.cpp` usage/window title, comments in `engine/scripts/gui/*.sh`, and docs `ENGINE.md`, `ARCHITECTURE.md`, `ENGINE-PERF.md`; leave `docs/qa_evidence/` untouched). Finish `t:yk`. *(Owner may veto the rename — see Risks; if vetoed, skip this step.)*
5. Repoint the five `ENGINE-SPIKE.md` links (`docs/README.md:65`, `docs/ROADMAP.md:72`, `docs/TERMINAL-ENGINE-OPTIONS.md:3`, `docs/VALIDATION.md:267,318`) to `ENGINE.md` and delete `docs/ENGINE-SPIKE.md`. Tick `t:p5`.
6. Tick `t:6s` and `t:tp` as done with the verification notes above — no code change.
7. Second audit pass (`t:v6`): inspect the listed areas — RelayWindow methods in `src/RelayWindow.h`/`main.cpp` for uncalled methods, unused `#include`s in the largest files (`main.cpp`, `Pane.h`, `BoardPane.cpp`), unused QSettings/config keys, engine internals (`GhosttyCore`, `TerminalSession`, `TerminalView`), and Python dead code in `remote/` (note: the path is `remote/gui_host.py` etc., not `backend/remote/rendezvous` — confirm what exists with `list_directory`). Post each substantive finding as a `board_comment` on this card and file a bugs-tab card for anything real; fix nothing in this pass beyond the trivially safe (e.g. a clearly unused include). Tick `t:v6` when the pass is reported.
8. Build and run targeted tests, then land per the workflow (`scripts/relay-build`, `scripts/land.py`), moving the card to needs-verification with evidence and a QA checklist.

**Risks / decisions for the owner**

- **Binary rename (step 4):** `relay-vterm-spike` appears in historical QA scripts and docs that reference the old name; a rename touches a name people may type from memory. Recommendation: rename to `relay-engine-harness` — the "spike" ended months ago — but this is the one judgment call worth an owner veto.
- **`stopAll()` flush (step 2):** `waitForBytesWritten` runs on the GUI thread during quit; the 500 ms cap bounds it, and quit already does saving work there, so this is acceptable — but if the sidecar is wedged, quit takes up to ~0.5 s longer.
- **Deleting `allIds()` (step 1):** it is a plausible future API (a card-id picker), but it is one `m_cards.keys()` call to re-add; removing keeps the audit honest.
- The stub deletion (step 5) changes five docs; each repoint is one line and reviewable.

**Verify**

- `scripts/relay-build` succeeds; `ctest --test-dir build -R board` (or the BoardModel/BoardPane test target) and `ctest --test-dir build -R relay-engine-tests` pass.
- For step 2: run Relay under Xvfb with an isolated `XDG_CONFIG_HOME`, share a pane (or stub the sidecar), quit, and confirm the sidecar receives `unpane`/`stop` before exit (its log or a `RELAY_REMOTE_*` hook); confirm no `QProcess: Destroyed while process is still running` warning on quit.
- For steps 3–4: configure a fresh build dir with `-DRELAY_BUILD_ENGINE=ON` and confirm the harness builds under its new name and `grep -rn ENGINE_SPIKE CMakeLists.txt engine/ docs/` is empty.
- For step 5: `grep -rn ENGINE-SPIKE docs/` returns no live links (qa_evidence may remain).
- For step 7: the card thread carries the second-pass findings list, and each real fault has its own bugs-tab card.
