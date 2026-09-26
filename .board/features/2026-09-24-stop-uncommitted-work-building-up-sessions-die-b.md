---
id: FYEY
type: work
status: needs-verification
labels: [feature, workflow, land, agents]
assignee: agent
implemented_by: anthropic/claude-fable-5-1 via claude-code
session: 987d2a1a-45ad-4dcd-8743-d17c68af8241
rank: zzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Relay pane 64de364c, 2026-09-24, after the #3BM5 salvage'
links: {plans: [], commits: [67abbcb49095, 78f1f80dab22, da4294d6aac9, 12ca85652ede, baaefc87c915, 88d7382336bc, 0dfb46a40078, d257de0a121e, 096b9512a547], evidence: [docs/qa_evidence/2026-09-25-fyey-uncommitted-work/], related: [3BM5, SZHQ, R5TC, 243T, WNKN, 76QW], github: null}
---
# Stop uncommitted work building up: sessions die before landing, claims outlive panes, board writes have no owner

## Issue
then write a card on the buildup

(or add to SZHQ)

## Plan
**Goal.** Make uncommitted work impossible to lose track of: workers stop sharing live code, landing happens at the verification boundary instead of whenever a session remembers, claims and board writes acquire owners that die with their pane, and hunks carry their author from the edit that made them. The measure is a day like 2026-09-24 (142 dirty entries, 93 board records, ~150 stale land sessions, two salvage batches and a take-back) ending with `land.py orphans` empty.

**Findings (re-read 2026-09-25 evening; corrections to the 04:00 plan marked).**
- **Why a backend edit kills other panes.** `dataRoot()` (`src/AppPaths.h:67`) falls back to `RELAY_SOURCE_DIR`, so in a dev build the data root *is* the live checkout. **Four** sites read `backend/` from it, not three: the pane worker `Pane::startWorker()` (`src/Pane.h:10716`, `python3 -X utf8 -S -u <data>/backend/worker.py`, per-process `QProcessEnvironment` with `RELAY_PANE_ID`/`RELAY_SESSION_TOKEN`; restarts from `src/PaneRuntime.cpp:659,667`, `src/PaneEvents.cpp:860`, `src/Pane.h:10975`), the Board worker (`src/BoardWorker.cpp:116–120`), the guest sidecar PYTHONPATH (`src/GuestBridge.h:307–309`), and the pane **shell's** `RELAY_BACKEND_DIR`/PYTHONPATH (`src/PaneRuntime.cpp:702–712`, used by guest hook shims). Detection already exists: `logs.source_changed()` (`logs.py:51`) fingerprints `backend/` at start and is short-circuited by `RELAY_BUILD_ID`; `agent.py:2995` turns an `AttributeError`/`ImportError` into "Relay's backend code changed after this agent started… Restart the agent". Prevention does not.
- **land.py sessions are anonymous.** Registry entries carry `repo, branch, started, updated, contact, claims` (`cmd_begin` ~775); `contact` already carries informal identity by convention (`who` today: "subagent a3, card #83YV", "pane cf7919f6 #5A37"). Hunk selection is positional (`--only-hunk src/Pane.h:1,3`). No `status`, `reap` or `orphans` command. #WNKN (refined tonight) adds the `token` field, auto-begin from the executor's write path, and an authorship journal `<land root>/authors/<token>.jsonl` with content-addressed blobs — **this card consumes that journal; it does not define a second one.**
- **The board knows the pane; land does not.** `board_claim` writes the pane token onto the card (`board_tools.py` ~3281, from `configure {pane_token}`); every write records `pane=RELAY_PANE_ID` (`board_protocol.py:512`); edits flush through `apply_now()` (925); nothing commits `.board/`. Every status change funnels through `_move` (`board_tools.py:2765`), which already stacks `_signal_gate`, `_human_qa_gate`, `_verify_gate` and `_qa_ask_gate` before writing — a land gate slots in beside them. (The 04:00 plan's "second move at 2915" is the tail of the same function, not a second site.) `issues -> .board` is an untracked symlink (`?? issues`).
- **Edit tools write anonymously**, but host and guest-bridge writes both go through `ToolExecutor.execute` (`tools.py` ~1069–1130; `guest_bridge.call_tool` ~969), so one hook covers both — #WNKN step 3 puts it there.
- **Subagents are threads, not processes** (`subagents.py:2`, `SubagentManager._start_thread`). The 04:00 step 9 ("spawn with `start_new_session`, SIGTERM the process group") has nothing to signal. `stop()` (`subagents.py:1276`) sets `stop_requested` and calls `Agent.stop()` (`agent.py:2588`): cancel event, `executor.stop_process()` — which kills only the **foreground** job the turn is waiting on (`tools.py:630`) — and provider cancel. Jobs a subagent handed back (`RestrictedExecutor` keeps `command_output`/`stop_command`) keep running, and a mailbox STOP via `agent_message` is only read between steps: that is how a1 landed ceb4860f 17 minutes after "stop the agents".
- **A pin store pattern already exists:** `src/RuntimeDirs.h` — `DirStamp`, `Owner {pid, starttime}`, `SweepResult`, a timid sweep. `run_command` children now run under systemd scopes with `_kill_group` (`jobs.py:86`, #ZPWT).

**Steps.**
1. **Pinned backend store** in `src/RuntimeDirs.{h,cpp}`: `pinnedBackend(dataRoot())` hashes the `backend/` tree (contents, not mtimes), materialises `$XDG_CACHE_HOME/relay/backend-<hash>/backend` on first use (hardlinks, copy fallback), returns the path. Owner file + sweep reuse the RuntimeDirs discipline (a pin nobody's live pid owns is swept). `RELAY_BACKEND_UNPINNED=1` keeps today's behaviour. Pin from the **working tree at spawn**.
2. **Launch from the pin** at the three process sites (`Pane::startWorker()`, `BoardWorker::start()`, `GuestBridge.h` PYTHONPATH), resolved once per spawn, and set `RELAY_BUILD_ID=<hash>` in that environment so `logs.source_changed()` stops firing for a pinned worker. The pane's **shell** keeps the live checkout (`RELAY_BACKEND_DIR`, PaneRuntime.cpp:702): a developer's shell should see the tree they edit, and the hook shims are short-lived. The worker reports `backend_rev` on ready/context; the pane shows it in pane info and the status line names it when it differs from the checkout's current hash. Add a "Reload backend" action (restart this pane's worker from the current tree).
3. **land.py learns panes, cards, owners:** on top of #WNKN's `token`, `begin` accepts `--card <id>` and `--owner <subagent-thread-id>` into meta.json and the registry (`contact` stays free text); new `land.py status --token <t> --json` reports the token's sessions (auto and manual), their claims and uncommitted hunk counts per path; `commit` refuses a session whose `owner` thread is marked cancelled (step 9's marker file `<land root>/cancelled/<thread-id>`).
4. **Author-based selection:** `--only-hunk`/`--exclude-hunk` gain `--by <pane-id|#card>` selecting hunks whose blocks match #WNKN's journal entries for that pane or card (same matcher as FOREIGN attribution); positional numbers stay as the fallback when no journal covers a path. (The journal itself is #WNKN step 3; nothing to write here.)
5. **Land at the verification boundary:** `_move` gains `_land_gate(card, status)`: before a move to `needs-verification` or `done`, run `scripts/land.py status --token <pane token> --json` (repo root from the board; skipped when there is no `scripts/land.py`, and on a subprocess failure it warns instead of refusing); uncommitted hunks → refusal naming files and hunk counts, offering `land.py commit`. The deliver skill's step 5 gains a "land your claims" bullet before the two landing bullets; regenerate `.board/POLICY.md`.
6. **Claims die with their pane:** new `land.py reap --token <t>`: for each of the token's sessions, dirty → a note on its `--card` ("N hunks uncommitted in <files>, snapshot <age>") and the session kept, marked `reaped`; clean → abandoned. Relay calls it from the worker-finished handler and pane close (`src/PaneRuntime.cpp` ~592–668; `src/ActivePaneClose.h`), keyed on the token, never idle time; the shell path is `RELAY_BACKEND_DIR/../scripts/land.py` only when it exists. `who`/`doctor` fold reaped sessions under an "orphans" line; the 12 h staleness rule stays as backstop. The card note is written through the board's own file fallback (append to `.board/threads/<ID>.md`) since land.py has no board tools.
7. **Board writes land themselves:** at turn end, after `apply_now()` flushes (worker turn completion in `agent.py`), a board-sync commits the `.board/` paths this pane wrote this turn (the protocol records `pane=` per write) through land.py's commit path with `--whole` on those paths and no build gate (no C++), CAS-retried, one commit per turn, message `board: <pane> <turn>`; skipped silently when the repo has no `scripts/land.py`. Add `issues` to `.gitignore` or commit the symlink — recommend ignoring it.
8. **Salvage is "resume card X":** `land.py orphans` prints uncommitted hunks grouped by claiming session's card (from `--card`, else the contact's `#ID`, else "no card"); a "Resume card" action opens a pane on the card with its orphans listed. Salvage reads the card's `## Done means` before landing anything.
9. **Subagent cancel that actually stops work:** `SubagentManager.stop()` also calls the subagent's `RestrictedExecutor.jobs.stop_all()` (background jobs included) and writes the cancelled marker for its thread id (step 3) so its land session cannot commit afterwards; the `agent` tool family gets an explicit stop (`agent_stop` as a main-agent tool if it is only a protocol message today — check `delegation_tool_specs`, subagents.py:522) and `agent_message`'s description says STOP-by-message is advisory. Kill goes through `jobs.py` `_kill_group` (process group / scope), which is where the processes are.

`#243T` keeps splitting the hot headers; this card only cites it.

**Orchestration.** Three tracks; A and B touch no shared files and run in parallel, C waits for B's land.py interface (steps 3–4) and for #WNKN steps 1–3 (token passthrough, executor hook, journal). All commits stay with the main agent Run hands this card to.
- **A (C++):** steps 1, 2, the reap call sites in 6, the "Resume card"/"Reload backend" actions — `src/RuntimeDirs.*`, `src/Pane.h` (minimal: the three `startWorker` lines), `src/PaneRuntime.cpp`, `src/BoardWorker.*`, `src/GuestBridge.h`, `src/ActivePaneClose.h`.
- **B (land.py):** steps 3, 4, 6's command, 8 — `scripts/land.py`, `tests/test_land.py`.
- **C (backend python, after B and #WNKN):** steps 5, 7, 9 — `backend/relay_core/{board_tools,agent,subagents}.py` and their tests; the deliver skill bullet and POLICY regeneration.

**Risks.**
- **Pinned means stale.** Pin from the working tree at spawn (recommendation), not `git archive HEAD`: HEAD-pinning would hide a session's own uncommitted backend edit. The pane must show `backend_rev` or a stale backend gets debugged blind; "Reload backend" is the escape hatch. *Owner: confirm working-tree pin.*
- **Board-sync commit volume** at 10+ panes: one commit per turn per pane, CAS-retried. If noisy, drop to one sync per fold. *Owner: per-turn or per-fold.*
- **Reap must never drop a live claim:** key on the token's worker being gone (RuntimeDirs liveness), never idle time; in doubt, write the note and keep the claim.
- **The land gate depends on identity plumbing** (#WNKN steps 1–4): a manual `begin <me>` from a guest shell without a token is invisible to `status --token` and passes the gate. Named in the refusal text as a limit, and `orphans` still catches it.
- **`src/Pane.h` is the hottest file in the repo:** keep A's diff there to the `startWorker` command lines; new code goes in `RuntimeDirs.cpp`/`PaneRuntime.cpp`.

**Verify.**
- `tests/test_land.py`: `--card/--owner` recorded; `status --token --json` lists auto and manual sessions; `reap` notes dirty sessions, abandons clean ones, never touches a live token; `orphans` groups by card; `--only-hunk --by` selects by author while a concurrent edit shifts positions; `commit` refuses a cancelled owner.
- C++ (`tests/*_test.cpp` near `runtimedirs`): a pin is created once per hash, owned, and swept when unowned; `RELAY_BACKEND_UNPINNED=1` restores the live path. Python (`tests/test_hosted.py` neighbourhood): a worker keeps answering while `backend/` is rewritten under it; a restart reports the new hash.
- `tests/test_board_protocol.py` + `tests/test_board_turns.py`: the move refusal names files and hunk counts; after a turn's board-sync `git status -- .board` is clean.
- `tests/test_guest_delegation.py`/`tests/test_subagents.py`: stop kills a handed-back job and the cancelled marker refuses a later commit.
- Manual: a multi-pane day ends with `land.py orphans` empty and `who` listing only live tokens.

## Done means
- Editing `backend/` while other panes' agents run does not end their turns: each worker runs a pinned copy taken at its start, the pane shows which revision it runs, and a restart (or the reload action) picks up the new one.
- A card cannot move to `needs-verification` or `done` while its pane's land session holds uncommitted hunks — the refusal names the files — and after a turn's board writes `git status -- .board` is clean.
- Closing or crashing a pane with uncommitted claims leaves a "N hunks uncommitted in <files>" note on its card; `land.py who` lists only sessions whose pane is alive; `land.py orphans` groups what is left by card.
- `commit` and `--only-hunk` select hunks by recorded author (session + card), so a concurrent edit elsewhere in the file cannot change what lands; a cancelled subagent cannot commit at all.
- Failure looks like 2026-09-24 again: dead sessions crowding `who`, `.board/` dirty in `git status`, turns dying with "backend code changed", or position-based hunk selections going stale.

## Decisions
- 2026-09-25, owner: **pin from the working tree at spawn** ("5 ok"), never from HEAD.
- 2026-09-25, owner: **board-sync per turn** ("6 per turn is ok"): one `board: <pane> <turn>` commit per turn that wrote to `.board/`.

## Tests
- `tests/test_land.py` — 146 passed (status, board-sync, --owner and the cancelled marker, reap, orphans, who folding, --by selection by pane and by card, everything pre-existing unchanged)
- `tests/test_board_tools.py` — `LandGateTests`: a move with uncommitted hunks is refused naming the file, a clean one passes, a failing script warns, other statuses never call it
- `tests/test_agent.py` — `BoardSyncTests`: one board-sync per turn with exactly the paths written, none without board writes or without land.py
- `tests/test_subagents.py` — stop ends a handed-back job within a second and writes the cancelled marker; `agent_stop` is listed
- `ctest -R runtimedirs` — 17 checks: pin created once per hash and reused, contents-not-mtimes hashing, unpinned and missing-tree fail-open, dead-owner sweep
- `ctest -R boardresume` — listed card gets the action and the prefilled prompt, unlisted card does not, failing or missing land.py hides it
- `tests/test_worker_backend_rev.py` — ready carries `backend_rev` with `RELAY_BUILD_ID`, "live" without
- `manual: docs/qa_evidence/2026-09-25-fyey-uncommitted-work/README.md`

## Execution Summary
Five commits by Opus subagents of pane 987d2a1a, all landed through `scripts/land.py`, plus two evidence commits. (`67abbcb4` in links.commits is another session's transcript-glyph change that mentions this card; `da4294d6` is #76QW's, carrying the deliver-skill "land your claims" bullet for this card's gate.)

- **`78f1f80d` track A, C++** (`src/RuntimeDirs.{h,cpp}`, `src/Pane.h` startWorker lines, `src/PaneRuntime.cpp`, `src/BoardWorker.cpp`, `src/GuestBridge.h`, `src/SessionInfo.cpp`, `src/PaneSession.cpp`, `backend/worker.py`): `pinBackend(dataRoot)` hashes `backend/` (contents, not mtimes; 12-hex id), materialises `$XDG_CACHE_HOME/relay/backend-<hash>/backend` by hardlinks into a `.making-*` sibling renamed only when whole, marks it owned and sweeps dead-owner pins timidly; `RELAY_BACKEND_UNPINNED=1` and any failure fall open onto the live path. The pane worker, the Board worker and the guest sidecar launch from the pin with `RELAY_BUILD_ID=<hash>` (so `logs.source_changed()` stays quiet); the pane shell stays on the live checkout. The worker's ready message carries `backend_rev`; pane info shows `backend <hash>` and says "backend changed; Restart agent to load it" when the checkout's hash differs; the stopped-agent banner gains "Reload backend". `Pane::reapLandScripts()` runs `land.py reap --token` detached on close and worker exit.
- **`12ca8565` track C, backend** (`board_tools.py`, `agent.py`, `subagents.py`, `.gitignore`): `_land_gate` in `_move` runs `land.py status --token <pane> --json` before a move to needs-verification or done and refuses naming files and hunk counts (a failing or slow script warns instead, key `land_warning`); `_sync_board_writes` at the end of every turn that wrote board files runs `land.py board-sync <token> -m "board: <pane> <turn>" <paths>` in a background thread; `SubagentManager.stop` also stops the subagent's whole job table and writes `<land root>/cancelled/<thread-id>`; new main-agent tool `agent_stop {id|all}`, and `agent_message`'s description says a STOP note is advisory; `/issues` ignored.
- **`baaefc87` Resume card** (`src/BoardPane.{h,cpp}`, `tests/boardresume_test.cpp`): the card page reads `land.py orphans --json` when it opens and shows "Resume card" for a listed card; pressing it does what Run does with the pane's first prompt prefilled with the orphan listing and the instruction to read `## Done means`, then `land.py who` and `commit <session>` or `abandon <session>`.
- **`88d73823` track B, land.py** (`scripts/land.py`, `tests/test_land.py`, CLAUDE.md): `begin --owner/--card`; `status --token [--json]`; `board-sync` (whole `.board/` paths, no build gate, no hold, CAS retried, never a claim); `reap --token` (dirty sessions kept and marked `reaped` with a note on their card thread, clean ones abandoned); `orphans [--json]` grouped by card; `who`/`doctor` fold reaped sessions into one line and show card/owner; `--only-hunk/--exclude-hunk … --by <pane|#card>` selects by the #WNKN authorship journal with the FOREIGN matcher; `commit` and `try` refuse a cancelled owner; the stale "default 2" slot sentence in CLAUDE.md fixed.
- Owner decisions honoured: pin from the working tree at spawn; board-sync per turn.

Evidence: `docs/qa_evidence/2026-09-25-fyey-uncommitted-work/README.md` — real `status`/`orphans`/`who` runs on this checkout and the whole-card test runs on a clean export. Limits, as planned: a manual `begin <me>` from a shell without a token is invisible to the gate (`orphans` still catches it); reap and orphans key on reaped/stale sessions, not on pane liveness, because land.py cannot see panes; running workers pick up the new backend at their next restart.
