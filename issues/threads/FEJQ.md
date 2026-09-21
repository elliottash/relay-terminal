<!-- relay:entry 20260919T230357Z-hd author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-allow-agents-to-control-the-relay-app-and-add-he.md

<!-- relay:entry 20260919T230612Z-4k author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T230612Z-p8 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
allow agents to control the relay app, and add helper agents in the options, sessions, and actions menus
```

</details>

<details><summary>after</summary>

```
allow agents to control the relay app, and add helper agents in the options, sessions, and actions menus.

give the agents skills to change options, run actions, or search the sessions manager, like they currently have for the switchboard.  

it can also open those panes if you ask it, and zoom to specific items / options.. if they cant yet, also allow agents to open the switchbaord and cards directly as well and zoom to them. 

when you are in options, actions, or sessions, you have a helper agent, same as the switchboard agent.
```

</details>

<!-- relay:entry 20260919T230613Z-es author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T231222Z-d6 author=agent kind=event model=glm-5.3 pane=switchboard turn=192e79b1af9749fbbae7b169ee006749/310eed9f8d034f6a8683ccde3db85331 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260919T231233Z-mb author=agent kind=comment mode=plan model=glm-5.3 turn=192e79b1af9749fbbae7b169ee006749/310eed9f8d034f6a8683ccde3db85331 -->
I have a full picture now. Writing the plan onto the card.

**Plan written on #FEJQ.** It copies the Switchboard-agent pattern: a new `AppTools` backend tool set (options get/set, action list/run, session search, `app_open` navigation) attached like `agent.board`, a GUI→worker app catalog riding the `keybindings` precedent, a new `app_command`/`app_command_result` round trip so the tool result reflects what the GUI actually did, and one per-window helper worker (new `helper` role) whose chat sits inside the Options, Actions and Sessions panes. Zoom-to-item uses the existing `revealOption(section, row)`, `openSessions(query)` and `openBoardCard(id)` entry points.

What it needs from you, when you have a moment (listed in the plan's Risks): the default set of options an agent may change (recommend excluding secret rows and Button rows), which actions count as `agent_safe` (recommend opt-in, deletes/resets off), and whether the main pane agent gets these tools on by default (recommend yes, behind an Options › Agent toggle).

<!-- relay:entry 20260920T040910Z-rr author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041534Z-we author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Discussing · moved in the Switchboard

<!-- relay:entry 20260920T140508Z-kj author=agent kind=event pane=switchboard -->
- ✦ agent updated this card · replaced `## Issue` and `## Plan`, retitled

<!-- relay:entry 20260920T140508Z-w5 author=agent kind=decision pane=switchboard -->
Owner, 2026-09-20, three decisions folded into the card: (1) **one joint system** — the Switchboard agent, the helper agents and "agents can control Relay" are one thing on one card (a split into two was tried and undone the same morning: a helper that can only describe an option is half a feature); (2) **Info and Activity get no helper** — those panes are about the pane's own agent, so that agent gets windowed `session_info` / `activity` read tools and an "Ask" row on each pane prefills its composer; (3) **the Switchboard is inside the system, not its precedent** — one per-window helper worker (the generalised `BoardWorker`, keeping the `switchboard` role so nothing migrates), one chat surface lifted from `BoardChatPanel`, one tool set (`board_*` + `app_*`) shared with every pane agent. Implementation goes to Opus subagents, one per plan step. Still the owner's: settable options, `agent_safe` actions, main-agent tools on by default, and whether the role's label becomes "Helper agent".

<!-- relay:entry 20260920T141153Z-eb author=agent kind=decision pane=switchboard -->
Correction (owner, 2026-09-20): the helper worker is **per tab**, not per window — Switchboards are per tab, and `RelayWindow::boardWorker(workspace)` already holds one per attached project, which is per tab because a tab holds one project. Options/Actions/Sessions in a tab ask the tab's worker; a tab with no project gets a board-less worker on its first ask. Goal, Decisions and step 6 corrected.

<!-- relay:entry 20260920T141302Z-ar author=agent kind=decision pane=switchboard -->
Owner, 2026-09-20: the same project in two tabs gets two Switchboards with two separate agents. So the helper worker is keyed by the **tab**, not the workspace (`boardWorker(workspace)` today would share one worker and one conversation between them): tab → worker, each configured with its tab's project, board files shared, conversation and queue the tab's own, persisted per (project, tab) under the tab's persistent id. Decisions and step 6 updated.

<!-- relay:entry 20260920T141732Z-wn author=agent kind=event pane=switchboard -->
- ✦ agent moved this card · Discussing → Executing · assignee agent

<!-- relay:entry 20260920T141732Z-wo author=agent kind=decision pane=switchboard -->
Owner, 2026-09-20: all five recommendations taken as written (settable = every value row but secrets; agent_safe opt-in starting with the reversible ones; main pane agent gets the write tools behind one Options › Agent toggle; `switchboard` relabelled "Helper agent", label only; per-tab helper starts on first ask) plus a sixth requirement: **it should be clear what's changed / done, and reversion / undo should be easy** — every agent write is shown as before → after with a one-click Undo the GUI performs itself, agents can list and undo their own changes, and a changed row carries a marker. Written into the card under "Decided by the owner". Execution starts now with Opus subagents, one per area, each landing its own files through scripts/land.py.

<!-- relay:entry 20260920T165116Z-sg author=agent kind=evidence pane=switchboard -->
**Integration: the five parts are one system, and it runs.** The panel, the app tools, the
per-tab worker, the Ask rows and the backend landed separately over the day; these commits wire
them into one thing and prove it live.

- `5263b353` Options and Actions carry the helper agent's panel, following the mode. One panel at
  the foot of the pane, collapsed, hidden until the window gives it somewhere to send, with the
  Sessions pane's seam name for name. `HelperChatPanel::setPane` moves the panel between
  `options` and `actions` with the mode instead of rebuilding it, so the log survives the swap.
- `c78c8004` `app_command`: the tab's helper is answered too, on its own pipe. Only a *pane*
  worker's commands were ever executed; a `BoardWorker`'s events went to the board views and the
  helper panels and no further, so every write the helper attempted waited out the 20 s deadline
  and came back `no_reply`. Both routes now build the answer in `appcommands::answerFor`.
- `6f16ff10` model box: one set of rows for all four panels (`relay::helpermodel`). Four boxes over
  one role and one worker have to agree about which model that is.
- `9c4b049c` the window joins the helper panels to the tab's worker, and one key asks.
  `RelayWindow::wireHelperPanel` does Options/Actions and Sessions from one template: send,
  listen, request ids, open-card/file/option/session, presets, the model box and the shortcut.
  Keymap `helper.ask` (Alt+Q) opens the helper of the pane the keyboard is in.
- `270509f2` the Switchboard's own answers link to an option and to a session too, since the agent
  answering there is the same one.
- `c2f158fd` what the live run found first: a worker event names itself in `event`, not `type` —
  reading `type` left every embedded panel showing "running" with an empty log while the worker's
  log said the turn was done in 628 ms — and the panel called itself "Switchboard agent" in its
  busy strip and invited questions about "the board itself" in every pane.
- `b5756ab0` Undo by hand answers the row's mark; Undo by the agent keeps it (§30.6). The row went
  back and then said "changed by the agent just now: on → off" about a revert the person had just
  performed by hand.
- `c3e8695c` the owner's wording for the collapsed row, 2026-09-20: "make the button say Helper
  Agent (Alt+Q)", "it should be at the bottom right rather than bottom left, and it should have a
  question mark icon next to it". One button, the live key inside its own text, a painted question
  mark beside it, right-aligned — the same in all three panes, because it is one helper.
- `caa07979` the last bug the run found, in the worker's own log: `protocol_error kind=board_chat
  msg="Configure a provider and workspace first."` `BoardWorker::start()` holds `configure` until
  the worker answers `ready`, but `send()` wrote straight to a process that was already running,
  so a message sent in that window went out **in front of** the configure — which is exactly what
  a helper panel's *first* ask does (§30.7: the worker starts on the first ask). The refusal came
  back as an `error` the panel does not draw, so the panel sat with its clock running and its log
  empty, and only the second ask ever worked.
- `1bfa5eeb` the evidence.

**Evidence:** `docs/qa_evidence/2026-09-20-agent-app-control/` — **29 checks, 29 passed** in one
run: `drive.sh`, `stub-provider.py`, 25 numbered screenshots, `notes.txt` (one PASS/FAIL line per
check, each naming its screenshot), the worker and app logs, and `NOTES.md` for what the run
proved, the four bugs it found and what it does not cover. Nothing called a provider: a loopback
stub answers both the pane agent and the tab's helper.

**Tests:** `ctest -R '^board$|^settings$|^conversations$|^appcommands$'` green, with new cases for
the panel following the Options/Actions mode, the `app_command` answer on either pipe, and the
undo marker.

**Commit `93e7b3da` carries the backend's message** — its content is the helper-panel rename
(`src/BoardChat.cpp` → `src/HelperChat.{h,cpp}`, CMakeLists), but its message is `c7a483da`'s,
word for word: a shared message file in the scratchpad, picked up by the wrong `land.py` commit a
minute apart. Both commits' **content** is correct and nothing was lost — only that one subject
line describes the other commit's change.

**What QA should still check.** The Actions mode of the Options pane live (the unit test covers
the panel following the mode; the live run asks it only in Options). `app_undo` asked for by the
agent through its own tool, as against the notification's Undo, which is covered. The helper's
conversation across a restart, and two tabs on one project across a restart — §30.7 wants the
conversation persisted per (project, tab) and §30.8 still lists that as not built; the backend
subagent is landing it separately. A real provider, since every decision in the run was scripted.
And `relay::appcommands::actionIsAgentSafe` lists a few keys that no longer appear in the action
catalog (`help.shortcuts` among them): harmless, but an agent told about one finds nothing to run.

<!-- relay:entry 20260920T165219Z-kh author=agent kind=progress pane=switchboard -->
Correction to the entry above: **the helper's conversation across a restart has landed** —
`3ddd2193`, persisted per (project, tab), and §30.8 documents it rather than listing it as not
built. It went in at 11:02, before the live run, so the run was on a build that has it; what the
run does not show is a *restart*, because each of its four phases starts Relay fresh with an empty
profile's worth of helper history. So QA's list loses "it is not built" and keeps "nobody has
watched a helper conversation come back": ask the Options helper something, quit, reopen the tab,
and see the log.

<!-- relay:entry 20260920T201900Z-pn author=agent kind=decision pane=terminal -->
Owner, 2026-09-20: "im having an issue where the sessions helper cant open panes because it says
its unsafe. can you cahnge that". Decision 2 amended — **opening a pane is agent_safe**:
`pane.splitRight`, `pane.splitDown`, `pane.splitLeft`, `pane.splitUp` and `closed.restore`, which
puts a closed pane back. It is the same line the decision already draws: a new pane holds nothing
of the person's until they put something in it, and the × in its header (`pane.close`, Ctrl+W) is
the one click that takes it back. `pane.close` itself stays off — it takes away whatever the pane
was holding. The helper reached for these because `app_open {target: "conversation"}`, which was
never gated (§30.4), needs a conversation to name, and "open a pane" has none; `app_action_run`
then answered `not_agent_safe`, which is the word the report calls "unsafe".

Landed in `relay::appcommands::actionIsAgentSafe()` with
`tests/appcommands_test.cpp::openingAPaneRunsAndClosingOneDoesNot` (a split runs through the
executor, `pane.close` is refused `not_agent_safe`) and the §30.2 list in
docs/AGENT-SESSIONS-PROTOCOL.md. `ctest -R appcommands` green. `writes_enabled` is unchanged: with
Options › Agent's toggle off, `run_action` is still refused for every key.
