# Relay — instructions for Claude sessions

The project's working rules live in `WARP.md` (issues, tests, protocol, shortcut hints) and the
docs it points at. Read that first. What follows is specific to how sessions work in this repo.

## Work in this checkout, on main. No branches, no worktrees.

Owner's rule, 2026-09-18: **do not create a branch or a git worktree for your work.** Edit
`/home/elliott/repos/relay-terminal` directly and commit to `main`.

This repo is worked by several Claude sessions at once, often in the same files. Branches and
worktrees look safer than they are here: work sitting on a side branch is invisible to everyone
else, so two sessions fix the same bug twice (it happened — the model dropdown and the unknown
`/command` were each implemented independently on the same day), a fast-forward aborts on any file
another session has open, and every abandoned worktree leaves a stale branch and a stale build
directory behind. Committing small and often to `main` is what keeps the other sessions honest
about what is already done.

What this means in practice:

- Commit your own work as soon as it builds and `ctest` passes. Do not sit on it.
- Before starting, `git log --oneline -15` and `git status` — someone may have just done it, or be
  half way through the file you are about to change.
- Never commit, stash or revert a file you did not write. If another session's unfinished work
  blocks you, say so and wait, or ask them; `issues/bug_intake.txt` and `issues/feature_intake.txt`
  are the owner's inboxes and are never yours to commit.
- Never use bare `git stash` / `git stash pop`: the stash stack is shared with every other session.
- Subagents work in this checkout too. Give them a narrow, named area of the code so two of them
  cannot land in the same function, and tell them not to commit anything they did not write.

## How to commit here without reverting someone else

On 2026-09-18 alone, five commits silently undid other sessions' work: each was built from an index
or a base that was older than `main`, so it wrote files back to their old contents. The working tree
still had the new code, so nobody noticed until a clean export failed to build. That is how
`backend/relay_core/tools.py` went twice in one day (#E99H) and how #W5N2 lost
`docs/REMOTE-AND-MULTIPLAYER-DESIGN.md`; they came back in `f38682f` and `26362e2`.

The hand-run recipe that replaced it then failed four more ways in one evening on 2026-09-19:
`git checkout HEAD -- <path>` on a path another session was editing overwrote work that existed
nowhere else; `git reset -q HEAD -- <paths>` run while the checkout sat on a different branch
unstaged somebody else's files; a merge written as
`git merge-file ours <(git show A:p) <(git show B:p)` exited 0 and applied nothing, because
`merge-file` cannot read a `/dev/fd` pipe; and the shared index quietly filled with entries equal to
older commits' blobs as `main` moved, so the next plain `git commit` by anyone reverted whatever had
landed since. Every one of those looks like success at the terminal.

So it is not hand-run any more. **`scripts/land.py` is the commit procedure here, and the only one.**

```
python3 scripts/land.py begin <me> <the paths you are about to change>
# edit, build and test in this checkout, exactly as before
python3 scripts/land.py commit <me> -m "message"      # or -m path/to/message.txt
```

`<me>` is any short name you pick for your session. `begin` snapshots those files as they are right
now — including files another session has already half-edited, and files that do not exist yet.
`commit` then takes, for each path, base = that snapshot, ours = the file at the current tip of
`refs/heads/main`, theirs = your working copy, and three-way merges them into *the tip plus your
hunks*: the other session's uncommitted edits were in the snapshot, so they are neither committed
nor touched, and they are still sitting in the working tree afterwards. It hashes the merged blobs
into a private index read from that same tip, `commit-tree`s onto it, checks that
`git diff --name-only TIP NEW` lists exactly your paths, and compare-and-swaps with
`git update-ref refs/heads/main NEW TIP`. If `main` moved while you were testing, the swap fails —
that is the one case it exists for — and the whole merge is recomputed against the new tip and
retried, up to ten times. Afterwards it sets the shared index entry for your paths, and only your
paths, to what it committed, so `git status` shows what is still uncommitted and nobody's next
`git commit` can revert you.

What it refuses to do, and why each refusal is an incident from the list above:

- **It never commits from the shared index and never `git add`s into it.** `python3
  scripts/land.py hook install` puts a `pre-commit` hook in place that makes git itself refuse a
  commit whose index is the shared one, saying so in a sentence; `begin` installs it if it is
  missing. The owner's escape hatch is `RELAY_ALLOW_SHARED_COMMIT=1 git commit …`.
- **It never runs `git checkout`, `git stash` or `git reset`, and never writes a working-tree
  file.** The one exception is `doctor --fix` on a path whose index entry *and* working copy are
  both byte-for-byte an older commit's blob — provably nobody's edit.
- **It merges through real temporary files**, never process substitution.
- **It reads the tip once per attempt** and passes that same sha to `commit-tree -p` and to
  `update-ref`'s old-value argument. It never re-reads the branch between building and swapping.
- **It never commits `issues/bug_intake.txt`, `issues/feature_intake.txt` or a `*.orig` file** (the
  first two are the owner's inboxes), and it refuses a path that `.gitignore` covers, so a build
  directory cannot be swept in.
- **A path you edited without `begin` is refused**: with no snapshot there is no way to tell your
  diff from anyone else's. Either claim it (`begin` snapshots it as it is now, so only what you do
  from then on lands) or pass `--whole <path>`, which commits that entire working copy and prints
  the hunks it is about to take with it.
- **A conflict aborts.** It names the paths, exits 3, and leaves the branch, the index and the
  working tree exactly as they were. Pull the other session's version into your copy by hand
  (`git show main:<path>`), then run `commit` again.

`--dry-run` prints the merged diff without landing anything, `--paths` lands a subset of what you
claimed, and `abandon <me>` drops the snapshots (never the working tree). `--help` is written for a
session that has not read this file.

`python3 scripts/land.py doctor` is the thing to run when the checkout looks wrong. It reports a
staged entry whose blob is an older commit's version of that path — the shape that reverts people —
and `--fix` puts the index (and, only in the provable case above, the working copy) back to HEAD's
version, which loses nothing. Staged content that is *not* in history is somebody's uncommitted
work: it reports that and leaves it alone, and the same goes for stray branches, extra worktrees,
`*.orig` files and a checkout that is not on `main`. It exits non-zero when it found something.

## Fix clear gaps; do not list them

Owner's rule, 2026-09-18: **when you find a clear gap in your own work, fix it rather than list
it.** A "Known gaps" or "Deliberately left" section is for what genuinely needs the owner — a product
decision, a trade-off with no obvious answer, or work outside what you may touch — not for loose ends
you could have tied. Before reporting, go through your gaps and ask of each: is the right behaviour
obvious, and is it within reach? If yes, do it, test it, and report it as done. What stays listed
must say why it could not be done here (whose decision it is, or which session's files it needs).
The same goes for subagents: tell them this rule, and when their report lists a gap that fails the
test, send them back to fix it rather than passing it on.

## `src/main.cpp` was split (2026-09-18). Here is where things went.

`main.cpp` was 13,000 lines and every session was editing the same file. It is now the includes,
`registerUrlHandler()`, `migrateFastRoleSettings()`, the quit signal and `main()` — about 330
lines — and one header per unit beside it:

- `src/AppPaths.h` — `dataRoot()`, `relayFuzzyScore()`, and the `RELAY_*` fallbacks.
- `src/Keymap.h` — `ActionDef`, `Keymap`.
- `src/Isolation.h` — `namespace isolation`.
- `src/Pane.h` — `QueueRowDelegate` and `Pane`. This is still 8,000 of the lines.
- `src/PaneChrome.h` — `ToolPane`, `PaneChrome`.
- `src/WindowChrome.h` — `ChromeButton`, `NotificationsPopup`.
- `src/RelayWindow.h` — `ClosedItem`, the `WindowManager` declaration, then `RelayWindow`. Those
  two share a header because they name each other in inline member bodies: `WindowManager::forget()`
  takes a `RelayWindow *`, `RelayWindow` holds a `WindowManager *`, so neither can be complete first.
- `src/WindowManagerImpl.h` — the `WindowManager` members that were already written out of line
  below `RelayWindow`, now `inline`.

**It is still one translation unit.** `main.cpp` includes those headers and nothing else does, and
every class in them defines its members in the class body, exactly as before. The split was about
being able to find and edit a class without scrolling past four others — not about build times, and
not a step towards a library. A one-line change to `Pane` still recompiles the lot. De-inlining
`Pane` into a `Pane.cpp` is a separate, riskier job; do not start it as a side effect of something
else.

Two practical consequences:

- Put a new window-level class in the header its neighbours are in, not back in `main.cpp`, and add
  it to the `relay` target's source list in `CMakeLists.txt` if it gets a file of its own (AUTOMOC
  and IDEs read that list; nothing here has `Q_OBJECT`).
- Each header includes what it uses and compiles on its own. If you add a use of a Qt class, add its
  `#include` to that header, not only to `main.cpp` — `main.cpp`'s includes come first in the one
  translation unit, so a missing one in a header will not show up in the build.

`scripts/split-main.py` is the tool that did it: it finds every region by content anchor, moves the
text without rewriting it (the only change is the word `inline`), and `--check` rebuilds the
original from the new files to prove no line was dropped or duplicated. It was run twice, once on
`git show HEAD:src/main.cpp` and once on the working tree, so the sessions that had uncommitted
edits in `main.cpp` got them back as ordinary uncommitted diffs in the new files. It is one-shot and
kept for the record; it refuses to run on an already-split `main.cpp`.
