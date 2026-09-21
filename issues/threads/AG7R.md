<!-- relay:entry 20260920T203500Z-ag author=agent kind=progress pane=terminal -->
Card opened from the owner's ask in the terminal, straight after `ab9e2ab7` landed the splits.

The review is read off the code on `main` at `ab9e2ab7`, not off §30.2's prose: every
`actionItem()` / `submenu()` / field-built `PaletteItem` in `src/RelayWindow.h` (93 keys), the
`Keymap` registry (93 registered actions), `appcommands::actionIsAgentSafe()` (33 keys), every
`SettingRow::Button`/`Buttons` in the options catalog (8 rows, 5 with a safe button), and the tool
specs under `backend/relay_core/`. The extraction script is throwaway; every number in the card is
reproducible with grep against those files.

The finding that surprised me: **12 of the 33 keys in the safe table cannot be run at all** —
they are registry actions with no palette row, so `AppCatalog.action()` answers `unknown_action`,
and one of them (`palette.agent`) is a retired key that `src/Keymap.h:176` migrates away. §30.2
promises the agent it may move the focus between panes; every one of those four keys fails. That
is the #FEJQ thread's closing note, which said "a few keys".

Second: `RelayWindow::runAction()` reads `Pane *pane = m_active` and `app_command` carries no pane,
so no pane-scoped action can be made safe until the command names its pane. That blocks the model,
effort, input-mode and plan-toggle answers whatever the owner says to them.

Card is in discussing with five questions; groups 1, 2, 6 and the secret-row test need no answer.

<!-- relay:entry 20260920T210500Z-ow author=agent kind=decision pane=terminal -->
Owner answered all eight, 2026-09-20 (quoted in full in the card's `## Decisions`): groups 1–3
yes, group 7 "pass the toggle like app_open", group 8 yes and wider ("allow sending messages and
pre-filling messages across panes -- and address the other limitations here"). Group 6 was not a
question and the card said so badly — nothing for him to do. Group 4 "i want those too (i think,
tell me if there is a risk)" and group 5 "i would want to lean on the side of allowing, argue to me
for each why not" both came back to me; the answers are in `## Planning notes`.

Three things I checked before arguing, because two of them changed my answer:

1. **Group 4's risk is mechanical, not editorial.** `executeAppCommand` returns
   `appCommands().execute()` synchronously and `execute()` calls `item.run()` inline, while every
   group-4 action ends in `dialog.exec()` — a nested event loop. So the `app_command_result` cannot
   be sent until the person dismisses a dialog they did not ask for, §30.3's 20-second deadline
   expires, and the agent is told `no_reply`: the #H6VQ failure by another route, with the window
   frozen behind the modal meanwhile. Answer to the owner: yes, but make those `run()`s
   non-blocking first (`open()` + callback, not `exec()`), and where the agent wants the outcome
   rather than the picker, give it an `app_open`-shaped path instead.
2. **Agents can already change hotkeys.** `set_keybinding` has existed since #GMCF and both the
   tab's helper and the pane agent get it whenever the GUI sent the keybinding catalog
   (`Pane::sendKeybindings()`). The owner's group-5 ask is already satisfied; only
   `keybindings.edit` (external editor) and `keybindings.clearOverrides` (bulk wipe) are off.
3. **`pane.close` is not as reversible as the closed list suggests.** `WindowManager::reopen()`
   rebuilds the pane from its saved layout — directory, text and conversation come back, the
   running process does not. Closing a pane mid-build loses the build.

Of group 5's ~34 keys I could only make an argument I believe against seven: `voice.toggle`
(switches a microphone on), `pane.share`/`pane.sharing` (outward-facing, and revoking does not
un-see), `app.update` (restarts the app, killing every shell in every window), the four
`control.*`/`program.delegate` keys (an agent granting itself control is circular),
`windows.fresh`, `keybindings.clearOverrides` and `history.clear`. Everything else I recommend
turning on, with eight more in a bounded middle where the owner should choose.

Work started: subagent `safetable` holds src/AppCommands.{h,cpp}, tests/appcommands_test.cpp, the
§30.2 paragraph and two spots in src/RelayWindow.h for groups 1, 3 (window-scoped), 6 and 7.
Group 2 follows it (same file), then group 8, then group 4's non-blocking pass.

<!-- relay:entry 20260920T211500Z-ld author=agent kind=progress pane=terminal -->
**Groups 1, 3 (window-scoped), 6 and 7 landed** — `1a2459ed` (src/AppCommands.{h,cpp},
tests/appcommands_test.cpp) and `c375a86e` (src/RelayWindow.h, §30.2).

- **Group 1.** `AppCommands` takes `registryLabel` and `runRegistryAction`; `findAction()` falls
  back to them when the catalog misses, so the twelve registry-only keys run. `catalog()` lists
  them with `agent_safe: true` under section `Shortcuts` — the same section
  `app_tools._shortcut_rows` uses, and it already skips any key the catalog holds, so the
  contradiction closes with no backend change. A registered key outside the table now answers
  `not_agent_safe`; one neither knows still answers `unknown_action`. `palette.agent` deleted.
- **Group 3.** `tests.open`, `app.about`, `logs.open`, `theme.folder`, `agent.screenshotPane`,
  `pane.equalize`, `menu:closed`, `closed:<id>` by prefix; the `declined:<path>` row's **Undo**
  button marked safe. No pane-scoped key — those wait on group 2.
- **Group 7.** The table is two explicit sets: `readActions()` (opens, reveals, focuses, restores)
  and `reversibleWriteActions()`; `actionIsAgentSafe()` is the union and the new `actionIsRead()`
  is the first. Only non-read keys are gated by `writes_enabled`. `agent.screenshotPane` and
  `pane.equalize` were deliberately put on the *writing* side — a screenshot changes what the
  person is about to send, equalize moves every splitter.
- **Group 6.** The real catalog needs a window, so the rule went into the library rather than a
  fixture-only test: `appcommands::rowNamedLikeASecret()`, applied in `catalog()` — the one place
  every row passes through on its way to an agent. An unmarked `Text` row named like a credential
  is listed `secret: true`, `settable: false`, no value, `set_option` refuses it, and a
  one-per-row `qWarning` says to mark it. One documented exception:
  `option:security/secret_patterns`, the guard's own configuration.

Checked independently by the orchestrating session before accepting: `ctest -R "appcommands|settings"`
5/5, `relay-appcommands-tests` 35 passed / 0 failed, and the group-6 regex applied by hand to every
`Text` row the shipped catalog builds (11 of them, via `textRow`/`hostListRow`/`listRow` plus
`local:address`) — `security/secret_patterns` is the only match and it is the exempted one, so the
guard withholds nothing real today.

Still open and NOT done: `local:address/save` and `found:<n>` **Save** in
src/LocalModelsSettings.cpp. The implementing session declined to widen them on its own because the
owner answered group 6 with "not sure what i am supposed to do there" rather than a yes, and I
agree — it is a one-line question for him, not a gap to close silently.

<!-- relay:entry 20260920T213500Z-g2 author=agent kind=progress pane=terminal -->
**Group 2 landed, and group 3 is now complete on both halves** — `2e51e3b6`, with `cb33a900`
adding the tool to docs/ARCHITECTURE.md's list.

A pane is named by **its session token** — the string `who` already carried — so the three rules
cost the model nothing in the common case: a pane agent with no `pane` means its own pane (the
command came out of that pane's worker, so `who` *is* the token); the helper with no `pane` lands
on the focused pane exactly as before, which §30.3 now says outright instead of implying; an
explicit `pane` names any pane of the window, and one that has gone answers the new §30.3 word
`unknown_pane`, refused before the policy is consulted so a dead pane never reads as a policy
problem.

Nothing exposed a pane id an agent could read — the catalog carries the *tab*, `session_info` is
about this conversation, `app_sessions_search` about saved ones — so `app_panes` /
`app_command {command: "list_panes"}` was added: `{id, title, cwd, tab, model, mode, busy, focused,
you}`. Deliberately a round trip and not a field of the `app` block, because panes open and close
between two catalogs and a stale list would have an agent name a pane that has gone.

`runActionNow()` takes an optional target defaulting to `m_active`, so every existing caller —
keyboard, palette, chrome, right-click — is unchanged, and only `actionIsPaneScoped()` keys follow
the aim. The splits, the explorer, Equalize, the moves and the focus keys go on anchoring on
`m_activeLeaf`, with the reason written down: a pane appearing in a tab nobody is watching, or a
focus that jumps out of the tab someone is typing in, is a worse surprise than the one being fixed.

Group 3's pane-scoped keys then went on: `menu:model`/`model:<id>`, `menu:effort`/`effort:<level>`,
`menu:mode`/`input.mode*`/`input.toggle`, `agent.planToggle` — in `reversibleWriteActions()`, so
they stay behind the writes toggle.

Checked by the orchestrating session before accepting: `ctest -R appcommands` green (45 cases, was
35), `tests.test_app_tools` + `tests.test_tool_groups` 78 OK, and — because the implementing
session reported that land.py had listed four of another session's hunks in `src/RelayWindow.h` as
its own — the landed diff was read for foreign code (none: no `pairPhone`, no `runPlugItem`, no
`#FR1C`) and that session's work confirmed still present both on `main` and in the working tree.
The one edit outside the named area, a two-line tuple in `backend/relay_core/tool_groups.py`, was
necessary (without it `app_panes`'s schema ships in every request instead of being deferred) and is
covered by `tests.test_tool_groups`.

Group 8 is now running in two parallel sessions: `panetalk` (cross-pane send and pre-fill, the rest
of the `app_open` targets, naming panes and tabs) and `helperread` (the helper reading the pane it
is helping). Group 4 is NOT started: the owner asked to be told the risk before it goes ahead and
has not yet answered.
