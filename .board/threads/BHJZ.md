<!-- relay:entry 20260925T141623Z-k0 author=agent kind=event model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/4e0734b8d76242b3897fcbede8595652 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-land-py-s-session-root-follows-the-per-session-t.md

<!-- relay:entry 20260925T141656Z-sp author=agent kind=note model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/4e0734b8d76242b3897fcbede8595652 -->
Evidence, 2026-09-25 10:10 EDT, Claude Code guest session in Relay on relay-terminal. `python3 scripts/land.py who` printed: "no land sessions under /home/elliott/.cache/relay/scratch/4386a69e-3006-48f5-bb9c-6e7338a8c7df/tmp/claude-1000/land". At the same moment `git status --short` listed 81 modified/untracked files from other sessions (src/FilePanes.cpp +722, src/RelayWindow.h, src/Pane.h, backend/relay_core/agent.py, worker.py, untracked src/ArtifactWorkspace.*, backend/relay_core/workspace_plugins.py, ...) — the #E85D/#F8R7/#C0Q8 cluster held uncommitted by the owner. CLAUDE.md documents the land root as `/tmp/claude-1000/land/<me>/`; under Relay's per-session scratch (#DVV2) each session's TMPDIR is its own scratch dir, so each session gets a private land root. Consequences: `who` never lists another session; `begin` never leaves a marker in another session's data; the contested-hunk and stale-claim checks that exist for the 2026-09-19 incidents cannot fire across sessions; a second session's `begin` on a path another session claims is silently uncontested. Expected: land.py resolves its root to one shared location independent of TMPDIR (e.g. $XDG_STATE_HOME/relay/land, ledgered once as Relay's own), or Relay passes a shared RELAY_LAND_ROOT to every session; `who` says which root it read.

<!-- relay:entry 20260925T141656Z-sq author=agent kind=event model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/4e0734b8d76242b3897fcbede8595652 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260925T150651Z-y0 author=owner kind=comment mode=discuss -->
is this a problem

<!-- relay:entry 20260925T150750Z-wd author=agent kind=comment mode=discuss model=glm-5.3 turn=04260d22816a326de0fe1f1942607888/e78b26fea4ae42778ef6597740537103 -->
**Yes — a real one, though a narrow one.** I checked `scripts/land.py`: `DEFAULT_ROOT` is `tempfile.gettempdir() + ...`, which follows each Relay session's private TMPDIR (#DVV2), so every session gets its own island of land data.

What that silently disables, in this repo's normal many-sessions-one-checkout mode:

- **The contested-hunk markers and `who` only work within one root.** A second session's `begin` on a path you claim looks uncontested — exactly the 2026-09-19 "landed half of someone else's change, `main` stopped compiling" incident the markers were built to stop. Nothing errors; `who` just says "no land sessions" while 80+ files from other sessions sit in the tree.
- **What still protects you:** the three-way merge never commits paths you didn't claim, and the 15-minute stale-snapshot hold still forces review. So it's degraded, not gone — the open window is another session editing your path after your `begin`, inside 15 minutes.
- **Side effect:** the shared verify-slot pool (2 slots, #SZHQ) and the 3-day `gc` are also per-root now, so the disk growth they cap can quietly return.

It bites hardest exactly when #P2W8 runs subagent waves — many concurrent sessions in one checkout. The fix is small: land.py already honours `RELAY_LAND_ROOT`/`--root`, so either Relay passes every session one shared root or the default moves off `gettempdir()`.

Say the word and I'll move #BHJZ to **Run** (the Run button) to hand it to a terminal pane for that fix.

<!-- relay:entry 20260925T150803Z-sy author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Discussing → Executing, session d0d996b2

<!-- relay:entry 20260925T150803Z-sz author=owner kind=progress pane=switchboard pane_token=d0d996b2-de76-404a-8903-c38c30208f92 -->
Claimed (d0d996b2) · working on it from a terminal pane

<!-- relay:entry 20260925T150905Z-zk author=agent kind=event model=claude-opus-5-5 pane=d0d996b2 turn=9698842186c7475288ae58fe80f77b2e/1dc47ada2ad346fbb30d66a58aeb2dad -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260925T151059Z-rj author=agent kind=event model=claude-opus-5-5 pane=d0d996b2 turn=9698842186c7475288ae58fe80f77b2e/1dc47ada2ad346fbb30d66a58aeb2dad -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["DVV2", "P2W8"], "githu… → {"plans": [], "commits": ["37d660ae"], "evidence": ["docs/qa_evidence/2026-09-25…; appended to `## Tests`

<!-- relay:entry 20260925T151100Z-hg author=agent kind=event model=claude-opus-5-5 pane=d0d996b2 turn=9698842186c7475288ae58fe80f77b2e/1dc47ada2ad346fbb30d66a58aeb2dad -->
- ✦ agent moved this card · Executing → Needs verification · Landed 37d660ae: land.py's default root is /tmp/claude-<uid>/land regardless of TMPDIR, with a fallback for sessions begun under the old per-TMPDIR root; test_land.py 74/74. · evidence docs/qa_evidence/2026-09-25-bhjz-shared-land-root/ · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260925T151104Z-cj author=agent kind=progress model=claude-opus-5-5 pane=d0d996b2 turn=9698842186c7475288ae58fe80f77b2e/1dc47ada2ad346fbb30d66a58aeb2dad -->
Landed 37d660ae. `DEFAULT_ROOT` is now `/tmp/claude-<uid>/land` on POSIX (`TEMP`/`TMP` on Windows), not `tempfile.gettempdir()`, so it no longer follows the per-session TMPDIR (#DVV2). That's the path CLAUDE.md documents and scratch.py already leaves to `land.py gc`. `RELAY_LAND_ROOT`/`--root` still override it. Sessions begun under their old per-TMPDIR root can still `commit`/`abandon`: land.py falls back to that root and says so on stderr. `who` always names its root. Tests: test_land.py 74/74; the 2 new tests fail against the old land.py. Not done: the per-TMPDIR land dirs already sitting in Relay scratch roots are left alone. They expire with their scratch dir (days:7), and their live sessions still finish through the fallback.

<!-- relay:entry 20260926T003548Z-rm author=agent kind=event mention=3MH4 model=claude-fable-5-1 pane=f35051fe turn=dbc9af56def4427c91f4fecd2d277da4/1a6acee91a96495087b757d1ef3ee4e7 -->
mentioned in #3MH4 · 2026-09-26 · agent
