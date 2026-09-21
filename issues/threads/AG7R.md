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
