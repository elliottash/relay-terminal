---
id: XS6Q
type: work
status: needs-qa-llm
labels: [feature, switchboard]
assignee: agent
implemented_by: Claude Opus 5 (1M context)
rank: zzzzs
created: '2026-09-18'
acceptance: a card offers Discuss, Plan and Execute; Discuss may edit the card and the thread shows it; Plan writes the card's plan and changes no code; Execute opens a terminal pane whose agent works the card, moves it to In progress and links its commits
source: issues/feature_intake.txt, 2026-09-18
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-card-discuss-plan-execute/], related: [], github: null}
---
# Replace "Ask the agent" on a card with Plan, Edit and Discuss

## Issue
rather than "ask the agent", lets have:
-- plan
-- edit
-- discuss

## Decisions
- 2026-09-18, owner: the card's single "Ask the agent" becomes three buttons — **Discuss**,
  **Plan**, **Execute**. Discuss is today's ask and also edits the card with agent and human input
  (the agent through `board_update`, hash-checked, the old text kept in the thread; the human with
  `e`). Plan writes or revises the card's plan and touches no code and no other card, reading the
  repository read-only. Execute hands the card to a real pane agent in a new terminal pane beside
  the board, moves the card to in-progress and links the pane's commits back. The mode is visible
  in the thread; `board_ask` gains a `mode` rather than new messages; Execute is GUI-side.

## Change
- **Protocol 19.10** (`docs/AGENT-SESSIONS-PROTOCOL.md`): `board_ask {mode: discuss|plan}`; both
  thread entries carry `mode=`, and so do `board_thread_appended` and the turn's events. A Plan may
  have no text. Design: `docs/SWITCHBOARD-DESIGN.md` 4.9.
- **What a mode may touch is enforced**, not only asked for (`board_tools.CardScope`, hooked into
  `Agent.tools()` / `Agent._prepare`): during a card turn the worker offers `read_file`,
  `list_directory`, skills and a new read-only `search_files`, never `run_command`, file writes,
  subagents or the terminal hand-off. Discuss keeps the ordinary board tools; Plan may only write
  its own card's `## Plan` and comment on it (`board_mode_refused` otherwise). Until now a card's
  "Ask the agent" ran with every pane tool.
- **Briefs** beside the policy: `backend/relay_core/board_discuss_brief.md`, `board_plan_brief.md`.
  The plan is the card's own `## Plan` (design 12.4: plan mode writes the plan onto the card).
- **Pane** (`src/BoardPane.cpp`): Comment · **Discuss** · **Plan** · **Execute**. Enter discusses,
  `p` / Ctrl+Enter plans (the box's text as the note), `x` executes, `d` goes to the reply box; `p`
  and `x` also work from the list. The running mode's button is Stop, the other two wait. The
  thread shows "owner  Plan · …", "✦ agent  Discuss · glm-5.3 · …". Button clicks hint their key.
- **Execute**: `board_update` assignee agent (hash-checked), `board_move` to in-progress, a
  `progress` note, then `RelayWindow`'s new `onExecuteCard` opens a terminal pane beside the board
  (main agent, the board's workspace) and `Pane::startBoardTask` submits the task with the card
  attached (`ask {cards}`), once the pane's agent is configured. The task (`board::executeTask`)
  tells the agent to set `implemented_by`, put `#ID` in every commit message, add each hash to
  `links.commits`, and land in `needs-qa-llm`. A card with no plan and no acceptance asks once, on
  the card.
- **Found and fixed on the way** (live run): a rewrite entry's before/after labels rendered under
  the text they name (`board::threadMarkdown`); a plan whose text repeated `## Plan` wrote the
  heading twice (`_without_own_heading`); the agent's remarks either side of a tool call ran into
  one line in a card answer (as the cleanup already avoided).

## QA checklist
- [ ] Open a card: the reply row reads Comment · Discuss · Plan · Execute, and the key line names
      `d`/Tab, Enter, `p`/Ctrl+Enter and `x`.
- [ ] Discuss (Enter) asking for a new title and a status change: the card changes, the thread
      shows the update and `rewrite` entries (before above the old text, after above the new) and
      a move event, and the reply says what changed. Both thread entries read "… Discuss".
- [ ] While it runs, Discuss is Stop (and stops it); Plan and Execute are disabled.
- [ ] Plan (`p`, the button, or Ctrl+Enter with a note) on a card whose repository has code: the
      card gets one `## Plan` with paths from the code; no file outside `issues/` changed
      (`git status`), no other card changed; thread entries read "… Plan". A click on Plan shows
      "Next time: p".
- [ ] Ask a Plan turn to change the title or run a command: it is refused (the tools say
      `board_mode_refused`, or name Execute), and nothing changes.
- [ ] Execute (`x`) on a card with a plan or an acceptance: a terminal pane opens beside the board
      in the card's workspace, its agent starts on "Execute #ID: …" with the card attached, the
      card is In progress and assigned to the agent, and the thread has "Execute · handed to …".
- [ ] Execute on a card with neither: the first press only asks, under the card; the second goes.
- [ ] Let the pane's agent finish: its commit message carries `#ID` and the card's `links.commits`
      has the hash.
- [ ] A Discuss or Plan while a cleanup runs is refused on the card with the text kept, as before.
- [ ] `ctest` (`relay-board-tests`) and `tests/test_board_protocol.py`, `tests/test_board_tools.py` pass.

## Implementer check
Claude Opus 5 (1M context), 2026-09-18. Live under Xvfb on a throwaway copy of three cards with
`glm-coding` (`glm-5.3`): one Discuss that retitled and moved a card, two Plans, one Execute whose
pane fixed the toy bug, committed with `#ZW95`, linked the hash and moved the card to Needs QA.
Evidence: `docs/qa_evidence/2026-09-18-card-discuss-plan-execute/NOTES.md`.
