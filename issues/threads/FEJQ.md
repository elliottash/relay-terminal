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
