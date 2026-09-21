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
links: {plans: [], commits: [], evidence: [], related: [FEJQ, H6VQ, GMCF], github: null}
---
# What an agent still cannot do in Relay, read off the catalog rather than remembered

## Issue
after you fix that one, do a broader review of what the agent cant do, so we can review them
together

(The one being fixed: "im having an issue where the sessions helper cant open panes because it says
its unsafe. can you cahnge that" — landed `ab9e2ab7`, the four splits and `closed.restore`.)

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

Questions for the owner, shortest first. Groups 1, 2, 6 and the `secret` test need no answer — I
will do them when you have looked at the rest.

1. Group 3's eight window-scoped keys: all yes?
2. Group 3's pane-scoped keys (model, effort, input mode, plan toggle) — yes in principle, so that
   group 2's pane argument is worth building?
3. `tab.new` and `window.new` (group 5): as safe as the splits, or deliberately not?
4. `agent.recap` / `agent.continue`: may an agent spend tokens on the person's behalf?
5. Group 7: should the open/reveal/focus actions pass the writes toggle, as `app_open` does?
