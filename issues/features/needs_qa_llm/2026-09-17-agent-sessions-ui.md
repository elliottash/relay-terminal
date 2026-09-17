# Agent sessions UI: model and effort, context, slash commands, rewind, fork, resume, recaps, export

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (GUI E1 subagent), 2026-09-17
- **Source**: owner decisions in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`; contract `docs/AGENT-SESSIONS-PROTOCOL.md`

## Behavior as implemented

- **Model and effort.** Picking a model while the pane is configured sends `set_model` (stored key); the conversation is kept and a toast says so. A per-pane effort picker sits next to the model picker; Alt+. / Alt+, (`agent.effortUp` / `agent.effortDown`, prompt box only) step low → medium → high → max and send `set_effort`. The default for new panes is Actions › Agent options › Default reasoning effort.
- **Context indicator.** `ctx 3.8k · 0.4%` in the prompt row from `context` events; the tooltip gives exact counts and the auto-compact limit; amber at 90% of the limit. "Compacting…" while `compaction_started` is pending; `compacted` prints an inline note with before/after tokens.
- **Slash commands.** Typing `/` opens a menu over the prompt box filtered by command-name prefix; ↑/↓ select, Enter runs (commands that need an argument are completed instead), Tab completes, Esc closes; the route label shows `COMMAND · /name · description`. Commands: /new, /clear, /model [name], /effort [level], /compact [focus], /context, /rewind, /fork, /resume, /plan, /recap, /agents (the subagents panel when a handler is installed, otherwise `agents_list` printed inline), /skills, /instructions, /export. Paths such as `/usr/bin/ls` are not affected: only exact command names run.
- **Rewind.** /rewind, Actions › Rewind…, or Esc Esc in an empty prompt box while the agent is idle (a single Esc still takes control of the terminal after 350 ms). A picker lists checkpoints newest first (turn, prompt, time, files) with Restore conversation and files (default), Conversation only, Files only, Fork from here. `rewound` prints what was restored and any conflicts, and puts the rewound prompt back in the prompt box when it is empty.
- **Fork.** /fork sends `fork`; the `fork_state` opens a new pane to the right, which sends `load_state` once configured and prints "Forked from “title” · N turn(s)".
- **Resume.** /resume lists saved sessions (title, updated, turns, model); Resume sends `resume`; the backend's recap prints inline in purple.
- **Recaps.** /recap asks for a manual recap. Away recap: when the window is re-activated after at least 3 minutes (`RELAY_RECAP_AWAY_SECONDS` overrides for testing), a turn finished while it was inactive, the prompt box is empty, at least 3 turns exist and no recap already covers this turn count, Relay sends `recap_request {reason:"away"}`. Actions › Agent options › Recap when you come back turns it off.
- **Export.** /export writes `<workspace>/.relay/exports/YYYY-MM-DD-HHMM-<title>.md` from the saved session (user/agent messages, tool calls, tool results in `<details>`) and opens it in a preview pane.
- **Agent options** (Actions › Agent options): Instructions…, Plans folder…, Compaction threshold…, Automatic turns from background agents… (`set_agent_options`, live), Excluded skills…, Default reasoning effort, AI next-command suggestions, Suggested next prompts, Recap when you come back. Settings that live in `configure` apply at once while the conversation is empty, otherwise at the next New chat.

## Implementer check (not a QA verdict)

Under Xvfb with an isolated `XDG_CONFIG_HOME` and the stored Kimi key (Kimi K3), in `docs/qa_evidence/2026-09-17-agent-sessions-ui/`:
- `implementer-slash-menu-filter.png`: `/co` → /compact, /context; route label COMMAND.
- `implementer-context-note-effort-max.png`: /context note, Alt+. to max (picker and toast).
- `implementer-rewind-picker-esc-esc.png`, `implementer-rewound-files-restored-prompt-back.png`: Esc Esc opened the picker; restoring turn 3 removed the agent's `subtract` edit and `test_calc.py`, and put the prompt back.
- `implementer-fork-new-pane.png`: /fork opened a pane on the right with the conversation.
- `implementer-resume-recap-inline.png`: /resume picker → session loaded, recap and next action inline.
- `implementer-away-recap-after-refocus.png`: 3 turns, focus moved to another X window during the third, refocus → one `recap_request` with reason away, recap printed.
- `implementer-compact-note-and-export-preview.png`: /compact note; /export opened the Markdown in a preview pane.
- `implementer-agent-options-submenu.png`.

Not verified live: switching to a different provider (only a Kimi key was available here; the `set_model` path is exercised by backend tests), the amber near-limit color, and conflicts in a rewind.

## Limitations

- /skills prints the skill count only; the backend has no skill listing event.
- A restored plan pane (closed-tab restore) has no owner pane, so its Execute buttons do nothing; reopen the plan from a new plan_written.
- Inline notes printed at an idle prompt whose line wraps (very long working directory in a narrow pane) can overlap the prompt redraw; this is the existing inline-output mechanism.
- /export reads the autosaved session file, so a turn still running is not included.

## QA checklist

1. Switch models mid-conversation; ask "what did I ask first?" — the answer uses the earlier conversation.
2. Alt+. / Alt+, and the effort picker; confirm with a reasoning-capable model.
3. Watch the ctx indicator grow; run /compact and /compact <focus>; with a small compaction threshold, see automatic compaction.
4. Type `/`, `/re`, Tab, Enter; type `/usr/bin/true` and confirm it runs in the terminal.
5. Let the agent edit two files over two turns; Esc Esc; try each restore option; edit a file by hand first to see a conflict.
6. /fork; continue both panes independently.
7. Restart Relay; /resume; check the recap.
8. Switch to another window for more than 3 minutes while a turn finishes; come back: one recap; switch away and back again with no new turn: no second recap.
9. /export and open the file.
