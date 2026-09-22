# Relay tools available to guest agents

Card #GD8K, 2026-09-21. This describes the bridge in this checkout, not the tools in an
already-running guest session. Restart Relay's worker/guest session to load changed Python code
and repeat MCP discovery. Existing Codex-owned subagents are not imported into Relay.

## Delegation and tasks

The `relay_board` server exposes `agent`, `agent_message`, `agent_wait`, and `update_todos`,
as well as its five existing board tools. Delegation and tasks do not require a Board.
The worker checks that its bound agent actually has the requested capability before dispatch.

Guests create tasks with `update_todos`, then pass the returned `T<n>` id as `todo_id` to
`agent`. Relay supplies child ids (`a<n>`), task status, progress, completion, persisted child
threads, and the existing subagent view and header badge. The task list is included in each
parent guest turn so an update can preserve earlier tasks. Child reports queued beside a new
user message are forwarded together; the report does not replace the user's prompt.

Guest child launches always return immediately, even if `background: false` was sent.
`agent_wait` polls for at most ten seconds per call; retain each child id and wait on that id
until it finishes. `agent_message` sends work to a running child or resumes a finished one.
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

## Native tools still unavailable through this bridge

Availability of native tools depends on the attached project, console, terminal grants and
loaded groups. This inventory comes from `Agent.tools()`, `ToolExecutor.tools()`, BoardTools,
AppTools, ActivityTools and the deferred groups, rather than from a particular model's memory.

| Area | Native Relay tools not exposed through the bridge | Practical difference |
| --- | --- | --- |
| Real terminal/program | `run_in_terminal`, `type_into_program` | Codex shell commands use separate processes; they cannot drive the user's interactive terminal through Relay. |
| App controls | `app_option_list`, `app_option_get`, `app_option_set`, `app_action_list`, `app_action_run`, `app_panes`, `app_send_prompt`, `app_prefill_prompt`, `app_rename`, `app_sessions_search`, `app_open`, `app_changes`, `app_undo` | No structured Relay settings, pane control, conversation search/open, cross-pane prompting or app undo. |
| More board operations | `board_create_card`, `board_claim`, `board_sections`, `board_signals`, `board_merge_cards`, `board_split_card`, `board_import_items` | Use the documented file/CLI fallback where supported; bridge writes cover only list/read/comment/update/move. |
| Tests | `tests_check`, `tests_run` | Codex can run tests in its shell, but lacks these structured card-test checks/runs and their signal integration. |
| This Relay conversation | `session_info`, `activity` | No structured access to Relay's session metadata and timing history. |
| Planning and interaction | `write_plan`, `exit_plan_mode`, `ask_user`, `load_tools`, `set_keybinding` | Codex's own planning/questions are separate; these native Relay workflows are not exposed as MCP tools. |
| Files, shell jobs and skills | `run_command`, `command_output`, `stop_command`, `read_file`, `list_directory`, `write_file`, `edit_file`, `load_skill`, `read_skill_file` | Codex has shell/file/job equivalents and receives Relay's skill catalog, but does not use the native implementations, Relay job ids or their full checkpoint/SSH integration. |
| Board-console search | `search_files` | Conditional native board-console search is not part of the guest bridge; Codex can use `rg`. |

## Verification

`tests/test_guest_delegation.py` uses the real Relay manager and bridge with fake model providers:
startup discovery without a board; task/child links and completion; saved threads and live events;
follow-ups; request deduplication; scope and cancellation gates; bounded waits; parent prompt plus
child reports; clickable child tool results; guest-child startup/resume/cleanup; and Codex launch,
resume and fork overrides. It spends no live-model tokens.

Manual verification after restarting the worker: ask a Codex pane for two independent delegated
reviews, confirm task links and the live child count, open each child transcript, send a follow-up,
and stop a running child. This verifies the installed Codex and GUI together; unit and protocol
replay tests alone do not establish that end-to-end result.
