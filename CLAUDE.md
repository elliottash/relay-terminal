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
