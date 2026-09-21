---
id: AG7R
type: work
status: discussing
labels: [feature, agent-app-control]
assignee: claude-code
waiting_on: owner
rank: m
created: '2026-09-20'
source: 'owner report, 2026-09-20, in the terminal pane after #FEJQ decision 2 was amended'
links: {plans: [], commits: [ab9e2ab7, 1a2459ed, c375a86e], evidence: [], related: [FEJQ, H6VQ, GMCF], github: null}
---
# What an agent still cannot do in Relay, read off the catalog rather than remembered

## Issue
after you fix that one, do a broader review of what the agent cant do, so we can review them
together

(The one being fixed: "im having an issue where the sessions helper cant open panes because it says
its unsafe. can you cahnge that" — landed `ab9e2ab7`, the four splits and `closed.restore`.)

## Decisions

Owner, 2026-09-20, answering the five questions:

> groups 1-3 all yes
>
> group 4, i want those too (i think, tell me if there is a risk)
>
> group 5, i want agents to be able to change hotkeys and maybe some of these other ones, new
> window / tab yes, i would want to lean on the side of allowing, argue to me for each why not.
>
> 6 not sure what i am supposed to do there
>
> 7 pass the toggle like app_open
>
> 8 allow sending messages and pre-filling messages across panes -- and address the other
> limitations here.

Settled, and being built:

- **Groups 1, 2, 3 — yes.** The twelve unreachable keys are made reachable, `run_action` gets a
  target pane, and both halves of group 3 go on: the window-scoped keys now, the pane-scoped ones
  (model, effort, input mode, plan toggle) once the pane argument exists.
- **Group 6 — nothing for the owner to do.** It was never a question; the card said so badly. The
  secret guard has never fired because no shipped row sets `secret`, and the fix is a test. Being
  written with group 1.
- **Group 7 — the open/reveal/focus subset passes the writes toggle**, the way `app_open` does.
  The reversible-but-writing entries (`*.reload`, the row buttons that test a key, detect servers
  or reorder models) stay behind it, so the toggle still means "may change things".
- **Group 8 — yes, and wider**: an agent may send a prompt to another pane and pre-fill a composer
  without sending, plus the rest of group 8's list. Queued behind group 2, which is where a pane
  gets a name a tool can use.

**Group 4 — the answer is yes, but not by marking them `agent_safe` as they stand.** See
"The risk in group 4" below: it is mechanical, not a matter of taste.

**Group 5 — the argument against each, as asked.** See "Group 5, key by key" below. Hotkeys are
already an agent's to change and have been since #GMCF; only the bulk wipe and the open-the-file
action are off.


## Discussion points

Measured against the code on `main` at `ab9e2ab7`, not against the protocol's prose. Method: every
`actionItem()` / `submenu()` / field-built `PaletteItem` in `src/RelayWindow.h` (93 keys), the
`Keymap` registry (93 registered actions), `appcommands::actionIsAgentSafe()` (33 keys), every
`SettingRow::Button`/`Buttons` in the options catalog, and the tool specs in
`backend/relay_core/`. Numbers below are from that sweep; each is checkable by grep.

Seven groups, in the order I think they want deciding. Groups 1 and 2 are faults and need no
decision from the owner — they need doing. Groups 3–7 are yours.

---

### 1. Twelve of the 33 "agent-safe" keys cannot be run at all. No decision, just broken.

`app_action_run` resolves a key against the **action catalog** — the palette rows the window
builds. These twelve are named safe in `actionIsAgentSafe()` but no palette row carries them, so
`AppCatalog.action()` raises `unknown_action` ("Relay has no action 'pane.focusLeft'"). An agent
that reads §30.2, believes it may move the focus, and tries it, is told the action does not exist:

| key | what the protocol promises | why it fails |
|---|---|---|
| `pane.focusLeft` `…Right` `…Up` `…Down` | "moving the focus between panes" | registry-only; no palette row |
| `window.next` `window.previous` | "…tabs and windows" | registry-only |
| `conversations.open` | "the session manager" | registry-only (the palette's row is `agent.resume`, which is **not** safe) |
| `palette.open` | "Actions" | registry-only |
| `help.shortcuts` | "the shortcuts page" | registry-only |
| `notifications.jump` | "jumping to a notification" | registry-only |
| `agent.agentsMenu` | "the agents menu" | registry-only |
| `agent.subagentPane` | "subagents" | registry-only |
| `palette.agent` | — | **retired key**: `src/Keymap.h:176` migrates it to `palette.open`. It exists nowhere else in the tree |

`app_action_list` does show eight of them, under section `Shortcuts` with `agent_safe: false`
(#GMCF lists registry actions with no palette row) — so the catalog contradicts the policy table on
the same key in the same answer.

Two ways out, and they are not exclusive: give these keys palette rows (`pane.focusLeft` as a
palette entry is arguably right anyway — the Actions pane is meant to be everything Relay can do),
or have `AppCommands::findAction()` fall back to the keybinding registry for a key that is in the
safe table. I would do the second — the table is the policy, and a policy that names a key the
executor cannot find is the bug — and delete `palette.agent`.

This is the #FEJQ thread's closing note ("lists a few keys that no longer appear in the action
catalog, `help.shortcuts` among them"), which turns out to be 12 of 33 rather than a few.

### 2. `run_action` has no pane. Everything lands on whatever pane the person is focused on.

`RelayWindow::runAction()` opens with `Pane *pane = m_active;` (`src/RelayWindow.h:1079`) and
`app_command` carries no pane. So a pane-scoped action asked for by the helper in tab 2 acts on the
pane the person happens to be sitting in.

Harmless for today's safe list (opening a pane and moving focus are window-scoped, and the splits
anchor on `m_activeLeaf`, so a helper's split does land beside the helper). But it is the thing
standing in front of **every** pane-scoped action in group 3 — model, effort, input mode, recap,
continue, stop. None of those can be turned on until `app_command` carries the asking pane's token
and `runAction` takes a target. Worth doing as its own change before the group-3 answers matter.

### 3. Reversible, in the catalog, never named. My recommendation is "yes" to each.

These pass decision 2's own test — the person sees it and takes it back in one click — and are off
only because the table was written from a short list:

| key | label | why it is reversible |
|---|---|---|
| `tests.open` | Test suites | opens a pane; `board.open` and `files.explorer` are already safe, this is the same pane class (#7BM4) |
| `app.about` | About | a dialog with one OK |
| `logs.open` | open the log folder | opens a folder; changes nothing |
| `theme.folder` | open the theme folder | same |
| `agent.screenshotPane` | Screenshot this pane | reads pixels; writes no setting |
| `pane.equalize` | Equalize pane sizes | a layout change undone by a drag — the weakest of these, and the one I would drop if you want the list shorter |
| `menu:closed` / `closed:<n>` | Recently closed → a specific pane | `closed.list` and `closed.restore` are both safe; restoring a *named* one is the same act |
| row button `declined:<path>` › **Undo** | "Undo" on a declined project | undo is the safe class by definition; the row just never set `agentSafeButtons` (`src/RelayWindow.h:3355`) |

Pane-scoped, and reversible, but blocked on group 2 — my recommendation is "yes, after group 2":

| key | label |
|---|---|
| `menu:model`, `model:<id>` | the pane's model |
| `menu:effort`, `effort:<level>` | reasoning effort |
| `menu:mode`, `input.modeAuto` / `…Terminal` / `…Agent`, `input.toggle` | input mode |
| `agent.planToggle` | plan mode on/off |

Each of those is a picker the person can move back with one click, and each is a thing people ask
a helper for in words ("put this pane on the local model").

### 4. Off because they open a modal the agent cannot answer — a different reason, and a better one.

Reversibility is the wrong question for these: running one parks a modal dialog on the person's
screen and takes the keyboard, and the agent has no way to fill it in or dismiss it. They should
stay off, and §30.2 should say *why*, because "not undoable in one click" is not the reason:

`files.open`, `project.pick`, `remote.join`, `remote.openShared`, `pane.share`, `agent.modelKeys`,
`agent.modelRoles`, `agent.instructions`, `agent.export`, `keybindings.edit`, `agent.rewind`,
`agent.rewindCode`, `agent.fork`, `ssh.connect`, `ssh:<host>`, `alias:save`, `alias:import`,
`project.init`.

If you want any of these reachable, the shape is a tool, not a palette key — `app_open` already
does this for Options/Actions/Sessions/Switchboard, where the agent names the destination and the
GUI does the opening with no dialog in between.

### 5. Correctly off, and I am not proposing changing any of them.

Destructive or outward-facing: `pane.close`, `windows.fresh`, `history.clear`, `hints.reset`,
`keybindings.clearOverrides`, `conversations.rebuild`, `project.detach`, `app.update`,
`pane.sharing`, `agent.stopAllSubagents`, `agent.clearQueue`, `terminal.clear`,
`terminal.interrupt`, `control.human`, `control.prompt`, `control.program.*`, `program.delegate`,
`agent.compact`, `agent.newChat`, `tab.new`, `window.new`, `tab.moveToNewWindow`,
`pane.moveToNewTab`, `pane.moveLeft/Right/Up/Down`, `pane.restartShell`, `terminal.native`,
`agent.interrupt`, `agent.continue`, `agent.stop`, `agent.recap`, `agent.resumeQueue`,
`ssh.splitSameHost`, `voice.toggle`, `helper.ask`.

(Six of those — `helper.ask`, `agent.interrupt`, `terminal.native`, `pane.restartShell`,
`ssh.connect`, `agent.provider` and the prompt-box keys `agent.modelBox`, `agent.effortBox`,
`agent.effortUp`, `agent.effortDown` — are registry-only like group 1, so they answer
`unknown_action` rather than `not_agent_safe`. Same outcome today; it matters only if group 1's
fallback is built, which would make them reachable and then refused on the policy, which is the
right answer.)

Two I would hear an argument about: **`tab.new`** and **`window.new`** are exactly as reversible as
the splits that just landed (Ctrl+W), and "open these in a new tab" is a thing people say.
**`agent.recap`** and **`agent.continue`** are not reversible but cost nothing but tokens. Say the
word on any of the three.

### 6. The options catalog: the secret guard has never been exercised.

`SettingRow::secret` — decision 1's "every value row except a secret" — is **set on no row in the
shipped catalog**. Only `tests/appcommands_test.cpp` sets it. That is not a leak today: API keys
are not rows at all, they live behind the `agent.modelKeys` dialog, and the only `Text` rows in
Options are the skills exclusion, the plans folder, the default project, the compaction threshold,
the microphone and the local server address. But nothing stops the next key-shaped row being added
without the flag, and the person who adds it will not have read §30.8. A `settingsSections()` test
that fails on a row whose id or label matches `key|token|secret|password` and is not marked would
cost one test and close it. I would just write it; it is not a decision.

Of the eight Button/Buttons rows in Options, five mark a button safe (local endpoint Test/Refresh,
Find servers, Detect, the model-tier reorder). Off and right: Reset to defaults, Pair a phone,
remove a model, rename/delete a profile. Off and probably wrong: the `declined:` Undo above, and
`local:address/save` / `found:<n>` **Save** — saving a local endpoint the agent just detected is
the other half of Detect, which is already safe, and it is one `remove` to take back.

### 7. `writes_enabled` refuses opening a pane, which is not a write.

`AppCommands::execute()` refuses **every** `run_action` when Options › Agent's "Agents may change
options and run actions" is off (`src/AppCommands.cpp:418`), while `app_open` is deliberately
exempt (§30.4: opening is not a write). So with the toggle off, a helper can open a conversation
into a new pane through `app_open`, but cannot open an empty pane through `run_action` — the same
act, two answers. Either the open/reveal/focus subset of the safe table should pass the toggle the
way `app_open` does, or `app_open` should be gated too. I would do the first; it makes the toggle
mean "may change things", which is what it says.

### 8. What has no tool at all — the gaps that are not the safe table's fault.

- **No agent can put a prompt into another pane.** The Sessions helper can open three conversations
  and cannot say a word to any of them. `run_in_terminal` and `type_into_program`
  (`backend/relay_core/agent.py:522`) reach the pane their own worker belongs to and nothing else.
- **`app_open` reaches four panes**: `options`, `actions`, `sessions`, `switchboard`, plus
  `conversation`. Not the file explorer, Test suites, Activity, ⓘ, requests or subagents — each of
  which has an action instead, and two of those actions are in group 1's unreachable twelve.
- **Nothing names a tab or a window.** No tool takes a tab id, so "open this in the other tab" has
  no expression. Renaming is not an action either: `/rename` and `/rename-tab` are slash commands
  typed into a composer, and no agent can type into a composer, so "call this pane «deploy»" cannot
  be asked of one.
- **The helper cannot read the pane it is helping.** It gets the options and actions catalogs and
  the session index; `session_info` and `activity` are the *pane* agent's tools (#FEJQ: "Info and
  Activity get no helper of their own").

## Planning notes

The five questions above were asked and answered on 2026-09-20; the answers are in `## Decisions`.
What follows is the two things the owner asked me to come back with.

### The risk in group 4, and it is not the one you would expect

Not "the dialog does something bad". The risk is that **the tool call hangs and the window freezes
behind a dialog nobody asked for**, and it is mechanical rather than a matter of taste:

`RelayWindow::executeAppCommand()` returns `appCommands().execute(command, who)` synchronously
(`src/RelayWindow.h:1713`), and `AppCommands::execute()` calls `item.run()` inline
(`src/AppCommands.cpp:427`) and builds the `app_command_result` from what comes back. Every one of
group 4's actions opens a **modal** — `pickFileForPreview()` ends in `dialog.exec()`
(`src/RelayWindow.h:739` and `:752`; two more at `:2307` and `:5337`), which spins a nested event
loop. So `run()` does not return until the person dismisses the dialog. Three consequences:

1. **The result never goes back down the pipe** until the dialog closes, so §30.3's 20-second
   deadline expires and the agent is told `no_reply`. That is exactly the failure the owner
   reported in #H6VQ — "sessions helper didn't do anything" — arrived at by a different route.
2. **The modal blocks the window.** Until it is dealt with, the person cannot type in any pane of
   that window. An agent asked something in tab 3 can stop someone working in tab 1.
3. **The agent cannot dismiss what it opened**, so decision 2's promise inverts: instead of the
   person taking a change back in one click, the person is *forced* to click to get their app back.

So the shape is a two-step, and both steps are worth doing:

1. **Make those `run()`s non-blocking** — `dialog->open()` with a finished-callback, or
   `QTimer::singleShot(0, ...)`, instead of `exec()`. `run()` returns at once, the result goes
   back, the dialog appears. Mechanical, and testable at the AppCommands layer without a window.
   After this, marking them `agent_safe` does what the owner asked and carries none of the risk.
2. **Where the agent wants the outcome rather than the picker, give it the outcome.** `app_open`
   already has this shape: the agent names the destination and the GUI does it with no dialog at
   all. `files.open` -> `app_open {target: "file", path}` is strictly more useful than making a
   file chooser appear in front of someone; the same goes for `project.pick`, `agent.export` (a
   path) and `agent.instructions` (a named file). The picker stays, for the person.

### Group 5, key by key: the argument against

Asked for by the owner ("argue to me for each why not"), with the honest strength of each. The
owner's lean is toward allowing, so these are grouped by whether I can make an argument I believe.

**Already allowed — the question does not arise.** *Changing hotkeys is an agent's to do today.*
`set_keybinding` has existed since #GMCF, and both the tab's helper and the pane agent get it
whenever the GUI sent the keybinding catalog (`Pane::sendKeybindings()`, `src/Pane.h:2774`, and
`keybindings=` in `board_protocol._build_page_agent`). Ask any pane agent to move a shortcut and it
moves. What is off is only `keybindings.edit` (opens the JSON in an external editor — group 4's
modal problem) and `keybindings.clearOverrides` (below).

**No argument I believe — turn them on.** `tab.new`, `window.new` (the owner said yes; they are as
reversible as the splits that landed in `ab9e2ab7` — Ctrl+W), `pane.moveLeft/Right/Up/Down`,
`pane.moveToNewTab`, `tab.moveToNewWindow` (moving back is the undo; the only cost is a layout that
shifts under you mid-task, which is startling and not harmful), `hints.reset` (brings the "next
time:" hints back — an agent teaching someone a shortcut is a good use of it), `terminal.native`
(a toggle, one click back), `helper.ask` (focuses a composer), `ssh.splitSameHost` (a pane on the
host you are already connected to; with splits allowed this is the same act),
`conversations.rebuild` (idempotent re-index; the only cost is time, and it can be slow),
`agent.newChat` (milder than it sounds — the conversation is saved and resumable from Sessions),
`agent.recap`, `agent.continue`, `agent.resumeQueue` (they spend tokens, which #RCPF has already
decided is acceptable on the owner's behalf).

**A real argument, bounded — the owner's call.**

- `pane.close`. The closed list does bring the pane back, with its directory, its layout and its
  conversation — but **not its running process**: `WindowManager::reopen()`
  (`src/WindowManagerImpl.h:395`) rebuilds from the saved layout. Close a pane running a build, an
  ssh session or a long command and that work is gone with no undo. The honest version is "safe for
  an idle pane, unsafe for a busy one", which the executor cannot tell apart today.
- `terminal.clear`. Wipes the scrollback the person may be reading. The session log keeps the text,
  so it is recoverable — just not where they were looking.
- `agent.clearQueue`. Deletes prompts **the person typed** and queued. Someone else's words are the
  one thing I would not have an agent throw away unasked.
- `agent.compact`. Irreversible: the detail the compaction dropped is gone. Cheap to ask for,
  expensive to be wrong about.
- `terminal.interrupt`, `agent.interrupt`, `agent.stop`, `agent.stopAllSubagents`. Each kills work
  in flight. Stop is also the person's emergency brake, and an agent that can press it can press
  its own. None of them can be aimed today — they hit the focused pane — so they are blocked on
  group 2 whatever the answer.
- `pane.restartShell`. Kills the shell and its children in order to restart it.
- `project.detach`. Reversible only through `project.pick`, which is a modal — so its undo sits
  behind group 4.

**I would hold the line on these seven, and here is why for each.**

1. `voice.toggle` — **it switches a microphone on.** The cost of a wrong "on" is recording a room
   that did not consent; the benefit is saving one click. No undo makes that trade worse, not
   better.
2. `pane.share` / `pane.sharing` — publishes a pane to other people over the network. Not
   reversible in the way that matters: revoking a link does not un-see what was on screen, and
   #W5N2's whole shape (multi-use links, email on join, no auto-admit) is about the person choosing
   this deliberately.
3. `app.update` — downloads a binary over the network and **restarts the app the person is working
   in**, killing every shell and agent in every window. There is no undo at all.
4. `control.human`, `control.program.agent`, `control.program.human`, `program.delegate` — these
   hand the terminal between the person and the agent. An agent granting itself control is
   circular: the thing being decided is whether the agent is in charge. This is the only refusal in
   the list I would defend on principle rather than on consequences.
5. `windows.fresh` — discards the saved window set: the person's entire arrangement, no undo.
6. `keybindings.clearOverrides` — wipes **every** custom shortcut in one call, and the overrides
   file is the only copy. This is not "agents may not change hotkeys" — they may, one at a time,
   deliberately, through `set_keybinding`. A bulk wipe is a different act.
7. `history.clear` — deletes terminal history. Irreversible, and it is the person's record.

If the owner wants any of the seven anyway: 1-4 are the ones I would ask him to say out loud on the
card, so the decision has a date on it. 5-7 I will turn on without further argument if he says so,
since they only destroy the person's own state and the person is the one asking.

### Order of work

1. **Landed** — groups 1, 3 (window-scoped), 6 and 7, in `1a2459ed` and `c375a86e`. See the
   thread entry for what each became.
2. **Next** — group 2: `app_command` carries the asking pane, `runAction` takes a target. Then
   group 3's pane-scoped keys, and every "aim it at a pane" entry above becomes answerable.
3. **Then** — group 8, which needs a pane to have a name a tool can use, so it follows group 2:
   send a prompt to a named pane, pre-fill a composer without sending, `app_open` for the remaining
   panes (explorer, Test suites, Activity, info, requests, subagents), naming tabs and renaming
   panes, and the helper reading the pane it is helping.
4. **Group 4** — the non-blocking `run()` pass, then the marks, then the direct-path tools.
5. **Group 5** — on the owner's answers to the two lists above.
