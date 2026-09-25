# Keyboard shortcut presets: Warp, VS Code, Konsole

Researched 2026-09-17 against official docs and default-keymap source; tables revised for the
QWEASDZXC map on 2026-09-22 and the Projects/Actions revision on 2026-09-23 (§0). "none" = the program has no
default for that action. "adapt" = not the program's own default; explained in the row.
Key strings are Qt portable text. The JSON is written for Relay's exact `(key, modifiers)` matcher.

**Shifted symbols.** On a US layout, Qt reports Ctrl+Shift+9 as key `(` with Shift held. So
Konsole's `Ctrl+(` must be stored as `Ctrl+Shift+(`, VS Code's Ctrl+Shift+5 as `Ctrl+Shift+%`,
Ctrl+Shift+` as `Ctrl+Shift+~`, and Ctrl+Shift+\ as `Ctrl+Shift+|`. The tables show the physical
keys. The JSON uses the forms Qt reports. Other layouts differ (see §5).

**Keys Relay already uses** (presets avoid them): Enter, Ctrl+Enter, Ctrl+Shift+Enter (send to
auto/agent/terminal), Esc (native input), and Ctrl+Shift+C/V plus Ctrl+C/V (copy/paste/interrupt).

## 0. The Relay preset and the rules every preset keeps (#QWAS, 2026-09-22)

**Modifier rule (owner).** Ctrl+Shift+<letter> is Relay's layer. Plain Ctrl+<letter> is bound to
the *same* command only where the key has no editing or terminal meaning: W, E, N, T, I, F, H, and
Q in the prompt box. For A S Z X C D G P plain Ctrl is left to the editor or the program.

**Pairing rule.** No action may hold Ctrl+<L> while a *different* action holds Ctrl+Shift+<L>, in
any preset. Together with "no chord bound twice" it is checked for all four presets by
`tests/test_keybindings.py` (`test_ctrl_and_ctrl_shift_of_a_letter_are_one_action`,
`test_no_preset_has_a_conflict`).

The Relay preset after the move (action → keys; everything not listed is unchanged):

| action id | keys | was |
|---|---|---|
| board.open | Ctrl+Shift+A | Ctrl+Shift+S |
| review.open | Ctrl+Shift+R | — (Review queue; the key was freed from pane.restartShell) |
| sessions.open (new) | Ctrl+Shift+S | — : "Sessions & Projects: sessions, projects, recently closed and globals" |
| files.explorer | Ctrl+Shift+D | Ctrl+B, Ctrl+Shift+B (Ctrl+D is never bound: end-of-input) |
| palette.open | — | Ctrl+Shift+A, then Ctrl+Shift+P; Ctrl+? opens Actions/help |
| help.shortcuts | Ctrl+?, Ctrl+Shift+/, Ctrl+/ | also F1, removed by #KYPR: F-keys act inside programs, and F1 is help in nano, mc and htop |
| prompt.clear (new) | Ctrl+Shift+Q, Ctrl+Q | — : clears the prompt box, Ctrl+Z brings it back (#CPRQ). Plain Ctrl+Q never acts while a program has the keyboard, in any `program_keys` mode |
| control.human | Ctrl+H, Ctrl+Shift+H | Ctrl+H only; now a toggle: take control, and again to go back to the Relay prompt |
| control.prompt | — | Ctrl+Shift+H |
| pane.restartShell | — | Ctrl+Shift+R (the stopped pane's banner and the palette remain) |
| agent.stopAllSubagents | — | Ctrl+Shift+X (the subagents UI and the palette remain) |
| projects.open | Ctrl+Shift+P | Restored as a direct key for Projects in the shared pane; Ctrl+P stays with the program |
| agent.resume, conversations.open | — | Ctrl+Shift+Y, —. Still registered for slash commands and keybindings.json |
| globals.open | Ctrl+Shift+G | Restored as the direct Globals shortcut; plain Ctrl+G remains unbound |
| closed.restore, pane.close, pane.splitRight | Ctrl+Shift+Z; Ctrl+W, Ctrl+Shift+W; Ctrl+E, Ctrl+Shift+E | unchanged |

Freed in the Relay preset: Ctrl+Shift+Y, B, X and Ctrl+B. Ctrl+Shift+G opens Globals; Ctrl+G remains available to the editor or terminal program.

**What the move did to the other presets** (each change keeps the two rules above):

| preset | action | now | why |
|---|---|---|---|
| warp | palette.open | — | Actions/help is Ctrl+?; Ctrl+Shift+P opens Projects |
| warp | files.explorer | Ctrl+Shift+B | the Relay default Ctrl+Shift+D is Warp's split-right here |
| warp | input.toggle | Ctrl+I, Ctrl+Shift+I | was Ctrl+I alone, with Ctrl+Shift+I on `input.modeTerminal`: two actions on one letter breaks the pairing rule |
| warp | input.modeTerminal | — | as above; the palette remains |
| vscode | palette.open | — | Actions/help is Ctrl+?; Ctrl+Shift+P opens Projects |
| vscode | files.explorer | Ctrl+Shift+E | VS Code's Show Explorer (V1); free here because split-right is Ctrl+Shift+5 / Ctrl+\ |
| vscode | agent.newChat | — | was Ctrl+N, while Ctrl+Shift+N is window.new: the pairing rule. `/new` and the palette remain |
| konsole | palette.open | — | Actions/help is Ctrl+?; Ctrl+Shift+P opens Projects |

The Board (Ctrl+Shift+A), Sessions (Ctrl+Shift+S), Projects (Ctrl+Shift+P), Globals (Ctrl+Shift+G), Actions/help (Ctrl+?) and prompt.clear inherit the Relay
default in all four presets; no preset binds those chords to anything else.

## 1. Warp (Linux)

Sources:
- W1 https://docs.warp.dev/getting-started/keyboard-shortcuts (Linux tab)
- W2 https://docs.warp.dev/terminal/windows/tabs/
- W3 https://docs.warp.dev/terminal/windows/split-panes/
- W4 https://docs.warp.dev/agents/local-agents/interacting-with-agents/terminal-and-agent-modes/
- W5 https://docs.warp.dev/agents/local-agents/interacting-with-agents/

| action id | keys | source / note |
|---|---|---|
| window.new | Ctrl+Shift+N | observed in the product (not listed in W1) |
| window.next / window.previous | — | none |
| tab.new | Ctrl+Shift+T | W1, W2 |
| tab.next | Ctrl+PgDown, Ctrl+Tab | W1/W2 Ctrl+Page Down; W3: Ctrl+Tab cycles tabs by default |
| tab.previous | Ctrl+PgUp, Ctrl+Shift+Tab | W1/W2 Ctrl+Page Up; W3 |
| pane.splitRight | Ctrl+Shift+D | W1, W3 |
| pane.splitDown | Ctrl+Shift+E | W1, W3 |
| pane.focusLeft/Right/Up/Down | Ctrl+Alt+Left/Right/Up/Down | W1 "Switch Panes", W3 |
| pane.close | Ctrl+Shift+W | W3 closes the pane, W2 closes the tab |
| closed.restore | Ctrl+Alt+T | W1/W2 "Reopen Closed Tab" (Ctrl+Shift+T is taken by new tab) |
| palette.open | — | Actions/help is Ctrl+?; Ctrl+Shift+P opens Projects |
| board.open | Ctrl+Shift+A | Relay default (#QWAS) |
| sessions.open | Ctrl+Shift+S | Relay default (#QWAS) |
| files.explorer | Ctrl+Shift+B | **adapt.** The Relay default Ctrl+Shift+D is Warp's split-right, so the explorer keeps its old B here |
| prompt.clear | Ctrl+Shift+Q, Ctrl+Q | Relay default (#CPRQ) |
| terminal.native | F12 | none in Warp. Kept Relay default: an F-key still works inside programs |
| terminal.interrupt | — | Warp uses contextual Ctrl+C (W4). Unbound: Ctrl+C already reaches the shell |
| agent.resume | Ctrl+Shift+Y | W5 Conversations menu. Only this preset binds it: the Relay default has had no key since #QWAS (sessions.open, Ctrl+Shift+S, opens the same pane). A toggle: pressed again with the Sessions pane focused it closes it, and from anywhere else it brings it forward |
| agent.newChat | — | Warp's own new-conversation keys are taken: Ctrl+Shift+Enter is Relay's send-to-terminal key, Ctrl+Shift+N is window.new |
| agent.stop | — | Warp: Ctrl+C while the agent responds (W4). Left unbound (same reason as interrupt) |
| input.modeAuto | — | none (auto-detection is a setting, W4) |
| input.modeAgent | — | Superseded 2026-09-17: Relay added `input.toggle`, which now takes Ctrl+I |
| input.modeProgram | — | Relay-only (#S976): types into a line editor at its prompt. Reached from the mode chip, "Type into it from here" and Ctrl+I's cycle while a program waits; no default key |
| input.toggle | Ctrl+I, Ctrl+Shift+I | W4: Ctrl+I toggles shell ↔ agent (acts from the prompt box only). Ctrl+Shift+I is the Relay default's twin |
| input.modeTerminal | — | Was Ctrl+Shift+I (an adaptation: in Warp that key toggles auto-approve) until #QWAS: with Ctrl+I on input.toggle it broke the pairing rule. Esc (Warp's "back to terminal") is taken by Relay |
| keybindings.edit | Ctrl+, | **adapt.** W1's Linux column says `Ctrl+⌘+K`, which is a macOS key; the keybindings page has a macOS-only binding. Ctrl+, opens Warp Settings, where the shortcuts live |
| keybindings.reload | — | none |

## 2. VS Code (Linux)

Sources: V1 https://code.visualstudio.com/shortcuts/keyboard-shortcuts-linux.pdf. The files below are
the registrations that "Preferences: Open Default Keyboard Shortcuts (JSON)" is built from, under
`https://github.com/microsoft/vscode/blob/main/src/vs/workbench/`:
- V2 `contrib/terminal/browser/terminalActions.ts`
- V3 `contrib/terminal/browser/terminal.contribution.ts`
- V4 `browser/parts/editor/editorActions.ts`
- V5 `browser/parts/editor/editorCommands.ts`
- V6 `browser/actions/windowActions.ts`
- V7 `electron-browser/actions/windowActions.ts`
- V8 `contrib/chat/browser/actions/chatActions.ts`
- V9 `contrib/chat/browser/actions/chatNewActions.ts`
- V10 `contrib/chat/browser/actions/chatExecuteActions.ts`
- V11 `contrib/preferences/browser/preferences.contribution.ts`

| action id | keys | source / note |
|---|---|---|
| window.new | Ctrl+Shift+N | V1, V6 `workbench.action.newWindow` |
| window.next / window.previous | — | none: `workbench.action.switchWindow` is unbound on Linux (macOS only: Ctrl+W), V7 |
| tab.new | Ctrl+Shift+` | V1, V2 `terminal.new` (terminals act as tabs) |
| tab.next | Ctrl+PgDown, Ctrl+Tab | V2 `terminal.focusNext`, V4 `nextEditor`; V1 lists Ctrl+Tab as "Open next" (V4: most-recently-used editor quick-open) |
| tab.previous | Ctrl+PgUp, Ctrl+Shift+Tab | V2 `terminal.focusPrevious`, V4 `previousEditor`; V1 |
| pane.splitRight | Ctrl+Shift+5, Ctrl+\ | V2 `terminal.split` (terminal focus); V1/V4 `splitEditor` |
| pane.splitDown | Ctrl+Shift+\ | **adapt.** The chord `Ctrl+K Ctrl+\` is `splitEditorOrthogonal` (V4). Terminal split has no second direction. Shift+\ = "the \ split, other direction" (VS Code itself uses Ctrl+Shift+\ for jump-to-bracket and focus terminal tabs) |
| pane.focusLeft / focusUp | Alt+Left / Alt+Up | V2 `terminal.focusPreviousPane` (primary Alt+Left, secondary Alt+Up) |
| pane.focusRight / focusDown | Alt+Right / Alt+Down | V2 `terminal.focusNextPane`. Chords for editor groups: `Ctrl+K Ctrl+←/→/↑/↓` (V1, V4); VS Code's single-key versions are these Alt+arrow terminal keys |
| pane.close | Ctrl+W | V1, V5 `closeActiveEditor`, V2 `terminal.killEditor`. The panel terminal's kill has no default |
| closed.restore | Ctrl+Shift+T | V1, V4 `reopenClosedEditor` |
| palette.open | — | Actions/help is Ctrl+?; Ctrl+Shift+P opens Projects. F1 stays available to programs |
| board.open | Ctrl+Shift+A | Relay default (#QWAS) |
| sessions.open | Ctrl+Shift+S | Relay default (#QWAS) |
| files.explorer | Ctrl+Shift+E | V1 Show Explorer. Free here: split-right is Ctrl+Shift+5 / Ctrl+\, and Ctrl+E is unbound, so the pairing rule holds |
| prompt.clear | Ctrl+Shift+Q, Ctrl+Q | Relay default (#CPRQ) |
| terminal.native | Ctrl+`, F12 | **adapt:** Ctrl+` is "toggle/focus terminal" (V1, V3). F12 is kept from Relay (in VS Code it is Go to Definition, which has nothing to do with the terminal) |
| terminal.interrupt | — | none |
| agent.newChat | — | V9 `chat.newChat` is Ctrl+N (in chat; secondary Ctrl+L left out because it clears the shell screen). Unbound since #QWAS: Ctrl+Shift+N is window.new, and one letter may not carry two actions |
| agent.stop | Ctrl+Esc | V10 `chat.cancel` while a request is running (Windows uses Alt+Backspace) |
| input.modeAuto | — | none (Ctrl+. opens the chat mode picker, V10, but there is no auto mode) |
| input.modeTerminal | — | none |
| input.modeAgent | Ctrl+Shift+Alt+I | V8: open chat in Agent mode. Linux override (other platforms: Ctrl+Shift+I) |
| input.modeProgram | — | Relay-only (#S976): types into a line editor at its prompt. Reached from the mode chip, "Type into it from here" and Ctrl+I's cycle while a program waits; no default key |
| keybindings.edit | Ctrl+, | **adapt.** The chord `Ctrl+K Ctrl+S` (V1, V11) can't be used. Ctrl+, is VS Code's Settings (V11), which users know as the way into keybindings |
| keybindings.reload | — | none (Reload Window's Ctrl+R applies only in development, V6) |

## 3. Konsole (KDE, verified identical in release/23.08, release/24.08 and master)

Sources:
- K1 https://docs.kde.org/stable_kf6/en/konsole/konsole/commandreference.html
- K2 https://invent.kde.org/utilities/konsole/-/blob/release/24.08/src/ViewManager.cpp
- K3 `…/src/MainWindow.cpp` (MainWindow is a KXmlGuiWindow)
- K4 `…/src/session/SessionController.cpp`
- K5 `…/src/Shortcut_p.h` (`ACCEL = Ctrl+Shift` on Linux)
- K6 https://invent.kde.org/frameworks/kxmlgui/-/blob/master/src/kxmlguiwindow.cpp
- K7 https://invent.kde.org/frameworks/kconfig/-/blob/master/src/gui/kstandardshortcut.cpp

| action id | keys | source / note |
|---|---|---|
| window.new | Ctrl+Shift+N | K1, K3 `new-window` |
| window.next / window.previous | — | none |
| tab.new | Ctrl+Shift+T | K1, K3 `new-tab` |
| tab.next | Ctrl+PgDown | K2 `next-tab` = Shift+Right, Ctrl+PgDown. **Shift+Right dropped:** it would break text selection in Relay's input box, and without Ctrl it can't work inside programs. Konsole's Ctrl+Tab is last-used tab, not next |
| tab.previous | Ctrl+PgUp | K2 `previous-tab` = Shift+Left, Ctrl+PgUp (Shift+Left dropped, same reason) |
| pane.splitRight | Ctrl+( | K1, K2 `split-view-left-right`. JSON: `Ctrl+Shift+(` (US) plus `Ctrl+(` (layouts with an unshifted paren, e.g. AZERTY) |
| pane.splitDown | Ctrl+) | K1, K2 `split-view-top-bottom`. JSON: `Ctrl+Shift+)`, `Ctrl+)` |
| pane.focusLeft/Right/Up/Down | Ctrl+Shift+Left/Right/Up/Down | K2 `focus-view-left/right/above/below` |
| pane.close | Ctrl+Shift+W | K1, K4 `close-session` (closes the focused session/view) |
| closed.restore | — | none: Konsole has no undo-close (no such action in K2–K4) |
| palette.open | — | Actions/help is Ctrl+?; Ctrl+Shift+P opens Projects |
| board.open | Ctrl+Shift+A | Relay default (#QWAS) |
| sessions.open | Ctrl+Shift+S | Relay default (#QWAS) |
| files.explorer | Ctrl+Shift+D | Relay default (#QWAS); Konsole binds nothing to it |
| prompt.clear | Ctrl+Shift+Q, Ctrl+Q | Relay default (#CPRQ). Konsole's own Ctrl+Shift+Q (quit) does not apply inside Relay |
| terminal.native | F12 | none (Konsole is always native). Kept Relay default |
| terminal.interrupt, agent.*, input.* | — | none |
| keybindings.edit | Ctrl+Alt+, | K1 Settings → Configure Keyboard Shortcuts; K7 `KeyBindings = CTRLALT(Comma)` |
| keybindings.reload | — | none |

## 4. Machine-readable presets

The JSON Relay ships is `Keymap::presetJson()` in `src/Keymap.h`; this copy mirrors it.

```json
{
  "warp": {
    "window.new": ["Ctrl+Shift+N"], "window.next": [], "window.previous": [],
    "tab.new": ["Ctrl+Shift+T"], "tab.next": ["Ctrl+PgDown", "Ctrl+Tab"], "tab.previous": ["Ctrl+PgUp", "Ctrl+Shift+Tab"],
    "pane.splitRight": ["Ctrl+Shift+D"], "pane.splitDown": [], "pane.splitLeft": [], "pane.splitUp": [],
    "pane.focusLeft": ["Ctrl+Alt+Left"], "pane.focusRight": ["Ctrl+Alt+Right"], "pane.focusUp": ["Ctrl+Alt+Up"], "pane.focusDown": ["Ctrl+Alt+Down"],
    "pane.moveLeft": [], "pane.moveRight": [], "pane.moveUp": [], "pane.moveDown": [],
    "pane.close": ["Ctrl+Shift+W"], "closed.restore": ["Ctrl+Alt+T"], "palette.open": [],
    "files.explorer": ["Ctrl+Shift+B"], "agent.resume": ["Ctrl+Shift+Y"], "app.settings": ["Ctrl+Shift+O"],
    "terminal.native": ["F12"], "terminal.interrupt": [],
    "agent.newChat": [], "agent.stop": [], "agent.requests": [],
    "input.modeAuto": [], "input.modeTerminal": [], "input.modeAgent": [], "input.toggle": ["Ctrl+I", "Ctrl+Shift+I"],
    "keybindings.edit": ["Ctrl+,"], "keybindings.reload": []
  },
  "vscode": {
    "window.new": ["Ctrl+Shift+N"], "window.next": [], "window.previous": [],
    "tab.new": ["Ctrl+Shift+~"], "tab.next": ["Ctrl+PgDown", "Ctrl+Tab"], "tab.previous": ["Ctrl+PgUp", "Ctrl+Shift+Tab"],
    "pane.splitRight": ["Ctrl+Shift+%", "Ctrl+\\"], "pane.splitDown": [], "pane.splitLeft": [], "pane.splitUp": [],
    "pane.focusLeft": ["Alt+Left"], "pane.focusRight": ["Alt+Right"], "pane.focusUp": ["Alt+Up"], "pane.focusDown": ["Alt+Down"],
    "pane.close": ["Ctrl+W"], "closed.restore": ["Ctrl+Shift+T"], "palette.open": [],
    "files.explorer": ["Ctrl+Shift+E"], "app.settings": ["Ctrl+Shift+O"],
    "terminal.native": ["Ctrl+`", "F12"], "terminal.interrupt": [],
    "agent.newChat": [], "agent.stop": ["Ctrl+Esc"], "agent.requests": [],
    "input.modeAuto": [], "input.modeTerminal": [], "input.modeAgent": ["Ctrl+Shift+Alt+I"],
    "keybindings.edit": ["Ctrl+,"], "keybindings.reload": []
  },
  "konsole": {
    "window.new": ["Ctrl+Shift+N"], "window.next": [], "window.previous": [],
    "tab.new": ["Ctrl+Shift+T"], "tab.next": ["Ctrl+PgDown"], "tab.previous": ["Ctrl+PgUp"],
    "pane.splitRight": ["Ctrl+Shift+(", "Ctrl+("], "pane.splitDown": [], "pane.splitLeft": [], "pane.splitUp": [],
    "pane.focusLeft": ["Ctrl+Shift+Left"], "pane.focusRight": ["Ctrl+Shift+Right"], "pane.focusUp": ["Ctrl+Shift+Up"], "pane.focusDown": ["Ctrl+Shift+Down"],
    "pane.close": ["Ctrl+Shift+W"], "closed.restore": [], "palette.open": [],
    "app.settings": ["Ctrl+Shift+O"],
    "terminal.native": ["F12"], "terminal.interrupt": [],
    "agent.newChat": [], "agent.stop": [], "agent.requests": [],
    "input.modeAuto": [], "input.modeTerminal": [], "input.modeAgent": [],
    "keybindings.edit": ["Ctrl+Alt+,"], "keybindings.reload": []
  }
}
```

**Since issue #78BN:** Ctrl+E is **New shell**. `pane.splitRight` makes a shell pane on the
right, and Left, Up or Down within two seconds re-docks it to that side, so **`pane.splitDown` has no
default key in any preset** and `pane.splitLeft` / `pane.splitUp` have none either. All four keep an
action, so they can still be bound here or run from the palette. The rows above record what each
program binds; the JSON is what Relay ships. Ctrl may still be held from the split key when the
arrow comes (Shift too: Ctrl+Shift+E leaves both down), as long as that chord is not a shortcut in
the preset — konsole's Ctrl+Shift+Down keeps focusing the pane below (card #JXWT).

**Since card #Q7Y9:** moving and placing share the same trick. A `pane.moveLeft` or
`pane.moveRight` followed by `pane.moveDown` **within two seconds** docks the pane beneath the
neighbor it moved toward (default keys: Ctrl+Alt+Left then Ctrl+Alt+Down, and Ctrl+Alt+Right then
Ctrl+Alt+Down). No new action and no new key: the second key is the ordinary Move-down, so the
chord follows whatever the preset binds to the three move actions, and Move-down on its own keeps
moving the pane down. That also means a preset can leave the chord out of reach: **the Warp preset
binds none of `pane.moveLeft` / `pane.moveRight` / `pane.moveDown`** (Warp gives Ctrl+Alt+arrow to
`pane.focus*` instead), so under it there is no chord until those actions are bound by hand — the
drag onto a pane's bottom edge, and Actions, are the ways to dock a pane beneath another there.

## 5. Collisions, pass-through and desktop grabs

**Collisions resolved (no preset has a duplicate key):**
- Options: `app.settings` is Ctrl+Shift+O in every preset (2026-09-18). The Relay default also keeps Ctrl+,; the Warp and VS Code presets give Ctrl+, to `keybindings.edit`, so there it is Ctrl+Shift+O alone.
- Warp new conversation: its Ctrl+Shift+N is also Warp's new window (the docs list both on Linux), and its Ctrl+Shift+Enter is Relay's send-to-terminal key; new chat is unbound in the Warp preset (`/new`, or Actions). Warp keeps Ctrl+Shift+Y for `agent.resume`, its conversations key; the Relay default binds nothing there since #QWAS.
- Warp Esc (back to terminal mode) is Relay's native-input key. Ctrl+Shift+I was used for it until #QWAS; it is now input.toggle's twin (pairing rule), and terminal mode has no key in this preset.
- Warp split-right (Ctrl+Shift+D) against the explorer's new default: the explorer is Ctrl+Shift+B in the Warp preset.
- VS Code new chat (Ctrl+N) against window.new (Ctrl+Shift+N): the pairing rule leaves new chat unbound.
- Warp and VS Code Ctrl+C (interrupt, stop agent) and VS Code Ctrl+L (new chat): left unbound so they still reach the shell.
- VS Code F1: dropped.
- Konsole Shift+Left/Right: dropped (text selection). Konsole's closed.restore is unbound because Relay's Ctrl+Shift+W would collide with close-session.
- Relay's input box uses Ctrl+Shift+Left/Right to select by word. Konsole's pane focus keys override that, as they do in Konsole itself.
- **Layout dependence:** shifted-symbol keys (`Ctrl+Shift+%`, `~`, `|`, `(`, `)`) are written for US layouts. A sturdier fix is to have `Keymap::match` also try the unshifted key via `QKeyEvent::nativeVirtualKey`/keysym.

**Keys that do not act inside vim or nano** (the default `program_keys: "shift-only"` lets only Ctrl+Shift combos and F-keys act). While a program runs, these reach the program:
- Warp: Ctrl+PgUp/PgDown, Ctrl+Tab, Ctrl+Alt+arrows, Ctrl+Alt+T, Ctrl+I (vim: jump forward), Ctrl+,
- VS Code: Ctrl+PgUp/PgDown, Ctrl+Tab, Ctrl+\, Alt+arrows, Ctrl+W (vim: window commands), Ctrl+` (sends NUL), Ctrl+Esc, Ctrl+,
- Konsole: Ctrl+PgUp/PgDown, the unshifted `Ctrl+(`/`Ctrl+)` forms, Ctrl+Alt+I, Ctrl+Alt+,

Everything else, including the F12 bindings, still acts inside programs.

**Tasks panel (`agent.requests`, added 2026-09-17):** Ctrl+Shift+K in the Relay preset only. Relay's
window filter takes it before KonsolePart, whose own Ctrl+Shift+K is "Clear Scrollback and Reset"
(still in the terminal context menu). Unbound in Warp (Ctrl+Shift+K clears blocks), VS Code
(`editor.action.deleteLines`) and Konsole (Clear Scrollback and Reset), so users of those presets keep
their habit; `/tasks` and the chip still work.

**Step through links (`links.step`, added 2026-09-17):** Ctrl+Shift+L, the same in all four
presets, because it is free everywhere: Warp, VS Code (Linux) and Konsole 23.08/24.08 bind
nothing to it (VS Code's Ctrl+Shift+L is "select all occurrences" in the editor, not in the
terminal; Konsole's L bindings are Ctrl+Shift+Alt+L "Show Menu Bar" and nothing on
Ctrl+Shift+L), and Relay had no Ctrl+Shift+L either. It is left out of the preset tables, so
every preset inherits the Relay default. It is a Ctrl+Shift combination, so it also acts while
a program owns the terminal under the default `program_keys: "shift-only"`.

**Desktop-environment grabs:**
- **Alt+Tab / Alt+Shift+Tab** (Relay default window.next/previous): taken by GNOME, KDE Plasma, Cinnamon and XFCE, so no preset uses them.
- **Ctrl+Alt+T** (Warp closed.restore): launches a terminal on Ubuntu/GNOME and KDE Plasma, so it will usually never reach Relay. Warp on Linux has the same problem. Rebind if needed.
- **Ctrl+Alt+Left/Right** (Warp pane focus): GNOME's default workspace switch (with Super+PgUp/PgDn). Ctrl+Alt+Up/Down is also taken on older GNOME, Cinnamon and XFCE.
- **Ctrl+Esc** (VS Code agent.stop): opens System Monitor/Activity in KDE Plasma. XFCE and some other desktops use it for the app menu.
- **F12**: grabbed globally by drop-down terminals (Yakuake, Guake, Tilda) when they are running.
- **Super combos:** none of the presets use Super/Meta. GNOME and Plasma reserve most of them (overview, tiling, workspaces), so keep presets free of Meta.
- **Ctrl+Alt+Del, Ctrl+Alt+Backspace, Ctrl+Alt+F1–F12** (session, VT): not used.

Card #P7SJ put Projects, Sessions and Globals in one shared pane. The 2026-09-23 revision gives
Sessions Ctrl+Shift+S and Projects Ctrl+Shift+P in every preset. `agent.resume` has no Relay
default key (Warp keeps Ctrl+Shift+Y); Globals keeps Ctrl+Shift+G. Ctrl+? opens Actions/help.
Pane screenshot capture is unbound by default. Explicit user overrides still take precedence.
