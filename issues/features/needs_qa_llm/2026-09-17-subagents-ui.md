---
id: M9T4
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent (E2, subagents UI)
assignee: implemented by Claude Opus 5 (Claude Code, GUI E2 worktree), 2026-09-17
rank: xo
created: '2026-09-17'
acceptance: '`tests/subagents_test.cpp` (ctest `subagents`), live run in `docs/qa_evidence/2026-09-17-subagents-ui/`'
source: '`issues/features/2026-09-17-agent-sessions-planning-subagents.md`, `docs/AGENT-FEATURES-RESEARCH.md` (design C, Claude Code style), `docs/AGENT-SESSIONS-PROTOCOL.md` section 8, `issues/features/needs_qa_llm/2026-09-17-backend-subagents.md` (event names and deviations)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Subagents UI: running-agents list, live transcripts, stop keys

## Behavior

Code: `src/SubagentsPanel.h/.cpp` (`relay::SubagentModel`, `relay::SubagentsPanel`),
`src/SubagentTranscript.h/.cpp` (`relay::SubagentTranscriptView`), and hooks in `src/main.cpp` marked
"subagents UI".

Running-agents list

- Shown only while the pane has subagents. A `main` row (working or idle, then the token split
  `main ctx 4.3k · agents ~25.4k tok`), then one row per subagent: status icon (○ waiting, ● running, ✓ done,
  ✗ failed, ■ stopped), `type id`, description plus the latest activity while it runs, then outcome (when
  finished), elapsed time (counts locally between progress events), tool count, tokens (`~` = estimated),
  a `bg` badge for background agents, and a × at the right edge. At most 5 subagent rows, then "+N more".
- The list floats over the bottom of the terminal, right above the composer, like the queue strip. The queue
  strip moves up above it. The list hides while the composer is hidden (human control, full-screen programs).
- Down on the composer's last line enters the list when history is at the draft, no @ popup is open, and no
  queue item is selected. Up stays with the queue and history. In the list: Up/Down move, Home/End,
  Enter opens the transcript (Enter on `main` returns), `x`/Delete/Backspace stops a running agent or dismisses a
  finished row, Esc or Up past `main` returns to the composer. Mouse: click selects, double-click opens,
  × stops or dismisses.
- Finished rows stay until dismissed, until the next user agent prompt starts (fix turns and Relay wake-up
  turns do not clear them), or until New chat. A worker restart clears the list.

Transcripts

- Enter opens a transcript pane split right of the terminal pane. When the window is under 1000 px wide,
  it opens as an overlay over the right 60% of the pane instead. An open transcript for the same agent is
  focused, not duplicated.
- Opening sends `agent_subscribe {id, on: true}`. The `subagent_transcript` snapshot renders first (prompt,
  assistant text, tool-call names, tool results cut to 12 lines), then "── live ──" and the streamed
  `subagent_event` payloads: deltas, `⚙ $ command` / `⚙ read path` lines, tool output, exit codes and errors.
  Control characters are stripped. Closing the last view for an agent sends `agent_subscribe {on: false}`.
- Title `✦ type id · description` (tab title too); status line with status, elapsed, tools, tokens,
  background, and the latest activity. Tooltip shows model and effort.
- The message box sends `agent_message {id, text}`. A running agent reads it before its next step; a finished
  one resumes in the background. The status bar shows `agent_message_delivered`. Esc in the box or × closes the
  transcript; the agent keeps running.
- Transcript panes are not saved with the layout and closing one is not recorded for Ctrl+Shift+W.

Terminal output

- One dim `✦` line when a subagent starts (`✦ explore a1 started in the background · …`, `resumed` after a
  message to a finished agent, definition warnings in parentheses) and one when it finishes
  (`✦ explore a1 done · 1:16 · 7 tools · 23.1k tok · background agent finished → main agent continues`).
  The handoff part names `wake`, `next_model_call`, `pending` (with `wakeups/max_auto_turns`), `returned`, or
  `discarded`. A later `subagent_handoff` prints `✦ a1 result → main agent continues` or the pending text.
  Subagent tool activity never reaches the terminal.
- A finished subagent calls the existing `notifyIfAway` (taskbar alert and `notify-send` when the window is
  inactive).

Commands and keys

- `agent.stopAllSubagents`, default Ctrl+Shift+X (acts inside programs with the default `shift-only` rule):
  `agent_stop {id: "all"}`. It reports "No running agents to stop." when none run.
- `agent.agentsMenu` (no default key) opens the palette at Agents.
- Palette, Agent section: "Agents…" (detail: N running) lists running and finished agents (Enter opens the
  transcript), then every definition from `agents_list` as `name   source · model · tools` with the
  description as tooltip (Enter puts "Use the NAME agent to " in the prompt box), then "Reload agent
  definitions" (tooltip lists skipped files and duplicates). "Stop all agents" sits next to it.
  Definitions are requested after every `configured`.
- The model picker tooltip also shows the token split.

## Deviations

- The list floats above the composer instead of sitting under it. The first build put it in the layout under
  the composer; when the list appeared mid-turn the terminal shrank, and Readline redrew its prompt in the middle
  of the agent's output (`implementer-01-first-build-list-in-layout-prompt-redraw.png`). Down still enters it.
- Main-agent tokens are the context size from the `context` event (`used_tokens`), not cumulative usage; agent
  tokens are the sum of the subagents' reported tokens.
- Finished rows do not expire after 60 s (research note); they stay until dismissed or the next user prompt.
- No `m` key to jump to the message box: Enter opens the transcript with the message box focused.
- `/agents` slash command: no slash menu on main at the time of writing. TODO: when the slash menu lands, map
  `/agents` to `agent.agentsMenu`.

## Implementer check (not a QA verdict)

- `ctest`: editor, filepanes, subagents (13 cases: lifecycle, handoff texts and the cap, transcript routing,
  observed main events, dismiss/clear, resume, formatting, definitions, panel keys, 5-row cap, transcript view
  snapshot/stream/escape stripping/message box), backend-and-bash. `./scripts/test.sh`: 200 tests OK.
- Live under Xvfb (isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`, Kimi K3 with the stored key picked by the
  app's normal startup configure), screenshots in `docs/qa_evidence/2026-09-17-subagents-ui/`:
  1. Main agent started a background `explore` on a fixture folder and answered 17 × 23 meanwhile. The finish line
     printed and the automatic main turn ran and summarized the three files (`implementer-02`).
  2. Two background agents: list with both rows, `bg` badges, activity, token split (`03`). Down entered the list,
     Down moved to a2 (`04`), Up + Enter opened a1's transcript split right with the snapshot and live tool
     lines (`05`).
  3. Sent "Also count the total number of lines…" from the transcript box to the running agent: status bar
     "Message sent · a1 reads it before its next step" (`06`); its final report included "Total lines across all
     files: 5", then done and "background agent finished → main agent continues" (`07`).
  4. Alt+Left, Down, Down, `x` stopped a2 (■ stopped) and a Relay wake-up turn reported it (`08`).
  5. Palette › Agents… listed both agents and the built-in `explore`/`general` definitions (`09`).
  6. Window resized to 900 px: Enter opened the overlay transcript (`10`); Esc closed it; Ctrl+Shift+X stopped
     the running agent (`11`); `x` dismissed a finished row (`12`); a new prompt cleared the remaining finished
     row and the list hid (`13`).
- Not checked live: `notifyIfAway` (no window manager under Xvfb), `pending` handoff past `max_auto_turns`
  (unit-tested text only), more than 5 rows (unit-tested height only), foreground subagents.

## Gaps

- No GUI field for `max_auto_turns` (`set_agent_options`); the worker default (50) applies.
- The floating list covers the terminal's last lines while subagents exist (the queue strip does the same).
- Long prompts that wrap in narrow panes still leave an extra prompt line around inline notices (existing
  inline-output behavior, not specific to subagents).
- Transcript panes are not restored with layouts and do not survive a worker restart (the old view stays
  open but gets no more events).
- The transcript shows the snapshot's tool results as raw JSON text.

## QA checklist

1. With no subagents the list is hidden. Ask the agent for a background `explore` task; the list appears above
   the composer without the terminal resizing or a prompt being redrawn inside the agent's output.
2. Exactly one start line and one finish line per subagent appear in the terminal, dim with ✦; no subagent
   tool lines.
3. Down on an empty composer enters the list; Up in the composer still selects queue items and history;
   with the @ popup open, Down moves in the popup.
4. Enter opens a split transcript (wide window) or an overlay (under 1000 px); the snapshot shows earlier
   steps and new tool calls stream in; closing it stops the stream (no `subagent_event` after close).
5. A message to a running agent changes its report; a message to a finished agent resumes it
   (row goes back to running, "resumed" line).
6. `x` on a running row stops it; on a finished row dismisses it. Ctrl+Shift+X stops all, also while vim runs
   in the terminal.
7. With the window inactive, a finishing agent flashes the taskbar and sends a desktop notification.
8. After a background agent finishes while the main agent is idle, the `✦ … → main agent continues` line
   precedes the automatic main turn. With `max_auto_turns` 1 and two finishes, the second line says the
   result is pending.
9. Palette › Agents… lists definitions from `.claude/agents` etc. with source, model and tools; Enter on one
   fills the prompt box; "Stop all agents" works.
10. Finished rows stay after the automatic turn, and clear when the user sends the next agent prompt or New chat.

## Merged with sessions UI (2026-09-17)

Merged into main together with the agent sessions UI (commit eed2b91). Conflicts were only in
`src/main.cpp` and were resolved keeping both features: both header sets; both Pane API blocks;
both helper blocks; event dispatch lets the subagents model observe/consume events before the
sessions handler; `ToolPane::Kind` is now `{Explorer, Preview, Plan, Subagent}` with path, title,
node and focus handling for all four; both pane callbacks are wired. `/agents` in the slash menu
now opens the subagents panel's Agents menu (`onShowAgents` → `openAgentsMenu`), resolving the
earlier TODO. `max_auto_turns` is in Actions › Agent options (from the sessions UI). The model
picker tooltip now says switching keeps the conversation.

Smoke under Xvfb (Kimi K3, isolated config): slash menu `/context`, Shift+Tab PLAN chip, queue
strip and agents list shown together, background explore agent with automatic main turn, Down
into the list, Enter opens the transcript split, `/agents` opens the Agents menu with running and
defined agents, stop marks the agent stopped. Evidence:
`docs/qa_evidence/2026-09-17-merge-sessions-subagents/`.
