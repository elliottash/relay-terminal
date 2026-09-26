# Isolated development and verified landing

Implementation contract for card #3MH4. The owner's later decisions supersede the historical
discussion on that card: start implementation now, no replay experiment, automatic AI
reconciliation, and weighted selection from the configured `high` tier at high effort. A failed
or unsafe reconciliation returns to the author agent, not to the owner. Production cutover is a
distinct, drained transition after verification; until then this checkout uses `land.py`.

## Invariants

1. Each development workspace owns its branch, index and working files. Submitted work is an
   immutable commit ID. Neither preparing nor reconciling a job rewrites an author's branch.
2. A repository has one publisher. Code and Board metadata use it. It publishes the exact commit
   it verified with an expected-old-ref comparison. A changed target requires new verification.
3. Intent is durable before changing Git refs. A restart can distinguish a completed publication
   from an interrupted verification. Notifications are retried independently of publication.
4. A clean workspace can contain unlanded work. Cleanup needs a released lease, a receipt covering
   its current tip, no pending submissions, and no dirty, untracked or ignored user data.
5. Canonical project identity and Board location are separate from execution cwd. No second Board
   is created in a sparse development tree.
6. Project configuration cannot relax the checks used to approve that configuration. Publication
   requires a registered accepted policy; merely adding a config file does not enable it.
7. A runnable main release is immutable and installed completely before `current` changes.
   It records its SHA. While rebuilding, the previous release remains runnable and its lag visible.
8. The queue never updates a target branch checked out in a human or agent workspace. Cutover
   moves the human checkout to a separate branch while preserving all files and the index.

## Placement and identity

`state_root` below is `$XDG_STATE_HOME/relay`, default `~/.local/state/relay`. An explicit
constructor/CLI override is supported for tests. It is never a pane's temporary directory.

```
state_root/integration/registry.sqlite3           # A1: repositories and workspace leases
state_root/integration/<repo-id>/queue.sqlite3    # A2: jobs, receipts, notification outbox
state_root/integration/<repo-id>/publisher.lock   # entire prepare/verify/publish transaction
state_root/integration/<repo-id>/logs/<job-id>/
state_root/integration/<repo-id>/tip/run/<sha>/    # installed runnable releases
state_root/integration/<repo-id>/tip/run/current  # atomic symlink to complete release
state_root/integration/resources.sqlite3          # A3: host admission ledger
state_root/integration/reconcile.sqlite3          # A4: budget reservations and token ledger
state_root/trees/<repo-id>/<workspace-id>/         # source-only development worktrees
cache_root/integration/<repo-id>/<slot>/           # bounded disposable verification builds
cache_root/integration/<repo-id>/tip/              # coalesced runnable-main build
cache_root/ccache/                                # compiler cache, separately bounded
```

Here `cache_root` is `$XDG_CACHE_HOME/relay`, default `~/.cache/relay`. Workspace IDs are opaque;
card and session are metadata, never unchecked path components. Repositories get a persistent
random ID keyed by their resolved Git common directory. Common-directory relocation is explicit
repair, not automatic creation of a second identity. SQLite uses WAL, foreign keys, busy timeout,
short transactions and schema versioning. Separate databases keep module ownership independent;
cross-database links are IDs plus receipts, never assumed atomic transactions.

## Module ownership and Python interfaces

Public records are JSON-compatible dictionaries, paths are strings in records and `Path` or string
arguments. Constructors accept keyword `state_root=None`. Every public operation raises a useful
module-specific exception on refusal. CLI adapters print the reason and exit nonzero. Additional
optional arguments are allowed; coordinate incompatible interface changes with the parent agent.

### A1 — `relay_core.trees`, card #RT3B

Owns `backend/relay_core/trees.py`, `scripts/relay-tree`, `tests/test_trees.py`.

```
register_repo(repo, *, state_root=None) -> dict
    # id, common_dir, project_root, board_root, target (default main), mode (legacy)
resolve_project(path, *, state_root=None) -> dict | None
    # resolve registered canonical repo from any linked worktree; no allocation
configure_repo(repo, *, state_root=None, mode=None, target=None) -> dict
    # metadata only; modes legacy/queue/paused; caller owns safe transition
TreeManager(repo, *, state_root=None)
    .create(session, *, card=None, base=None, excludes=(), init=None,
            max_workspaces=50) -> dict
    .list() -> list[dict]
    .get(workspace_id) -> dict
    .release(workspace_id, *, owner) -> dict
    .sync(workspace_id, *, owner) -> dict
    .remove(workspace_id, *, receipt=None) -> dict
```

Workspace records: `id, repo_id, path, branch, base_sha, session, card, owner, status, created_at,
updated_at, init_error`; status includes `creating, active, init_failed, released, retained, removed`.
Repository and workspace tables are A1's schema. Create is idempotent per active session and
holds a cross-process lease; worktree and DB crash windows are recoverable, not silently dropped.
Sparse exclusions use tested non-cone patterns and never remove instruction/config files. The
canonical Board is excluded; own evidence can be included explicitly. Init failure is durable and
retryable. No copying secrets. Sync refuses active competing leases and dirty work; no implicit
rebase at submission. Cleanup accepts a queue receipt but verifies repo, submitted SHA and current
tip; no broad `git clean`, no deleting ignored dependency/user files. Tests use actual temporary Git
repos for same-file isolation, recovery, sparse rules, quota and init failures.

### A2 — `relay_core.landq`, card #FW1C

Owns `backend/relay_core/landq.py`, `scripts/relay-land`, `tests/test_landq.py`.

```
Queue(repo, *, state_root=None)
    .submit(submitted_sha, *, request_id, workspace_id=None, card=None,
            base_sha=None, kind="code", selected_tests=()) -> dict
    .status(job_id=None) -> dict | list[dict]
    .cancel(job_id) -> dict
    .process_one(verifier, *, reconcile=None) -> dict | None
    .recover() -> list[dict]
    .receipt(job_id) -> dict | None
```

`verifier(job, candidate_sha, candidate_path)` returns `{ok: bool, policy_hash: str, log: str,
verified: bool, reason?: str}`. It must be called on every changed candidate. `reconcile(context)`
returns `{status: "resolved"|"author_required", candidate_sha?: str, ...}`; any resolved commit
still goes through the required verifier. B1 supplies production adapters. Defaults refuse to
publish; testing callbacks are explicit. Never let a callback change the target or author branch.

Jobs table includes `id, request_id UNIQUE, repo_id, workspace_id, card, kind, base_sha,
submitted_sha, target_sha, candidate_sha, candidate_tree, policy_hash, selected_tests_json,
status, reason, cancel_requested, created_at, updated_at`. Receipts are unique per job and include
`job_id, repo_id, submitted_sha, target_before, published_sha, tree, policy_hash, verified, landed_at`.
Persist verification results bound to candidate SHA/tree and policy before `ready`. Outbox rows
have a unique delivery key and retry state. Git refs retain submissions and candidates across GC.

`queued -> preparing -> verifying -> ready -> publishing -> landed`; additional outcomes are
`conflict, failed, cancelled, interrupted`. Save publishing intent in SQLite before `update-ref`.
Recovery recognizes the exact published candidate even if it is now an ancestor of the target;
otherwise it requeues interrupted work with no stale verification pass. A publication that won a
cancellation race reports `landed`, never pretends to undo it. Two processes cannot publish the
same repo concurrently. Target movement requires reconstructing and reverifying the candidate.
Use a service-owned worktree/private index, preserve merge ancestry, scrub ambient GIT_* vars.

Metadata jobs contain only canonical `.board/` snapshots and run schema/path checks through the
same publisher. They cannot update code via a rename/symlink/path escape. Snapshots are immutable;
Board edits arriving later remain in the canonical Board and become later jobs.

### A3 — configuration, admission, main releases, card #ASQ4

Owns `backend/relay_core/projectconf.py`, `backend/relay_core/integration_slots.py`,
`backend/relay_core/main_release.py` and corresponding `tests/test_*.py` files.

```
projectconf.load(repo, *, revision=None) -> dict
projectconf.policy_hash(config) -> str
projectconf.detect(repo) -> dict                 # suggestions, not automatic trust
projectconf.run_gate(config, cwd, *, selected_tests=(), env=None) -> dict
    # verifier result: ok, policy_hash, log, verified, reason
integration_slots.HostAdmission(*, state_root=None)
    .acquire(repo_id, job_id, *, memory_bytes=0, disk_bytes=0, cpus=1,
             priority="land", timeout=0) -> context manager
    .status() -> dict                           # capacity, usage, reservations, waiters, live
main_release.MainRelease(repo, *, state_root=None, cache_root=None, repo_id=None)
    .update(sha, config) -> dict
    .status() -> dict                           # current sha/path, requested sha, lag/error
```

Config v1 (`.relay/project.toml`) normalizes these sections and rejects bad types/unknown dangerous
values. Supported commands are argv arrays (or arrays of argv arrays where stated), not implicitly
interpolated shell strings. A shell command must explicitly name `sh -c`. Path placeholders are
substituted as individual argv entries or literal string replacements, never shell-quoted guesses.

```toml
version = 1
[project]
target = "main"
[workspace]
exclude = [".board", "docs/qa_evidence"]
max_workspaces = 50
init = []
[verification]
commands = [["python3", "-m", "pytest", "-q"]]
timeout_seconds = 900
ungated = false
[resources]
memory_bytes = 8589934592
disk_bytes = 4294967296
cpus = 2
[main]
build = []
install = []
executable = "bin/relay"
keep = 2
[reconcile]
enabled = true
max_attempts = 2
tokens_per_case = 200000
tokens_per_day = 10000000
```

`main.build`/`main.install` are arrays of argv arrays and support `{source}`, `{build}`, `{dest}`.
Main releases include all runtime assets needed after the disposable source/build is reused;
copying a binary that reaches back into an overwritten source directory is insufficient. Use a
project install command and smoke gate (`main.smoke`, an optional array of argv arrays supporting
`{dest}` and `{executable}`). B1 passes the canonical registry ID to MainRelease explicitly.
Coalesce updates; keep at least two completed releases.
Do not promise cache hits or zero extra compilation: record actual time and bytes.

Accepted config is captured from the target at activation and stored durably by B1. Candidate
config changes always run the old policy; activating a new policy is explicit. Unknown files use
the full gate. No Python fast lane without Python checks. `pytest` no-tests and CTest zero-tests
must not silently approve. Extra selected tests cannot remove configured required commands.

Host admission is shared across repos. Respect effective service cgroup/CPU limits, available
memory and disk headroom. Reservations are transactional and released on process death. Zero
capacity waits/refuses with a reason. Limit concurrent heavy jobs, track wait age and prioritize
landings without indefinitely starving try jobs. Linux is the first supported publication host;
other hosts receive a clear refusal, not an unlocked fallback.

### A4 — `relay_core.reconcile`, card #P9ZA

Owns `backend/relay_core/reconcile.py`, `tests/test_reconcile.py` (may add a narrowly named
provider adapter/test file). Reuse existing roles/provider APIs, do not change their selection.

```
Reconciler(*, state_root=None)
    async .reconcile(context, *, model_call=None) -> dict
```

Context includes `repo, repo_id, job_id, base_sha, target_sha, submitted_sha, candidate_path,
cards, intents, conflicts, diagnostics, policy`. Return `status`, patch/candidate information,
`model`, `preset`, `tokens`, `attempts`, `reason`, and `trailer`. The queue owns commit creation and
publication; the reconciler edits only its disposable candidate, never the author's files.

Production draws randomly from top-ranked configured `high` candidates through existing
`roles.ordered_candidates(choose=True)`/role resolution and subscription weighting. Use high
effort; do not hardcode vendor model IDs. Include guest subscriptions through the actual provider
adapter. Record drawn model/account and usage. Fake model injection supports deterministic tests.
Durably reserve case/day budgets before calls, count input/output usage, account for uncertain
calls conservatively after crashes. At most two attempts. Cap context and completion budgets.

Reject patches outside candidate paths, changes weakening/removing tests, unresolved conflict
markers, or substantive deletion of either side's intent. Conservative refusal is fine; route it
to the author agent with diagnostics and patch, never a per-conflict owner confirmation. This
check is not proof of semantics: the required project gate remains mandatory. Accepted fixes get
`Reconciled-From: <target-sha> <submitted-sha>` and idempotent notes on both cards.

## Wiring and protocol

B1 (#AMQQ) takes ownership of landq/relay-land after A2 finishes and adds
`integration_service.py`, service/cutover tests, and narrow legacy `land.py`/`agent.py` Board-sync
adapters. A2 CLI verbs: `submit SHA --request-id ID`, `status [ID]`, `cancel ID` (JSON output).
B1 adds `run [--once]`, `activate`, `pause`, `rollback`, `try`, `main-status`, `main-run`,
`project-init`. A1 CLI: `create SESSION`, `list`, `sync ID --owner OWNER`, `release ID --owner OWNER`,
`remove ID`. All accept `--repo` and `--state-root`. Exit 0 success, 1 usage/config, 2 refused,
3 conflict, 5 gate failure, 7 admission timeout. Submit itself succeeds when the job is recorded.

B2 (#80X1) owns backend pane adapters (`board_tools.py`, `tools.py`, `guest_launch.py`, `scratch.py`,
worker workspace handling and protocol doc). B3 (#DV5Y) owns GUI adapters (`PaneRuntime.cpp`,
`Pane.h`, `BoardPane.{h,cpp}`, narrowly necessary launcher/actions and targeted GUI tests).
Resolve project and allocate workspace before starting shell/worker/guest. If allocation fails,
do not silently fall back to shared development. Existing processes move only at a stopped turn
and controlled restart. Read-only planning can stay canonical. Register mode separately from config;
legacy projects behave unchanged. Native and guest tools receive the same canonical Board root,
execution cwd, workspace ID and immutable submission instructions.

Protocol additions (B2 documents final section number):

* `tree_status`: `project_root, board_root, repo_id, workspace_id, execution_cwd, branch,
  base_sha, state, recoverable, reason`.
* `queue_status`: `repo_id, jobs` with `id, card, workspace_id, status, reason, age_seconds,
  candidate_sha, published_sha`; optional `main_release` record.
* `main_moved`: `repo_id, previous_sha, sha`; informational, never an implicit rebase.

Live rows show workspace/branch, submitted job, wait reason, retained work and runnable-main lag.
The launcher "Relay (main)" resolves the current installed executable; no shell injection or
overwriting a running binary. Reuse existing action/shortcut hints where an action is added.

## Cutover and rollback

B1 provides an inventory/dry-run before enabling: active legacy claims/processes, dirty and ignored
files, target attachment, registry and accepted policy. Stop new legacy publication, drain existing
writers under a shared transition lock, preserve baseline refs and all work. Create a human branch
at the existing target tip and change HEAD symbolically without rewriting files or index, recording
the transition. Refuse if any other worktree remains attached to the target. Start the service,
route Board snapshots to it, then enable future workspace launches. Never move live processes.
The legacy guard covers commit, repair and Board sync. Replace the Relay-owned pre-commit hook
so ordinary commits in private worktrees succeed; keep the shared-index refusal and preserve any
non-Relay project hook. Board CLI fallback resolves the canonical Board too.
Rollback pauses admission, drains/stops the publisher, retains jobs and branches, and only then
restores the legacy publication mode. Test with pending work; do not reset a dirty checkout.

## Verification and ownership handoff

Each agent claims its child card, records Done means before code, uses legacy land.py begin/commit
for its named implementation files, runs focused tests and lands its changes. No full suite unless
needed for the accepted integration gate. Contract changes go through the parent. Phase 1 modules
are new files; B1 begins after A1–A4, then B2/B3 wire their disjoint surfaces. Common tests use
`tempfile.TemporaryDirectory`, real Git repos with configured local test identity, and independent
state/cache roots; no test mutates the actual repo's mode or config. An extra shared fixture is
optional; avoid coordinating tests through mutable global state.

C1 (#8J0A) independently probes two same-file authors, dirty and clean-unlanded exits, crashes
before/after publication, duplicate submit, competing publishers, moving target, Board writes
during verification, failed/zero-test gates, cross-repo resource exhaustion, stale artifacts,
cancellation, post-submit edits and rollback. Stage native and guest pane flows and capture the
visible status under an isolated profile. Use a different model family where available.
C2 (#2DP8) demonstrates a second Python project using only config, documents actual commands,
updates BUILDING.md and prepares migration guidance. A week of matched workload measurements
follows rollout; it is not a reason to delay the authorized implementation.

## Operating a configured project

The executable interface is `scripts/relay-land` (or the installed `relay-land`). These examples
assume a **different** Git repository at `$project`, with a committed `.relay/project.toml` on
`main`. `project-init` registers it in legacy mode and suggests a config; `--write` writes that
suggestion for review, but neither form activates the queue. Commands return JSON except
`main-run`, which replaces itself with the installed executable.

```bash
project=/path/to/other-project
relay-land --repo "$project" project-init
# Review .relay/project.toml, define a required gate and self-contained main install,
# then commit that config on main.
relay-land --repo "$project" inventory
relay-land --repo "$project" activate --dry-run
# After draining legacy writers and reviewing the dry run:
relay-land --repo "$project" activate
relay-land --repo "$project" run
```

`run` is a **foreground, durable-state loop**, not a daemonizing command. Run one instance per
repository under a supervisor (for example, a user systemd service with `Restart=on-failure`)
and stop that service before rollback. A fresh `run` resumes the persisted queue; `run --once`
processes one tick for a controlled test. `run --no-main` leaves runnable-main updates disabled
for that invocation. Submission only records a job and succeeds before verification. The author
flow is:

```bash
relay-land --repo "$project" workspace create author-session --card '#ABCD'
# Edit and commit in the returned execution_cwd; save its workspace_id and commit SHA.
relay-land --repo "$project" try COMMIT_SHA
relay-land --repo "$project" submit COMMIT_SHA --workspace-id WORKSPACE_ID \
  --request-id UNIQUE_REQUEST_ID --card '#ABCD'
relay-land --repo "$project" status JOB_ID
relay-land --repo "$project" receipt JOB_ID
relay-land --repo "$project" main-status
relay-land --repo "$project" main-run -- --version
```

`try` is advisory and cannot replace the publisher's gate. A candidate changing
`.relay/project.toml` still runs the **previously accepted** gate. After the new config lands,
inspect it and explicitly run `relay-land --repo "$project" capture-policy` to accept it for
later jobs. Selected `--test` values add checks; they do not remove configured commands. A
failing or zero-test gate cannot publish. Conflict reconciliation runs only when enabled in the
accepted config: it draws from configured High-tier candidates with subscription weighting and
high effort, under case/day token budgets, attempt and context caps. Budget reservations cannot
guarantee a hard spend ceiling when a guest provider reports usage late or exceeds an estimate;
inspect provider usage. An unresolved or unsafe candidate returns to the author with diagnostics.

The canonical Board remains under the registered project's `board_root`, while source-only
workspaces exclude it. `board-submit .board/path --session SESSION` snapshots current Board
files into a metadata job; later edits remain in the canonical Board for another job. Use
`snapshot`, `events`, `handoffs` and `workspace status ID` to see the queue, author notices and
retained work. `main-status` exposes installed SHA, requested SHA, lag and build errors;
`main-run` resolves the `current` installed release. A running old executable survives an update
because the new release is installed completely before `current` changes. The active human
checkout is on a separate branch after cutover; `main` is the publication target, not a branch
to attach another worktree to.

`workspace release ID --owner OWNER` releases the lease; `workspace remove ID` still refuses
unlanded commits, pending submissions, dirty files and untracked or ignored data without a
matching landing receipt. `workspace sync` is available through `relay-tree`; do it only with a
clean, released workspace. Workspace quotas come from `workspace.max_workspaces`, while gate
resource requests come from `[resources]` and the host-wide admission ledger. Admission is
Linux-first and cgroup-aware; a reservation is accounting, not a kernel memory cap. A busy or
undersized host waits or refuses rather than building without admission. Disposable builds and
retained runnable releases use real disk and CPU; measure them on the target host rather than
assuming compiler-cache hits.

The default state root is `$XDG_STATE_HOME/relay` (`~/.local/state/relay`); the default cache
root is `$XDG_CACHE_HOME/relay` (`~/.cache/relay`). The registry is
`state/integration/registry.sqlite3`; a project's queue, receipts and service policy are under
`state/integration/<repo-id>/`, its source trees under `state/trees/<repo-id>/`, and disposable
verification and coalesced main builds under `cache/integration/<repo-id>/`. Installed releases
are `state/integration/<repo-id>/tip/run/<sha>/`, with `run/current` an atomic symlink. The Git
common directory holds `relay-publication.json` and the transition lock. `--state-root` and
`--cache-root` isolate CLI trials; they do not alter the registered default project.

The [migration guide](PARALLEL-DEVELOPMENT-MIGRATION.md) gives the preservation checklist,
supervisor example and rollback sequence. This repository remains in legacy mode until its
separate production cutover; the active `CLAUDE.md` rule still applies here.
