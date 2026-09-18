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
