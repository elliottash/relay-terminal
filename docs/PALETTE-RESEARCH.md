# Agent and terminal palettes: research and recommendations

Research date: 2026-09-17. Scope: two keyboard-driven right-sidebar palettes for Relay,
**Ctrl+Shift+A "Agent options"** and **Ctrl+Shift+T "Terminal options"**. Each is a filterable
list you move through with the arrow keys. Esc closes the sidebar and returns focus to the terminal.

## 1. How other programs do it

| Program | Invocation | Scope / structure | Matching and ranking | Shortcuts shown | Nesting and state | Docking |
|---|---|---|---|---|---|---|
| Warp | Ctrl+Shift+P palette; Ctrl+R command search; Ctrl+Shift+R workflows | One global palette with filter prefixes (`actions:`, `workflows:`/`w:`, `prompts:`/`p:`, `notebook:`, `files:`, `drive:`, `sessions:`, `launch_configs:`, `env_vars:`) and matching filter chips | Fuzzy | Yes | Agent controls are not in a palette: they sit in the input toolbar (model, voice, image, context-usage meter, autodetect toggle) and in slash commands (`/new`, `/fork`, `/compact`, `/plan`) | Centered overlay |
| VS Code | Ctrl+Shift+P (`>` in Quick Open) | Prefixes: `>` commands, `@` symbols, `:` line, `#` workspace symbols, `?` help | Fuzzy, with a "recently used" section above "other commands" (`workbench.commandPalette.history`) | Yes, plus a gear to rebind | Multi-step Quick Picks ("1/3"), separators, per-item buttons | Top-center overlay |
| Zed | Ctrl+Shift+P; agent panel | Palette of actions; the agent panel has its own model selector (Ctrl+Alt+/), favorite-model cycling, profiles (tool sets), and a thread sidebar | Fuzzy | Yes; the keymap editor records keys inline | Model and profile pickers are separate popovers | Overlay; the agent panel is docked |
| JetBrains Find Action | **Ctrl+Shift+A** | Every action, including ones with no menu entry or no shortcut | Fuzzy, recent first, abbreviations | Yes; **Alt+Enter on a result assigns a shortcut** | Boolean settings show inline on/off switches; disabled actions are listed | Overlay |
| WezTerm | Ctrl+Shift+P (ActivateCommandPalette) | Built-in key assignments plus Lua `augment-command-palette` entries | Fuzzy, ranked by **frecency** | Yes (keycaps) | Flat | Modal overlay |
| kitty | Ctrl+Shift+F3 (command-palette kitten) | Every mappable action, **mapped and unmapped**, grouped by category | Case-insensitive, prefix, typo-tolerant for words of 4+ letters, searches the key, name and category columns | Yes, as a column | Category headers | Overlay window |
| Windows Terminal | Ctrl+Shift+P | Action mode (`>`); delete `>` for commandline mode (`wt` args) | Fuzzy, recent commands | Yes | **Nested commands** ("Change font size..."), **iterable commands** (one item per profile or color scheme) | Overlay |
| Ghostty (1.2+) | Ctrl+Shift+P (GTK) | Almost every keybind action, generated automatically | Fuzzy | Yes | Flat | Overlay |
| Claude Code | `/` menu; Alt+P model; Shift+Tab mode cycling; Alt+T thinking | Slash menu plus a direct key per high-value toggle | Prefix/fuzzy on `/` | In docs and help | Pickers are separate dialogs; `/model` applies mid-turn | Inline in the TUI |
| Cursor | Ctrl+. mode menu; Ctrl+/ cycles models; Shift+Tab rotates modes | Agent / Ask / Plan modes; model list | Fuzzy | Yes | Mode and model are separate menus | Popover in the agent side pane |
| opencode | Ctrl+P command list; leader Ctrl+X; `<leader>m` models; Tab cycles agents | Commands, models, sessions (new, list, compact, undo, redo) | Fuzzy | Yes | Model list with favorites | TUI dialog |
| Raycast | Cmd+K action panel | Actions for the selected item, grouped in sections (for example a separate "Danger zone") | Filtered | Yes; Enter runs the primary action, Cmd+Enter the secondary | **Submenus replace the list; Esc pops back one level** | Panel |

Takeaways:
1. Nobody else has a separate "agent palette" and "terminal palette". Warp, Zed, Cursor and Claude Code
   keep agent state (model, mode, autonomy) as **visible, always-on controls** with a direct key
   (Warp's toolbar, Cursor's Ctrl+. and Ctrl+/, Claude Code's Alt+P and Shift+Tab), and put everything
   else in one global palette. Relay's split works if the two palettes share one engine and each
   one can search the other's items (see open decision 1).
2. Showing each shortcut on its row is universal. Letting you rebind from the row is the standout feature (JetBrains
   Alt+Enter, VS Code's gear, Zed's inline recorder).
3. Ranking is fuzzy match plus recency or frecency (VS Code, WezTerm). Kitty's typo tolerance and its
   search over shortcut and category text are cheap and useful.
4. Pickers with a value (model, mode) use **nested lists with a checkmark on the current value**
   (Windows Terminal nested and iterable commands, Raycast submenus, Warp and Zed model dropdowns).
5. Everyone else uses a transient overlay, and none of them sends keystrokes to a running program while it
   is open. A right sidebar is unusual but reasonable for Relay, because it does not cover the
   terminal output the agent prints inline.

## 2. Recommended AGENT palette (Ctrl+Shift+A)

All items act on the **focused pane**. The header shows the pane's current state, for example
`Pane 2 · GLM-5.3 · Auto · running`. Items are ranked by value.

| # | Label | What it does | Kind | Suggested shortcut |
|---|---|---|---|---|
| 1 | Model: *GLM-5.3* ▸ | Opens the list of models (Kimi K3, GLM-5.3, DeepSeek V4.1 Flash), with a checkmark on the current one. Takes effect on the next request. The submenu ends with "Set as default for new panes". | Submenu (radio) | Ctrl+Alt+M |
| 2 | Next model | Cycles through the models without opening the list, like Cursor's Ctrl+/ | Action | Ctrl+Alt+/ |
| 3 | Stop agent | Cancels the running turn and its tool calls. Disabled when the agent is idle, with the reason "Agent is idle". | Action | Keep the existing one |
| 4 | New chat | Clears this pane's conversation. The row shows the message count as its description. | Action | Ctrl+Alt+N |
| 5 | Input mode: *Auto* ▸ | Auto / Terminal / Agent, with a checkmark on the current mode. | Submenu (radio) | Ctrl+Alt+I (cycle) |
| 6 | Require approval for commands and writes | Toggle, **off** today (tools run without approval). An "Always ask" option like Warp's and Zed's. | Toggle ✓ | none |
| 7 | Copy last agent reply | Copies the final reply as Markdown. Agent output is inline, so it is hard to select. | Action | none |
| 8 | Show conversation / context usage | Read-only rows: token count and model context size. Enter copies the conversation as Markdown. | Info + action | none |
| 9 | Compact conversation | Summarizes the history to free context, like `/compact` in Warp and opencode. | Action (future) | none |
| 10 | Fork conversation to new pane | Opens a split with the same history, like Warp's `/fork`. | Action (future) | none |
| 11 | Provider and API keys… | Opens the BYOK settings. The description shows "OpenRouter · key set" or "no key". | Opens dialog | none |
| 12 | Import keys from Warp | Existing action. Hidden or disabled when no Warp config is found. | Action | none |
| 13 | Apply model to all panes | Sets every pane in the window to the focused pane's model. | Action | none |
| 14 | Agent keybindings… | Jumps to the keybinding editor filtered to `agent.*`. | Opens editor | none |

Leave out for now: profiles (Warp and Zed need them only because they have many models and
permission sets; Relay has 3 models and one permission level) and MCP/rules (no backend yet).
When approval (item 6) gains more than one level, add "Autonomy ▸ Always ask / Ask for risky /
Never ask" in its place.

## 3. Recommended TERMINAL palette (Ctrl+Shift+T)

| # | Label | What it does | Kind | Suggested shortcut |
|---|---|---|---|---|
| 1 | Native terminal input | Sends keystrokes straight to Konsole. The checkmark shows the current state. | Toggle ✓ | F12 |
| 2 | Interrupt shell (Ctrl+C) | Sends SIGINT to the foreground program. Disabled at an idle prompt, with the reason "Shell is idle". | Action | none (Ctrl+C in native mode) |
| 3 | Split right / Split down | Existing actions | Action | Ctrl+P / Ctrl+Shift+P |
| 4 | New tab / New window | Existing actions | Action | Ctrl+T / Ctrl+N |
| 5 | Close pane | Existing action. Label changes to "Close tab" or "Close window" when it would close one. | Action | Ctrl+W |
| 6 | Restore closed ▸ | A list of recently closed panes, tabs and windows with their directories, newest first. Enter on the parent restores the most recent. | Submenu | Ctrl+Shift+W |
| 7 | Go to pane ▸ | A list of panes as "tab · directory · running command". The directional move stays on Alt+arrows. | Submenu (iterable) | Alt+arrows |
| 8 | Clear scrollback | Konsole clear-scrollback-and-reset | Action | none |
| 9 | Find in scrollback | Konsole search bar | Action | none |
| 10 | Copy last command output | Copies the output of the last command, useful to paste into the agent | Action | none |
| 11 | Zoom / maximize pane | Toggle ✓ | Toggle | none |
| 12 | Font size ▸ (+ / − / reset) | Nested, like Windows Terminal's "Change font size..." | Submenu | Ctrl+= / Ctrl+- / Ctrl+0 |
| 13 | Open directory in file manager / copy cwd | Uses the pane's working directory | Action | none |
| 14 | Edit keybindings… / Reload keybindings | Existing actions | Action | none |

## 4. Interaction rules

**Opening and focus**
- The shortcut opens the sidebar with the filter field focused. Pressing the same shortcut again closes it.
  Pressing the *other* palette's shortcut switches palettes in place.
- The palette is **modal for keystrokes but not for output**. The terminal keeps rendering and a running TUI
  keeps running, but no key reaches Konsole or the composer until the sidebar closes. When it closes, focus
  returns to exactly where it was (composer, or native Konsole input). Ctrl+Shift+A and Ctrl+Shift+T
  would then take precedence even in native mode, like the pane shortcuts. See open decision 3.
- Do not resize the terminal when the sidebar opens (use an overlay docked on the right, not a layout
  split). Resizing sends SIGWINCH and forces TUIs like vim and htop to reflow. This is the main reason other
  programs use overlays.
- Mouse: click to run, wheel to scroll, click outside to close. Everything must work without a mouse.

**Keys** (same as kitty, WezTerm and VS Code)
- Up/Down and Ctrl+N/Ctrl+P (plus Ctrl+J/K) move the selection and wrap. PageUp/PageDown move a page; Home/End go to the first/last row
  (when the filter is empty, or with Ctrl+Home/End).
- Enter runs the item. On a toggle it flips the toggle and **keeps the palette open**; on an action it closes the palette.
  Right arrow or Enter opens a submenu (▸); Left arrow or Backspace on an empty filter goes back up.
- Esc: clears the filter if there is text; otherwise goes up one level; otherwise closes (Raycast's
  stack model). Esc never interrupts the agent or the shell.
- Tab puts the selected label in the filter, like a completion. Ctrl+Enter or Alt+Enter on a row
  opens "Change shortcut…", which records a key chord inline (JetBrains, Zed).

**Matching and ranking**
- Fuzzy subsequence matching with bonuses for word starts and consecutive characters. Also match the description,
  category and shortcut text, so typing "f12" finds the native toggle. Tolerate a single typo in words of 4+ letters (kitty).
- Aliases per item ("stop", "cancel", "abort" → Stop agent; "llm", "kimi" → Model).
- Empty filter: show a **Recent** section (last 3–5 items used in this palette) followed by the ranked
  static order. With a query: a flat ranked list, then frecency as a tiebreaker. Store the history per palette.
- Submenu entries are searchable from the top level: typing "deepseek" shows `Model › DeepSeek V4.1 Flash`,
  so choosing a model takes three keystrokes.

**Rows**
- Each row has a label, a dim description holding the current value or reason, and the shortcut right-aligned as keycaps.
- Show toggles with a checkbox and submenus with a trailing ▸ and the current value.
- Show disabled items dimmed and still selectable. Enter shows the reason in the footer and does nothing. Do not hide
  them: hiding hurts discoverability (JetBrains lists disabled actions).
- Show a destructive action (New chat when there are messages, Close window) with a warning color. Ask for confirmation only
  where Relay already asks.
- The footer shows hints for the selected row: `Enter run · → open · Alt+Enter rebind · Esc close`.

**Accessibility**
- Filter field + QListView (or QTreeView) with `QAccessible` names. Selection changes must emit
  accessible focus events so Orca announces "Model, GLM-5.3, submenu, Ctrl+Alt+M".
- The sidebar has a minimum width that fits the longest label, and follows the theme's contrast tokens. The state of a
  toggle or disabled item is shown by more than color alone.

**Implementation note.** Build one `PaletteModel` with an action registry that the toolbar,
keybindings (`backend/relay_core/keybindings.py` action IDs) and both palettes share. Each action has an
id, label, aliases, category, kind (action/toggle/submenu/info), `isEnabled()` with a reason,
`isChecked()`, and children. The palettes then become two filtered views (`agent.*` and
`terminal.*`, `pane.*`, `window.*`), and new actions show up automatically, as in Ghostty and kitty.

## 5. Open decisions for the owner

1. **Two palettes or one with scopes?** Recommendation: keep both shortcuts but share one
   engine. A query with no match in the current palette offers "Search all actions", or a `>` prefix
   searches everything. Otherwise users have to remember which palette holds "Interrupt shell".
2. **Shortcut conflicts.** Ctrl+Shift+T is "reopen closed tab" in browsers and "new tab" in
   Konsole and GNOME Terminal. Relay already maps restore to Ctrl+Shift+W. Ctrl+Shift+A is JetBrains' "find
   action", which fits. Ctrl+Shift+P (the conventional palette key) is already "split down". Decide
   whether to keep Ctrl+Shift+T or pick another key.
3. **Priority over running TUIs.** Should Ctrl+Shift+A/T work in native mode while vim, tmux or emacs
   run? The pane shortcuts already take priority, so consistency says yes. Adding a "pass next key through"
   escape hatch would cost little.
4. **Approval toggle.** Tools currently run without approval. Should the agent palette offer
   "Require approval" now (a toggle), later (a three-level autonomy menu), or never?
5. **Model scope.** Should picking a model change only the focused pane (current behavior), or should it
   also update the default for new panes? Warp keeps the selection for future prompts. Recommendation: pane only,
   with an explicit "Set as default" row in the submenu.

## Sources

- Warp command palette: https://docs.warp.dev/terminal/command-palette
- Warp keyboard shortcuts: https://docs.warp.dev/getting-started/keyboard-shortcuts
- Warp terminal and agent modes (toolbar, Ctrl+I, Esc): https://docs.warp.dev/agent-platform/local-agents/interacting-with-agents/agent-modality
- Warp profiles and permissions (Agent decides / Always ask / Always allow / Never, allow/denylists, Ctrl+Shift+I auto-approve): https://docs.warp.dev/agent-platform/agent/using-agents/agent-profiles-permissions
- Warp model choice: https://docs.warp.dev/agent-platform/agent/using-agents/model-choice
- Warp slash commands and forking: https://docs.warp.dev/agents/slash-commands , https://docs.warp.dev/agent-platform/local-agents/interacting-with-agents/conversation-forking/
- VS Code UI / Quick Open prefixes: https://code.visualstudio.com/docs/getstarted/userinterface
- VS Code Quick Pick UX guidelines: https://code.visualstudio.com/api/ux-guidelines/quick-picks
- VS Code command history setting: https://code.visualstudio.com/docs/configure/settings
- Zed agent panel: https://zed.dev/docs/ai/agent-panel ; Zed key bindings / keymap editor: https://zed.dev/docs/key-bindings
- JetBrains Find Action: https://www.jetbrains.com/help/idea/searching-everywhere.html
- WezTerm ActivateCommandPalette: https://wezterm.org/config/lua/keyassignment/ActivateCommandPalette.html
- kitty command palette: https://sw.kovidgoyal.net/kitty/kittens/command-palette/
- Windows Terminal command palette: https://learn.microsoft.com/en-us/windows/terminal/command-palette
- Ghostty 1.2 release notes: https://ghostty.org/docs/install/release-notes/1-2-0
- Claude Code interactive mode: https://code.claude.com/docs/en/interactive-mode
- Cursor keyboard shortcuts: https://cursor.com/docs/reference/keyboard-shortcuts
- opencode keybinds: https://opencode.ai/docs/keybinds/
- Raycast action panel: https://developers.raycast.com/api-reference/user-interface/action-panel
