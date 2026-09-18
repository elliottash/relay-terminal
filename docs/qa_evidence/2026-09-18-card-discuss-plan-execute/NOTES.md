# A card's Discuss, Plan and Execute (#XS6Q, 2026-09-18)

Owner: *"rather than "ask the agent", lets have: plan / edit / discuss"*. Built as three buttons
under a card's reply box: **Discuss**, **Plan**, **Execute**. Design: `docs/SWITCHBOARD-DESIGN.md`
4.9. Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` 19.10.

## How this was run

Shots 01–03, 05, 08 and 09 come from a build of this work before the three fixes named below;
04, 06 and 07 from the staged tree of the commit (HEAD plus this work only, checked out into a
scratch directory), after them. Both were run as `relay --fresh --clean-shell` under a private **Xvfb `:187`** (1700x1000),
with its own `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` / `XDG_RUNTIME_DIR` / `TMPDIR`
and `provider/preset=glm-coding` in that config. The key came from the desktop keyring
(`RELAY_KEYRING` unset); it is in no file here. Model: `glm-5.3` for the Switchboard agent and the
Execute pane. Driven with `xdotool`, captured with `import`.

The workspace was a **throwaway git repo**, not this repository: `issues/board.yaml` and three
cards copied from `issues/` (`#SPBN`, `#WQFS`, `#ZW95`), plus a 20-line toy `src/complete.py`
whose folder completion appends `"/-"`, so that `#ZW95` ("Tab completion adds a stray "-" after a
folder") had code to plan against and fix. The repository's own `issues/` was never touched.
`workspace-after/` holds the three cards and their threads as the run left them, and
`workspace-git-log.txt` the throwaway repo's history.

Paid calls: one Discuss, two Plans, one Execute (a pane turn of 17 steps).

## The shots

| File | Shows |
|---|---|
| `implementer-01-three-buttons` | A card: **Comment**, **Discuss** (accent), **Plan**, **Execute** (outlined in the agent colour); the reply placeholder "Enter discusses, Ctrl+Enter plans, Ctrl+Shift+Enter only comments"; the key line with `e`, `d`/Tab, Enter, `p`/Ctrl+Enter, `x`. |
| `implementer-02-discuss-running-stop` | Enter sent a Discuss: the owner's entry reads "owner  Discuss · just now", the streaming reply is headed "✦ agent  Discuss", Discuss has become **Stop** and Plan / Execute are disabled. |
| `implementer-03-discuss-edited-the-card-before-render-fix` | The Discuss edited the card: retitled it and moved it Inbox → Discussing, through `board_update_card` / `board_move_card`. The thread has the update event, the `rewrite` entry with the old and new title, the move event and the reply naming both changes. **Found here:** the rewrite's "before"/"after" labels came out *under* the text they name (QTextDocument drops `<details>`/`<summary>`). |
| `implementer-04-discuss-edit-in-thread-and-execute-asks-first` | After the fix (`board::threadMarkdown`): "✦ rewrote title", **before** over the old title, **after** over the new. Also `x` on this card, which has neither a plan nor an acceptance: the line under the card asks once — "Execute again (x) to hand it over as it is, or Plan (p) first" — and nothing was written or opened. |
| `implementer-05-plan-section-first-run` | `p` on `#ZW95` in the list opened it and planned: Goal / Findings (with `src/complete.py::candidates` and the exact line) / Steps / Risks / Verify, found with `search_files` and `read_file`. **Found here:** the card said `## Plan` twice (the model repeated the heading inside `text`), and the reply ran "…Writing the plan.The plan is on…" together across the tool call. |
| `implementer-06-plan-running-and-next-time-p` | The **Plan** button clicked on `#WQFS` (acceptance, no plan): it is **Stop** while the plan is written, and the slow path taught its key: "Next time: p". |
| `implementer-07-plan-section-after-fixes` | That plan written, after both fixes: one `## Plan` (`_without_own_heading`), and the reply's two remarks either side of the tool call are two paragraphs. The thread: "owner  Plan", the "replaced `## Plan`" event, "✦ agent  Plan". |
| `implementer-08-execute-opens-the-pane` | `x` on `#ZW95` (it had a plan and an acceptance, so no question): a new terminal pane split to the right of the board, in the workspace, with its prompt chip showing `#ZW95`; its first turn is the task ("Execute #ZW95: … Carry out the plan until the acceptance holds", then the board's conventions). The card went to **In progress** ("Moved #ZW95 to In progress · Execute · Undo"). |
| `implementer-09-execute-pane-committed-and-moved-to-qa` | The pane's agent set `implemented_by: glm-5.3`, fixed `src/complete.py`, added `tests/test_complete.py`, committed `4b2063a fix(completion): folder candidates end in "/" not "/-" (#ZW95)`, recorded `commits 4b2063a` in the card's `links`, and moved it to **Needs QA (LLM)** with an evidence path and a QA checklist — all visible in the board's card header, in the middle. |

## What the thread file says

`workspace-after/thread-ZW95.md`: `owner … mode=plan` "Plan this card.", the plan event, `agent
… mode=plan` answer, the owner's move ("Execute: handed to a terminal pane"), the `progress` note
"Execute · handed to a new terminal pane …", then the pane agent's writes (`pane=<its id>`):
`implemented_by`, `links.commits`, progress comments and the move to Needs QA.

## Checks that are not pictures

- `tests/test_board_tools.py` (`CardTurnScopeTests`, `SearchFilesTests`): a Plan writes only its own
  card's `## Plan` — retitling, relabelling, the Issue, another card, a move and a create are
  refused with `board_mode_refused`; a repeated heading is written once; Discuss may rewrite and the
  thread keeps the old text; `search_files` skips `.env`, `.git` and binaries, stays in the
  workspace and does not exist outside a card turn.
- `tests/test_board_protocol.py` (`ModeTests`, `CardScopeAgentTests`): `mode` defaults to discuss,
  a Plan needs no words, the brief goes with a change of mode but not with a second Discuss, turn
  events and both thread entries carry the mode, the scope ends on done / error / cancelled, bad
  modes and empty Discusses write nothing, a Plan is refused during a cleanup; the worker's Agent
  offers no `run_command` / `write_file` / `edit_file` during a card turn and gets them back after.
- `tests/boardmodel_test.cpp`: the three buttons and their Stop/disabled states, Enter / `p` /
  Ctrl+Enter send the right `board_ask`, the thread's mode labels, Execute's ask-first and its
  `board_update` → `board_move` → `board_comment` → `onExecuteCard` sequence, the task text, and
  the rewrite's before/after order.
