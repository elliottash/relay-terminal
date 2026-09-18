| foreground | on | rule | measured | |
|---|---|---|---|---|
| `text` `#ece6e0` | `background` `#0e0f12` | AA | **15.48:1** | pass — body text on the window ground |
| `text` `#ece6e0` | `surface` `#15161a` | AA | **14.60:1** | pass — text in editors, previews, the idle composer |
| `text` `#ece6e0` | `surface_raised` `#241c18` | AA | **13.52:1** | pass — text on chips, menus, the focused composer |
| `text_muted` `#a2968c` | `background` `#0e0f12` | AA | **6.64:1** | pass — cwd, hints, notification bodies |
| `text_muted` `#a2968c` | `surface` `#15161a` | AA | **6.27:1** | pass — secondary text on the text surface |
| `text_muted` `#a2968c` | `surface_raised` `#241c18` | AA | **5.80:1** | pass — chip labels: the whole status strip |
| `accent` `#c07a4a` | `background` `#0e0f12` | AA | **5.59:1** | pass — accent used as text (the route label, the running queue) |
| `accent_text` `#1a0f08` | `accent` `#c07a4a` | AA | **5.49:1** | pass — a primary button's label |
| `shell` `#45c8ee` | `surface_raised` `#241c18` | AA | **8.57:1** | pass — the mode chip reading TERMINAL |
| `agent` `#ab97f7` | `surface_raised` `#241c18` | AA | **6.78:1** | pass — the mode chip reading AGENT |
| `shell` `#45c8ee` | `background` `#0e0f12` | AA | **9.81:1** | pass — the shell destination as text |
| `agent` `#ab97f7` | `background` `#0e0f12` | AA | **7.76:1** | pass — the agent destination as text; agent prompt echo |
| `on.shell` `#051f27` | `shell` `#45c8ee` | AA | **8.73:1** | pass — the agent prefix chip (ink on the shell fill) |
| `on.agent` `#0c0527` | `agent` `#ab97f7` | AA | **7.97:1** | pass — the plan chip (ink on the agent fill) |
| `on.warning` `#251a07` | `warning` `#e5c07b` | AA | **9.89:1** | pass — the shell prefix chip (ink on amber) |
| `on.caution` `#251407` | `caution` `#e4a779` | AA | **8.55:1** | pass — the secret chip |
| `on.selection` `#f7f1ed` | `selection` `#5a3a26` | AA | **9.07:1** | pass — selected text in every input |
| `success` `#7ec88c` | `surface_raised` `#241c18` | AA | **8.39:1** | pass — the tasks chip, done |
| `warning` `#e5c07b` | `surface_raised` `#241c18` | AA | **9.69:1** | pass — the tasks chip, attention; warn labels |
| `error` `#e06c75` | `surface_raised` `#241c18` | AA | **5.24:1** | pass — an error on a chip |
| `success` `#7ec88c` | `background` `#0e0f12` | AA | **9.61:1** | pass — a success dot on a notification row |
| `warning` `#e5c07b` | `background` `#0e0f12` | AA | **11.10:1** | pass — a warning dot on a notification row |
| `error` `#e06c75` | `background` `#0e0f12` | AA | **6.00:1** | pass — an error dot on a notification row |
| `border_strong` `#666f7a` | `background` `#0e0f12` | UI | **3.76:1** | pass — the focused pane's outline |
| `border` `#3a2822` | `background` `#0e0f12` | deco | **1.37:1** | pass — a hairline rule |
| `syntax.command` `#45c8ee` | `surface` `#15161a` | AA | **9.25:1** | pass — composer command, idle |
| `syntax.command` `#45c8ee` | `surface_raised` `#241c18` | AA | **8.57:1** | pass — composer command, focused (you are typing) |
| `syntax.unknown` `#f07178` | `surface` `#15161a` | AA | **6.32:1** | pass — composer unknown, idle |
| `syntax.unknown` `#f07178` | `surface_raised` `#241c18` | AA | **5.85:1** | pass — composer unknown, focused (you are typing) |
| `syntax.flag` `#e5c07b` | `surface` `#15161a` | AA | **10.47:1** | pass — composer flag, idle |
| `syntax.flag` `#e5c07b` | `surface_raised` `#241c18` | AA | **9.69:1** | pass — composer flag, focused (you are typing) |
| `syntax.string` `#7ec88c` | `surface` `#15161a` | AA | **9.06:1** | pass — composer string, idle |
| `syntax.string` `#7ec88c` | `surface_raised` `#241c18` | AA | **8.39:1** | pass — composer string, focused (you are typing) |
| `syntax.path` `#66d0c0` | `surface` `#15161a` | AA | **9.77:1** | pass — composer path, idle |
| `syntax.path` `#66d0c0` | `surface_raised` `#241c18` | AA | **9.05:1** | pass — composer path, focused (you are typing) |
| `syntax.operator` `#9a938a` | `surface` `#15161a` | AA | **5.95:1** | pass — composer operator, idle |
| `syntax.operator` `#9a938a` | `surface_raised` `#241c18` | AA | **5.51:1** | pass — composer operator, focused (you are typing) |
| `syntax.variable` `#ab97f7` | `surface` `#15161a` | AA | **7.32:1** | pass — composer variable, idle |
| `syntax.variable` `#ab97f7` | `surface_raised` `#241c18` | AA | **6.78:1** | pass — composer variable, focused (you are typing) |
| `syntax.agent` `#ab97f7` | `surface` `#15161a` | AA | **7.32:1** | pass — composer agent, idle |
| `syntax.agent` `#ab97f7` | `surface_raised` `#241c18` | AA | **6.78:1** | pass — composer agent, focused (you are typing) |
| `syntax.token` `#45c8ee` | `surface` `#15161a` | AA | **9.25:1** | pass — composer token, idle |
| `syntax.token` `#45c8ee` | `surface_raised` `#241c18` | AA | **8.57:1** | pass — composer token, focused (you are typing) |
| `term.fg` `#dad4ce` | `term.bg` `#0e0f12` | AA | **13.04:1** | pass — terminal output |
| `term.cursor` `#f4efe9` | `term.bg` `#0e0f12` | UI | **16.76:1** | pass — the cursor block |
| `ansi1` `#f2777a` | `term.bg` `#0e0f12` | AA | **7.02:1** | pass — ANSI 1 as text |
| `ansi2` `#7dd399` | `term.bg` `#0e0f12` | AA | **10.63:1** | pass — ANSI 2 as text |
| `ansi3` `#ecc476` | `term.bg` `#0e0f12` | AA | **11.61:1** | pass — ANSI 3 as text |
| `ansi4` `#61afef` | `term.bg` `#0e0f12` | AA | **8.11:1** | pass — ANSI 4 as text |
| `ansi5` `#c09ae9` | `term.bg` `#0e0f12` | AA | **8.25:1** | pass — ANSI 5 as text |
| `ansi6` `#56c8d8` | `term.bg` `#0e0f12` | AA | **9.72:1** | pass — ANSI 6 as text |
| `ansi7` `#cbc3bb` | `term.bg` `#0e0f12` | AA | **11.01:1** | pass — ANSI 7 as text |
| `ansi9` `#ff8c8f` | `term.bg` `#0e0f12` | AA | **8.58:1** | pass — ANSI 9 as text |
| `ansi10` `#98e5b0` | `term.bg` `#0e0f12` | AA | **12.93:1** | pass — ANSI 10 as text |
| `ansi11` `#f8d58e` | `term.bg` `#0e0f12` | AA | **13.60:1** | pass — ANSI 11 as text |
| `ansi12` `#81c4ff` | `term.bg` `#0e0f12` | AA | **10.29:1** | pass — ANSI 12 as text |
| `ansi13` `#d2b0f5` | `term.bg` `#0e0f12` | AA | **10.28:1** | pass — ANSI 13 as text |
| `ansi14` `#78ddea` | `term.bg` `#0e0f12` | AA | **12.16:1** | pass — ANSI 14 as text |
| `ansi15` `#f4efe9` | `term.bg` `#0e0f12` | AA | **16.76:1** | pass — ANSI 15 as text |
| `ansi8` `#6e645c` | `term.bg` `#0e0f12` | UI | **3.32:1** | pass — ANSI 8, the dim colour |

| distinctness | ΔE76 | |
|---|---|---|
| `accent` vs `warning` | 28.5 | ok |
| `accent` vs `error` | 31.2 | ok |
| `border_strong` vs `warning` | 57.6 | ok |
| `border_strong` vs `error` | 54.3 | ok |
| `surface_raised` vs `warning` | 77.0 | ok |
| `border` vs `error` | 57.6 | ok |
| `shell` vs `agent` | 54.6 | ok |
