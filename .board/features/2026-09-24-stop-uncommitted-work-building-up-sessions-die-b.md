---
id: FYEY
type: work
status: planned
labels: [feature, workflow, land, agents]
rank: zzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: 'Relay pane 64de364c, 2026-09-24, after the #3BM5 salvage'
links: {plans: [], commits: [], evidence: [], related: [3BM5, SZHQ, R5TC, 243T], github: null}
---
# Stop uncommitted work building up: sessions die before landing, claims outlive panes, board writes have no owner

## Issue
then write a card on the buildup

(or add to SZHQ)

## Plan
**What happened (2026-09-24, #3BM5).** By evening the shared checkout held 142 dirty entries: about 1,900 lines of code across 33 files, 93 board records, and 750 land.py session directories, about 150 of them stale. The salvage took two batches (6c334cd3, 557f8b2c), one take-back (221bfef7 undid a subagent's commit of work the owner did not want landed), and hunk-by-hunk surgery for #R5TC. Three shared files mixed hunks from up to five cards, and the tree moved under every attempt: two land runs were refused because main advanced mid-build, and one hunk selection went stale between the dry run and the commit (the build gate caught it). #SZHQ fixes the disk side (verify slots, gc, relay-scratch). This card covers the uncommitted work.

**Root causes, most important first.**
1. **Agents die between editing and landing.** Every worker runs `backend/` from the checkout the other sessions edit, so one session's backend edit kills other panes' turns ("Relay's backend code changed after this agent started, so it mixed old and new modules"). The pane_send label NameError (d2a60b55) killed more. The work was finished; the landing step never ran.
2. **Landing is a manual step at the end of a turn,** so a crash, a closed pane or a context compaction skips it. CLAUDE.md says commit as soon as it builds, but nothing enforces that.
3. **Land claims outlive their panes.** A board claim is released when its pane closes; a land.py claim is kept until someone runs `abandon`, which nobody does. `who` and `doctor` drown in dead sessions, and the next agent cannot tell a live claim from a dead one.
4. **Board writes have no owner.** The board tools write `.board/` files, but no session commits them unless it remembers to. 93 records were left over.
5. **Nothing records who wrote each hunk.** Several sessions interleave hunks in `Pane.h` (1 MB) and `RelayWindow.h` (480 KB), and attribution is done afterwards by reading `#CARD` comments.

**Steps.**
1. **Pin each worker's code** (the highest-value change). At worker start, run from a snapshot of `backend/` (a content-addressed copy under the runtime dir, or `git worktree`-free `git archive HEAD backend`), not the live checkout. A backend edit applies to new workers and to a pane's next restart. Keep an explicit "reload backend" action for developers.
2. **Land at the verification boundary.** `board_move_card` to needs-verification or done refuses while the calling session holds land.py claims with uncommitted hunks, and names them. The deliver skill gets a `land` step before the move.
3. **Claims die with their pane.** `land.py begin` records the pane token (RELAY_PANE_ID). On pane close or worker crash, Relay reads that session's claims. With uncommitted hunks it writes "N hunks uncommitted in <files>, snapshot <age>" on the card the pane had claimed; without any it runs `abandon`. `who` hides sessions whose pane is gone.
4. **Board writes land themselves.** Each board tool write commits its own files through land.py, which is cheap (no build gate for `.board/`, only CAS retries), or a single board-sync that runs at every turn end. Remove the untracked `issues -> .board` symlink or commit it.
5. **Record hunk authorship at edit time.** The edit tools (Relay's `edit_file`/`write_file`, and the guest bridge for Claude and Codex) append `{session, card, path, range}` to the land session, so `commit` and salvage select hunks by author instead of by position. Position-based `--only-hunk` became stale twice in one evening.
6. **Salvage is "resume card X", not "land these files".** Add a `land.py orphans` report that groups uncommitted hunks by the claiming session's card, plus a Relay action "Resume card" that opens a pane on the card with its orphaned hunks listed. The salvager must read the card's Done-means before landing; a subagent told to "land everything" landed work the owner did not want.
7. **Keep splitting the hot headers** (#243T): move `Pane`/`RelayWindow` method bodies into `.cpp` files so fewer cards share a hunk.

**Risks.** Pinned workers run stale code until restarted, so the pane must show which backend revision it runs. Auto-landing board writes adds commits (squash them per turn). Auto-abandon must never drop a live session's claim: key on the pane being gone and on the worker's pid, not on idle time.

**Verify.** Tests: a worker keeps running while `backend/` changes under it; a card move is refused while claims are uncommitted; closing a pane with dirty claims writes the card note and keeps the working tree; a board write leaves `git status -- .board` clean; `--only-hunk` by author survives a concurrent edit that shifts positions.

## Done means
- Editing `backend/` while other panes' agents run does not end their turns; each pane shows the backend revision it runs, and a restart picks up the new one.
- A card cannot move to needs-verification or done while its session holds uncommitted land claims; the refusal names the files.
- Closing or crashing a pane with uncommitted claims leaves a note on its card saying what is uncommitted; `land.py who` lists only sessions whose pane is alive.
- After any board tool write, `git status -- .board` is clean.
- land.py selects hunks by recorded author, so a concurrent edit elsewhere in the file does not change what lands.
- A day like 2026-09-24, with ten or more concurrent sessions, ends with no orphaned hunks: `land.py orphans` prints nothing.
