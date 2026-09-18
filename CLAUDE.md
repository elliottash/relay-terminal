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
still had the new code, so nobody noticed until a clean export failed to build. Commit this way:

1. **Never commit from the shared index**, and never `git add` into it. Use a private one, set per
   command and never exported (a test that runs `git add` in a temp repo inherits an exported
   `GIT_INDEX_FILE` and corrupts it): `BASE=$(git rev-parse HEAD); GIT_INDEX_FILE=<scratch file> git
   read-tree $BASE`, and prefix each later git command the same way.
2. **Add only your changes** to it. A file only you changed: `git add <path>`. A file that also
   holds another session's uncommitted edits: build the file as `BASE`'s version plus your hunks,
   `git hash-object -w` it and `git update-index --cacheinfo`. Do not use hunk-filtered
   `git apply --cached --unidiff-zero`, which can put insertions on the wrong line without an error.
3. **Build and test the exact tree** you are committing, not the working tree:
   `rm -rf` a scratch dir, `git checkout-index -a --prefix=<scratch dir>/`, then configure (check
   CMake's exit code: a reused build dir hides a broken configure), build, and run `ctest` including
   `backend-and-bash`, since Python tests read C++ sources. The working tree holds everyone's
   uncommitted code and proves nothing about your commit.
4. **Compare-and-swap onto `main`:** `NEW=$(git commit-tree $(git write-tree) -p $BASE -m ...)`, then
   `git update-ref refs/heads/main $NEW $BASE`. If `main` moved, this fails; rebuild on the new
   HEAD and retry. Never "just re-add" the files you built earlier on top of a newer base.
   **Read the base once.** `$BASE` must be the same value in step 1, in `-p $BASE` and in
   `update-ref`'s old-value argument. Each Bash call is a fresh shell, so the variable is gone by
   the time step 4 runs and `BASE=$(git rev-parse HEAD)` looks like a harmless way to get it back —
   it is not. The build in step 3 takes minutes, commits land here every few, and that second read
   returns a newer head: the swap then guards the head it was given, succeeds, and writes your
   older tree over everything that landed while you were testing. That is the one case the swap
   exists to refuse, and it is what lost `backend/relay_core/tools.py` (#E99H, twice in one day)
   and #W5N2's `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md`, restored in `f38682f` and `26362e2`. Carry
   the value across calls in a file instead (`echo $BASE > <scratch>/base`), re-`read-tree` from it
   right before committing, and gate on `git diff --name-only $BASE $NEW`: it must list only your
   own paths. Check that before `update-ref`, not after — a revert nobody notices stays on `main`
   for hours, because every working tree still shows the file as it should be.
5. **Reset the shared index for your paths** afterwards: `git reset -q HEAD -- <your paths>`. Skipping this leaves the shared index at your old blobs, and the next plain
   `git commit` by anyone reverts your commit.

If `git diff --cached --stat` in the shared index ever shows files you did not stage, do not commit
over them. Check whether each staged blob is an older version from history
(`git log --format=%h -- <file>`, then compare `git rev-parse :<file>` against `<commit>:<file>`).
If it is, unstaging it with `git reset -q HEAD -- <file>` loses nothing, so do that and tell the
session that owns it.

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
