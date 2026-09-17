---
id: CW8D
type: work
status: needs-qa-llm
component: [router, worker, agent, providers]
milestone: desktop-alpha
workstream: routing, agent (F2, backend)
assignee: implemented by Claude Opus 5 (Claude Code, backend F2 worktree), 2026-09-17
rank: ur
created: '2026-09-17'
acceptance: '`tests/test_routing_thinking_skills.py` (25 tests), updated `tests/test_provider.py`, live run in `docs/qa_evidence/2026-09-17-routing-thinking-skills/NOTES.md`'
source: '`docs/AGENT-SESSIONS-PROTOCOL.md` section 11. Owner: "make sure we have a perfect list of shell commands, that also updates automatically based on what programs you have. lets make an inclusive list of commands that are often used in natural language, eg go, install, etc -- in those cases, the agent reads your command and guesses whether you meant terminal or an agent" (model-assisted routing approved); thinking and tool calls must be easy to observe; skills can be refined and imported with review.'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Backend: routing assist, thinking events, turn records, skill refine and import

## Behavior

Routing (`backend/relay_core/router.py`, `route_assist.py`):

- `route` results carry `needs_assist` and `assist_reason`. A valid command whose first word is in the inclusive
  English-word list and whose input reads like a sentence gets `needs_assist: true`; the route is the local guess
  (agent, or shell for echo/printf-like commands). Valid commands without sentence signals stay shell; invalid ones go
  to the agent as before.
- Executables on PATH are scanned per directory and cached by directory mtime, so new installs route as commands
  without a restart.
- `route_assist {id, text, cwd?, mode?, timeout_ms?}` → one no-tools, low-effort call with the pane's provider →
  `route_assisted {id, route, confidence, reason, elapsed_ms}`, or `route: null` with `error` (`timeout`,
  `not_configured`, provider error). Runs on a background thread.

Thinking and turns (`provider.py`, `agent.py`, `queue.py`):

- `thinking_delta {turn_id, text}` from `reasoning_content`, `reasoning` or `reasoning_details`; `thinking_done
  {turn_id, elapsed_ms, chars}` before the answer. `delta` is answer text only; reasoning is still kept in messages.
- `turn_id` (the queue item id) on `agent_started`, `tool_started`, `tool_result`, `done`, `error`, `cancelled`;
  `call_id` on tool events. `turn_summary` right before the terminal event. `tool_output_get` → `tool_output
  {stored: true, ...}`; `turn_transcript_get` → `turn_transcript {items}`. Last 50 turns kept.

Skills (`skills.py`, `skill_manage.py`, `observe_protocol.py`):

- `skills_list`, `refine_skills` (copies to `~/.config/relay/skills/<name>/` with `refined_from`; originals
  untouched; refined copies win and the clash is reported), `import_skills_preview` / `import_skills_confirm`
  (pinned commit, https/ssh only, copies to `~/.local/share/relay/skill-imports/<repo>@<commit>/`, added to the
  default search path), `skills_check_updates` (`git ls-remote`, no automatic updates).

Deviations from the section 11 text are listed in the protocol doc, section 11.1. The main ones: `route_assist` uses
256 output tokens (20 truncates thinking models), accepts `timeout_ms`, and the reply to `tool_output_get` shares its
event name with streaming `tool_output {text}` chunks and is marked `stored: true`.

## Known gaps

- The 2 s default timeout is too short for Kimi K3 (0/6 answered live) and usually for GLM Coding (2/6); with 15 s all
  12 examples were routed correctly. The GUI should keep its local guess and may pass a longer `timeout_ms`.
- Import was tested against local `file://` repositories only (test-only flag), not a real https/ssh remote.
- Imported and refined skills join the search path only when `configure.skills.dirs` is not set.
- Preview clones live in a temp directory until confirmed or the worker exits; a crashed worker leaves them behind.
- No GUI changes in this workstream.

## QA checklist

1. `./scripts/test.sh` passes.
2. With a fake PATH containing `go`, `install`, `make`, `find`, send `route` for: `go build ./...` (shell, no assist),
   `go to the docs folder and summarize` (assist, agent), `make` (shell), `make the tests pass` (assist, agent),
   `install ripgrep` (assist, agent), `time make` (shell), `find . -name x` (shell),
   `find where the config is loaded` (assist, agent). Try your own English-word commands and look for false assists
   on normal commands.
3. Put a new executable into a PATH directory while the worker runs; the next `route` treats it as a command.
4. Configure a real provider; send `route_assist` for the examples with the default and with `timeout_ms: 15000`.
   Confirm one `route_assisted` per request, `route: null` on timeout, and that nothing is executed.
5. Run an agent turn with a thinking model: `thinking_delta` events before `thinking_done`, reasoning never in
   `delta`, `turn_id` on the listed events, `turn_summary` before `done`, then `tool_output_get` and
   `turn_transcript_get` for that turn. Cancel a turn mid-reasoning and check `thinking_done` and `turn_summary` still
   arrive.
6. `refine_skills` on a copy of a skill with `target_dir`: original bytes unchanged, name kept, description meaning
   kept, `refined_from` set, supporting files copied, `skills_list` shows the refined copy first and the original
   `shadowed_by` it (default target).
7. `import_skills_preview` on a public https skills repository (and a `ref`), check the pinned commit and item list;
   confirm a subset; check the copy location, that symlinks were not copied as links, and that `configure` without
   `skills.dirs` lists the imported skills. `skills_check_updates` reports `current` and `latest`. Refuse `http://`,
   `file://`, `ext::` and option-like URLs.
