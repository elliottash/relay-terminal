# Relay tools available to guest agents

Card #GD8K, 2026-09-21. This describes the bridge in this checkout, not the tools in an
already-running guest session. Restart Relay's worker/guest session to load changed Python code
and repeat MCP discovery. Existing Codex-owned subagents are not imported into Relay.

## Delegation and tasks

The `relay_board` server exposes `agent`, `agent_message`, `agent_wait`, and `update_todos`,
as well as the native Board tool catalog. Delegation and tasks do not require a Board.
The worker checks that its bound agent actually has the requested capability before dispatch.

Guests create tasks with `update_todos`, then pass the returned `T<n>` id as `todo_id` to
`agent`. Relay supplies child ids (`a<n>`), task status, progress, completion, persisted child
threads, and the existing subagent view and header badge. The task list is included in each
parent guest turn so an update can preserve earlier tasks. Child reports queued beside a new
user message are forwarded together; the report does not replace the user's prompt.

`agent` uses the native foreground or background behavior: a foreground call returns the
child's report, while a background call returns its id immediately. `agent_wait` accepts the
native 1–1800 second timeout. `agent_message` sends work to a running child or resumes a finished one.
Relay's existing manager owns cancellation, concurrency and completion wake-ups.

A child uses Relay's configured subagent model resolution. If that resolves to a guest model,
it starts a separate guest harness on the child's worker thread, closes the process at the end
of its turn, and resumes the same guest session for follow-up work. Its bridge exposes no task
or delegation tools. Read-only definitions use the guest's `deny` permission posture; guest
harness tools otherwise remain the guest's, rather than Relay's `RestrictedExecutor` tool list.

Relay-launched Codex sessions disable `features.multi_agent` and `features.multi_agent_v2`
in process and thread configuration, including resume and fork. This leaves the user's Codex
configuration untouched. Instructions require Relay delegation and prohibit launching an agent
CLI through the shell. This is routing enforcement for Codex's built-in subagent tools, not an
OS-level prohibition on arbitrary shell programs. Claude shares the expanded bridge and guidance;
this change does not disable Claude's internal delegation tools.

## Native and guest capability mapping

Availability of native tools depends on the attached project, console, terminal grants and
loaded groups. This inventory comes from `Agent.tools()`, `ToolExecutor.tools()`, BoardTools,
AppTools, ActivityTools and the deferred groups, rather than from a particular model's memory.

| Area | Guest capability | Practical difference |
| --- | --- | --- |
| Board | All ordinary Board tools, including `board_create_card`, `board_claim`, `tests_check`, `tests_run`, `board_signals` and `board_try`, come from the live Board catalog. | Console-only merge, split and search, and cleanup-only sections remain scoped to those native contexts. |
| Relay app and session | The app catalog and `session_info`/`activity` are bridged. | The same write toggle, safe-action markers, pane targeting and worker-owned history apply. |
| Real terminal/program | `run_in_terminal` and `type_into_program` are bridged. | Program typing is refused unless the person handed over the visible program for this turn. A guest's shell is separate from that terminal. |
| Planning/keybindings | `write_plan`, `exit_plan_mode` and `set_keybinding` are bridged. | Plan mode and a live keybinding catalog govern calls. |
| Questions and skills | Guests use their own structured question tools and file tools to read Relay's injected skill catalog. | These are capability equivalents rather than identically named MCP tools. |
| Local files and shell | Guests use their harness tools locally; Relay's bridged command/file tools are for an active SSH host and require `host`. | Relay's local job ids are not shared with the guest's shell jobs. |
| Delegation | Relay `agent`/`agent_message`/`agent_wait` remain bridged. | Foreground/background selection and wait duration follow the native manager. |

Guest MCP discovery can precede Agent binding. Static schemas for app, session, keybinding and
program tools are advertised then; execution checks the live catalog, pane scope and per-turn
grant. `tests_run` can wait for the native 30-minute ceiling. Foreground delegation can run
longer; Codex's process-local MCP timeout and the proxy socket are set to 24 hours for these calls.

## Verification

Card #GPA8: `tests/test_guest_board_bridge.py` compares native pane tools with bridged tools
and exercises creation, claim, app writes and refusals, own-session reads, keybindings, program
handover and a named test run. Both installed managed clients completed disposable MCP turns
covering Board creation/claim and app/session/test discovery. The commands, results and limits
are recorded in `docs/qa_evidence/2026-09-23-GPA8-guest-tool-parity/verification.md`.

`tests/test_guest_delegation.py` uses the real Relay manager and bridge with fake model providers:
startup discovery without a board; task/child links and completion; saved threads and live events;
follow-ups; request deduplication; scope and cancellation gates; native waits and foreground Stop; parent prompt plus
child reports; clickable child tool results; guest-child startup/resume/cleanup; and Codex launch,
resume and fork overrides. It spends no live-model tokens.

Manual verification after restarting the worker: ask a Codex pane for two independent delegated
reviews, confirm task links and the live child count, open each child transcript, send a follow-up,
and stop a running child. This verifies the installed Codex and GUI together; unit and protocol
replay tests alone do not establish that end-to-end result.
