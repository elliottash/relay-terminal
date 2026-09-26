---
id: TE6D
type: work
status: planned
labels: [feature, landing, workspaces, performance]
parent: 3MH4
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-26'
source: Claude pane 21c53ef4, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [VK6J], github: null}
---
# Allocate a pane's workspace tree on its first write or command, not when the pane opens

## Issue
Every pane in a queue-mode project takes a workspace tree as soon as it opens, even when its agent only reads and explores. That filled the 50-tree quota on the first day and cost 28 GB. The owner wants a tree allocated only when the agent is about to change something: its first file write, or its first run_command, since a shell command may write.

> why not wait until the agent is going to change something to start a new worktree? what if agent is just exploring
> — elliott · [session:97d268b4846648f49e6aba30a5ebe433](relay://session/97d268b4846648f49e6aba30a5ebe433) · 2026-09-26

## Plan
**Rule.** A pane starts with no tree. Reads run against the canonical checkout: read_file, list_directory, search_files, the Board tools, terminal_read. The first *mutating* action allocates the tree and moves the agent into it before that action runs: write_file, edit_file, run_command, or a guest CLI's own edit or shell tool. run_command counts as mutating because Relay cannot tell whether an arbitrary command writes. A later refinement may allow a short read-only allowlist (`git log`, `git diff`, `rg`, `ls`, `cat`) to run without a tree.

**Native panes (worker).**
1. `Pane::prepareWorkspace` stops calling `workspace_context prepare` at pane start in queue mode. It sends `tree_status: {state: "deferred"}` and starts the shell and worker at once. The shell already stays in the user's folder (aa7c277b).
2. The worker's executor gets `ensure_tree()`. Before any write tool or run_command, when the state is deferred, it calls `workspace_context.prepare` (the same allocation and quota as today), switches `workspace.root`, cwd and environment (`RELAY_WORKSPACE_ID`, `RELAY_BOARD_ROOT`) to the tree, then runs the call. If the quota refuses, the call fails with the reason and the pane stays usable for reading.
3. The worker emits `workspace_ready` so the GUI updates the pane's tree chip and `m_treeStatus`, and so `releasePreparedWorkspace` knows there is a lease to release. Protocol doc: a new event in `docs/AGENT-SESSIONS-PROTOCOL.md`.
4. Subagents already share the parent's tree (50ea2472); a subagent's first write triggers the same `ensure_tree` on the parent's identity.

**Guest panes (Claude Code, Codex).** A guest CLI's own Edit and Bash tools run in its cwd, so Relay cannot intercept them before the fact. Options, in order of preference: (a) start the guest in the canonical checkout with the harness's pre-tool hook (Claude Code `PreToolUse`, Codex equivalent) calling `relay-land workspace ensure` and restarting the guest session in the tree with `--resume` on the first write or Bash; (b) keep eager allocation for guest panes only. Decide after trying (a) on Claude Code.

**Safety.** Before the tree exists, an agent must not edit the canonical checkout. The write tools are the gate for native panes. For run_command before allocation, the command runs *after* allocation, never in the checkout.

**Cleanup.** A pane that closes without ever writing never had a tree, so nothing to release. Released trees already stop counting toward the quota (50ea2472); add a periodic reaper that removes released trees with no unlanded commits.

**Tests.** A worker unit test: read tools run with no allocation, and the first write allocates exactly once. A LiveGui test: a pane opens, the agent reads, `pane_leases()` is empty; the agent writes and one lease appears. The existing quota and restore tests keep passing.

**Expected effect.** Tree count tracks panes that actually changed code, not open panes; disk and quota pressure drop accordingly.
**Board workers (the tab helper, `RELAY_PANE_ID=switchboard`).** Same rule, and today they break it the worst way. Found on #4F5W, 2026-09-26:
- The tab's `BoardWorker` sends a `configure` with no `pane_token`, and the worker calls `workspace_context.prepare` at configure time (`backend/worker.py`, the `configure` branch). So every Board helper takes a slot when the tab opens, before anyone asks it anything. After the 10:16 restart this hit the 50/50 quota, the configure failed, and the Board sat on "Loading the Board…". #4F5W now loads the Board anyway, but the slot is still taken.
- With no token of its own, the worker falls back to `RELAY_SESSION_TOKEN` from the environment, which it inherits from the GUI or another pane. Observed: three Board workers under the GUI's leftover token `306e779c` sharing lease `wt6cfc25b9ea2d7cf5` (created 14:20:45 when one started a Codex guest), and one under pane `cabc279e`'s token, so it reacquired **that pane's** tree `wtf5c8365fe5d0a4c9` and would run its tools there.

Fix, as part of this card:
5. The Board worker's `configure` (`RelayWindow::startBoardWorker`, `src/RelayWindow.h`) sends `tree_status: {state: "deferred"}`. At configure it resolves the canonical project and Board root and allocates nothing, which is `prepare(..., planning_only=True)` today. Opening a Board, reading cards and every `board_*` write needs no tree: the Board is the canonical `.board/`, which workspaces exclude.
6. Its first mutating tool (write_file, edit_file, run_command, a guest's edit or shell) goes through the same `ensure_tree()` as a pane.
7. It gets a token of its own for that lease: a per-tab helper token, stable across restarts, so a reopened tab reacquires its own retained tree. It never inherits `RELAY_SESSION_TOKEN`: `startBoardWorker` removes it from the child environment, and the worker refuses to allocate under a token the GUI did not send.
8. Closing the tab (`releaseBoardWorker`) releases the helper's lease, as closing a pane does.

Tests: a worker test in which a Board `configure` plus `board_open` on a queue project allocates nothing and returns the cards; the helper's first `run_command` allocates exactly one tree under the helper token. A GUI or LiveGui check that N tabs with Boards open hold 0 leases until one of their agents writes.
