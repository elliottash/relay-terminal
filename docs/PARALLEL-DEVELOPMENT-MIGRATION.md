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

   **Live legacy claims are a readiness gate you check yourself.** `activate` blocks only on a
   *running* `land.py commit`/`repair` process (`inventory` → `.legacy.processes`). It does not
   block on sessions that hold claims and uncommitted hunks but are idle between commands
   (`.legacy.sessions`, sessions active within the last 12 hours). Do not copy a count from
   an earlier day into the rollout record. Capture the list at cutover time, in the same minute
   as the dry run:

   ```bash
   relay-land --repo "$project" inventory | python3 -c \
     'import json,sys; i=json.load(sys.stdin); print(json.dumps(i["legacy"], indent=1))'
   python3 /path/to/relay/scripts/land.py who
   python3 /path/to/relay/scripts/land.py orphans --json
   ```

   Every live session there must either `land.py commit`, `land.py abandon` (which leaves the
   working tree alone) or be reaped (`land.py reap --token <t>`) before `activate`. Reaping keeps
   dirty sessions for rescue. After activation, the legacy `land.py commit` procedure no longer
   publishes, so a claim left open lands later only through a workspace submission. Its hunks
   stay in the old checkout's working tree for their author to move.
3. Stop new legacy publication and let existing `land.py commit`/`repair` and Board sync work
   drain. Stop or restart live agents only at a turn boundary; an already running process is not
   moved into a workspace. Resolve any other worktree still attached to `main` before activation.
   The transition takes the publication lock exclusively so no legacy ref swap races it.
4. Run `relay-land --repo "$project" activate` only after the dry run says
   `can_activate: true`. It records the target baseline, accepts the config from the *target
   tip*, writes the mode marker, and, if the human checkout was on `main`, creates `human` at
   that same SHA and changes HEAD symbolically. It does not rewrite the checkout files or index.
   Save the returned repo ID, accepted-policy hash, human branch and baseline ref.

## Relay's own prepared configuration

This is the `.relay/project.toml` prepared for `relay-terminal`. It is **not committed and not
active**: it lands on `main` as step 1 of the real cutover, and `activate` accepts it from the
target tip. It parses under the shipped `projectconf.normalize_config` (policy hash
`f1b468a7afbe` on 2026-09-26). Its `main.install` and `main.smoke` were rehearsed against an
existing `build-fast/`. `cmake --install` took 1.24 s and produced `bin/relay` plus
`share/relay/{app,backend,remote,rendezvous,scripts,shell,theme}`, 86,294,432 bytes in all.
`bin/relay --version` printed `relay 0.1.0` offscreen and the installed backend imported. That
build was `-O0 -g1`, so a Release install will differ in size. The first real gate and main
build still need measuring on the publication host.

```toml
version = 1

[project]
target = "main"

[workspace]
# Board and evidence stay in the canonical project root, never in author trees.
exclude = [".board", "docs/qa_evidence"]
max_workspaces = 24
init = []

[verification]
timeout_seconds = 5400
# The candidate tree's path changes per job, and a CMake build directory is pinned to one
# source path. So each gate mirrors the candidate into a fixed, service-owned source directory
# (rsync --checksum keeps the mtimes of unchanged files, so the warm build stays incremental),
# then configures, builds and tests there. flock serializes this gate with a concurrent
# `relay-land try`. `--no-tests=error` fails an empty ctest run.
commands = [
  ["sh", "-c", """
set -eu
root="${RELAY_VERIFY_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/relay/verify/relay-terminal}"
mkdir -p "$root/src" "$root/build"
exec 9>"$root/.lock"; flock 9
rsync -a --checksum --delete --exclude=/.git --exclude=/build --exclude='/build-*' ./ "$root/src/"
[ -f "$root/build/CMakeCache.txt" ] || cmake -S "$root/src" -B "$root/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build" --parallel "${RELAY_JOBS:-2}"
ctest --test-dir "$root/build" --output-on-failure --no-tests=error -j "${RELAY_JOBS:-2}"
"""],
  ["sh", "-c", "cd \"${RELAY_VERIFY_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/relay/verify/relay-terminal}/src\" && scripts/test.sh"],
]

[verification.environment]
QT_QPA_PLATFORM = "offscreen"
RELAY_KEYRING = "off"

[resources]
memory_bytes = 17179869184   # 16 GiB: optimized compile at RELAY_JOBS=8 (#04EC: ~2 GiB/job)
disk_bytes = 21474836480     # 20 GiB: mirrored source + warm build
cpus = 8

[main]
build = [
  ["cmake", "-S", "{source}", "-B", "{build}", "-DCMAKE_BUILD_TYPE=Release"],
  ["cmake", "--build", "{build}", "--parallel", "--target", "relay"],
]
# cmake --install copies bin/relay and every runtime asset under share/relay.
install = [["cmake", "--install", "{build}", "--prefix", "{dest}"]]
executable = "bin/relay"
smoke = [
  ["sh", "-c", "QT_QPA_PLATFORM=offscreen \"{executable}\" --version"],
  ["sh", "-c", "test -f \"{dest}/share/relay/backend/worker.py\" && PYTHONPATH=\"{dest}/share/relay/backend\" python3 -c 'import relay_core.board, relay_core.landq'"],
]
keep = 3
timeout_seconds = 7200

[reconcile]
enabled = false
max_attempts = 2
tokens_per_case = 200000
tokens_per_day = 10000000
```

Why it looks like this:

- **Source and build are split.** `verification.commands` run with the disposable candidate
  tree as cwd. The service does not yet export a warm build path such as `VERIFY_BUILD` or
  `RELAY_BUILD_DIR` into the gate environment. That is why the gate names its own external
  root (`RELAY_VERIFY_ROOT`, default `~/.cache/relay/verify/relay-terminal`) instead of
  building inside the candidate. If B1 later exports a service-owned build directory, swap
  the default for it. Until then this root belongs to the gate: nobody edits or builds there
  by hand. The shared checkout's `build/` is still built by `scripts/relay-build`, and
  `land.py try` keeps using its verify slots. There is no `relay-build --build-dir` flag, so
  none is used here.
- **`--delete` in the mirror** keeps an ignored or deleted source file from a previous
  candidate out of the next one's build. A stale ignored Python module otherwise stays
  importable and can pass a gate it should fail.
- **Gate breadth is the owner's decision.** As written, every publication runs the full
  `ctest` and `scripts/test.sh` suites, the same suites this repo tells sessions not to run by
  hand. That is the safe default for the sole publication path, but it is the slowest. A
  narrower accepted gate is a policy change: it lands on `main`, then someone runs
  `capture-policy`.
- **`reconcile.enabled = false`** until the owner turns it on. Conflicts then return to their
  authors instead of spending High-tier tokens.
- **`[main]` builds only the `relay` target** and installs with `cmake --install`. The
  install rules in `CMakeLists.txt` copy the backend, shell integration, remote, rendezvous,
  app, themes and helper scripts. The smoke commands prove that the installed binary starts
  and that the installed backend imports without the source checkout.

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
3. **`--keep-head` restores the mode, not the checkout.** After it, the marker says `legacy`,
   but the canonical checkout is usually still on `human`, and that branch is usually *behind*
   `main`, because the queue kept publishing while the human worked there. The legacy Relay
   workflow assumes the checkout is on `main` at its tip. `land.py commit` merges against
   `refs/heads/main` and builds from the working tree. Old sessions started on a `human`
   checkout would edit, build and test a stale tree. So, before any legacy session resumes:

   ```bash
   git -C "$project" symbolic-ref -q HEAD || echo detached
   git -C "$project" rev-parse HEAD main
   git -C "$project" merge-base --is-ancestor HEAD main && echo "HEAD is behind or at main"
   git -C "$project" status --short
   git -C "$project" diff --cached --stat
   ```

   If `status` is empty and HEAD is an ancestor of `main`, move the checkout onto `main` in one
   of two ways. You can run `rollback` again without `--keep-head`, which fast-forwards and
   moves HEAD safely. Or you can run `git -C "$project" switch main`, which refuses to
   overwrite local changes. If `status` is not empty, keep those files and use no reset. Commit
   them on `human` and `submit` them *before* rolling back, or save them as a patch outside the
   repository and re-apply it after `switch main`. `human` commits that are not on `main`
   (`merge-base --is-ancestor` fails) must be submitted or merged first. Rollback refuses them
   for the same reason. Check that the index shows no staged entries left over from the other
   branch (`python3 /path/to/relay/scripts/land.py doctor`).
4. Inspect `relay-land --repo "$project" inventory` and `status` again. Queued, failed and
   conflicted jobs, their receipts and their workspace branches are retained. Nothing is
   cancelled by rollback. Preserve them for their authors: resubmit after a later
   re-activation, or carry the commit over by hand. Only once the marker reports `legacy`
   *and* the checkout is on `main` at its tip should anyone use the legacy `land.py`
   procedure. Do not blindly restart old agents because the marker changed. Restart them
   one at a time at a turn boundary, after the checkout check above. Do not silently move
   their live processes or delete workspaces. Workspace removal requires a released lease, a receipt
   matching its current tip, no pending job and no dirty, untracked or ignored files. A clean
   tree with an unlanded commit is still retained.

The host-wide admission ledger sizes work against effective CPU/memory/disk headroom and can
refuse or defer it; reservation is not a process memory limit. Linux is the supported initial
publisher. AI reconciliation, if enabled, uses weighted configured High-tier models with a
bounded number of attempts and token reservations. Delayed usage reports and guest account
overshoot can exceed an estimate, so monitor the provider ledger and do not use those budgets
as a hard billing cap. Failed gates and unsafe reconciliations return to the author; they do
not become an owner approval queue.
