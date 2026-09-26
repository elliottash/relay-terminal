# Migrating a project to isolated development

This is a project-by-project cutover guide for Relay's parallel-development queue. It does not
activate the Relay source repository. Use Linux for the first publication host. Keep the old
checkout and all of its files; the queue changes how future work is allocated and published.

## Prepare the project

1. Make sure `main` is a real branch and the project has a required test gate. Run
   `relay-land --repo "$project" project-init` to register the Git common directory and inspect
   the suggested `.relay/project.toml`. If using `project-init --write`, review its output before
   committing it. Configure `verification.commands` as argv arrays and make them fail on broken
   code. Configure `[main]` with a build/install that copies **all** runtime assets into `{dest}`
   and a smoke command that runs the installed executable. Commit the config on `main`.
2. Inventory *before* changing mode:

   ```bash
   relay-land --repo "$project" inventory
   git -C "$project" status --short --ignored
   git -C "$project" worktree list --porcelain
   python3 /path/to/relay/scripts/land.py who
   relay-land --repo "$project" activate --dry-run
   ```

   Save the JSON inventory and list of ignored/untracked paths with the rollout record. Check
   active `land.py` claims, legacy publisher processes, attached `main` worktrees, staged and
   unstaged changes, ignored build/dependency files, target SHA, hook handling, and the config
   policy hash. A dry run reports blockers and the planned symbolic HEAD move without changing
   mode. A dirty checkout is an item to preserve, not a reason to reset it.
3. Stop new legacy publication and let existing `land.py commit`/`repair` and Board sync work
   drain. Stop or restart live agents only at a turn boundary; an already running process is not
   moved into a workspace. Resolve any other worktree still attached to `main` before activation.
   The transition takes the publication lock exclusively so no legacy ref swap races it.
4. Run `relay-land --repo "$project" activate` only after the dry run says
   `can_activate: true`. It records the target baseline, accepts the config from the *target
   tip*, writes the mode marker, and, if the human checkout was on `main`, creates `human` at
   that same SHA and changes HEAD symbolically. It does not rewrite the checkout files or index.
   Save the returned repo ID, accepted-policy hash, human branch and baseline ref.

## Keep the publisher running

`relay-land run` remains in the foreground and holds one daemon lock per repository. For a
user-level systemd installation, create `~/.config/systemd/user/relay-land-<project>.service`
with the **absolute** installed CLI and project paths:

```ini
[Unit]
Description=Relay landing publisher for <project>

[Service]
Type=simple
WorkingDirectory=/absolute/path/to/project
ExecStart=/absolute/path/to/relay-land --repo /absolute/path/to/project run
Restart=on-failure
RestartSec=3

[Install]
WantedBy=default.target
```

Then run `systemctl --user daemon-reload`, `systemctl --user enable --now
relay-land-<project>.service`, and inspect `systemctl --user status relay-land-<project>.service`
plus `relay-land --repo "$project" snapshot`. Arrange login lingering only if this publisher
must continue while no user session exists. A service restart resumes its persisted queue; do
not start a second runner. `run --once` is useful for an isolated rehearsal, not a background
supervisor. The queue's SQLite records, refs, logs and outbox survive a crash; `status`,
`receipt`, `handoffs` and `events` distinguish a published job from one requiring author work.

New agent panes receive isolated workspaces. A human remains on the `human` branch in the old
checkout and can inspect `main` without checking it out there. To work as an author, create a
workspace, commit, `try`, `submit`, and watch `status`/`receipt` as shown in
[the operating commands](TREES-AND-LANDING.md#operating-a-configured-project). A queued commit
is immutable; edits made after submit stay in that workspace for another submission. Keep the
canonical Board in the registered project root, not in each sparse tree. Changes to the
verification policy require explicit `capture-policy` after the config itself lands; the old
accepted gate judges that config change.

The release launcher resolves `tip/run/current`; `main-status` reports installed SHA and lag
while a new build is pending. Keep at least two completed installed releases. Observe actual
build duration, source tree bytes, verification cache bytes and release bytes on the host. A
tiny independent Python fixture in `tests/test_parallel_project_adoption.py` measured, on
2026-09-26, 2,093 source bytes (excluding `.git`), 2,205 bytes in one source tree, 6 bytes
in the main build directory (2,208 bytes across the cache), and 1,929 bytes in the current
installed release. One run's first and second landing ticks took 0.913 s and 0.990 s, with
main build/install times of 0.185 s and 0.308 s. These
are sample measurements of a tiny project, not a compilation-speed or production-capacity
promise; real projects need a matched workload and a week of measurements after rollout.

## Roll back without discarding work

1. Stop new author submissions, run `relay-land --repo "$project" pause`, then stop the runner
   (`systemctl --user stop relay-land-<project>.service` for the example above). Check
   `inventory`/`snapshot` for in-flight jobs and wait for the publisher lock to drain. Do not
   kill a verification merely to make the mode marker change.
2. Run `relay-land --repo "$project" rollback --keep-head` when the human checkout has dirty,
   untracked or ignored data, or when you want to keep its branch attachment unchanged. This
   restores legacy publication mode while retaining the queue, receipts, workspace branches,
   installed releases and human branch. It does not reset or clean anything. Without
   `--keep-head`, rollback only reattaches `main` if HEAD is at the same commit or Git accepts a
   safe fast-forward; otherwise it refuses and leaves the project paused. Resolve that checkout
   yourself, then retry. Never use `git reset --hard` or a broad `git clean` as a rollback step.
3. Inspect `relay-land --repo "$project" inventory` and `status` again. Preserve any failed or
   queued submission for its author, and use the legacy `land.py` procedure only after the marker
   reports `legacy`. Restart old agents at a controlled boundary; do not silently move their
   live processes or delete workspaces. Workspace removal requires a released lease, a receipt
   matching its current tip, no pending job and no dirty, untracked or ignored files. A clean
   tree with an unlanded commit is still retained.

The host-wide admission ledger sizes work against effective CPU/memory/disk headroom and can
refuse or defer it; reservation is not a process memory limit. Linux is the supported initial
publisher. AI reconciliation, if enabled, uses weighted configured High-tier models with a
bounded number of attempts and token reservations. Delayed usage reports and guest account
overshoot can exceed an estimate, so monitor the provider ledger and do not use those budgets
as a hard billing cap. Failed gates and unsafe reconciliations return to the author; they do
not become an owner approval queue.
