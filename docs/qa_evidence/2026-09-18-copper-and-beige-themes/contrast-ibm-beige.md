| foreground | on | rule | measured | |
|---|---|---|---|---|
| `text` `#1f1c16` | `background` `#cdc0a8` | AA | **9.47:1** | pass — body text on the window ground |
| `text` `#1f1c16` | `surface` `#f3f2f0` | AA | **15.19:1** | pass — text in editors, previews, the idle composer |
| `text` `#1f1c16` | `surface_raised` `#ded3bf` | AA | **11.47:1** | pass — text on chips, menus, the focused composer |
| `text_muted` `#4d463a` | `background` `#cdc0a8` | AA | **5.19:1** | pass — cwd, hints, notification bodies |
| `text_muted` `#4d463a` | `surface` `#f3f2f0` | AA | **8.33:1** | pass — secondary text on the text surface |
| `text_muted` `#4d463a` | `surface_raised` `#ded3bf` | AA | **6.29:1** | pass — chip labels: the whole status strip |
| `accent` `#1a3070` | `background` `#cdc0a8` | AA | **6.88:1** | pass — accent used as text (the route label, the running queue) |
| `accent_text` `#f3f2f0` | `accent` `#1a3070` | AA | **11.04:1** | pass — a primary button's label |
| `shell` `#324d5c` | `surface_raised` `#ded3bf` | AA | **6.03:1** | pass — the mode chip reading TERMINAL |
| `agent` `#4f4163` | `surface_raised` `#ded3bf` | AA | **6.25:1** | pass — the mode chip reading AGENT |
| `shell` `#324d5c` | `background` `#cdc0a8` | AA | **4.98:1** | pass — the shell destination as text |
| `agent` `#4f4163` | `background` `#cdc0a8` | AA | **5.16:1** | pass — the agent destination as text; agent prompt echo |
| `on.shell` `#eef3f6` | `shell` `#324d5c` | AA | **7.99:1** | pass — the agent prefix chip (ink on the shell fill) |
| `on.agent` `#f2eff5` | `agent` `#4f4163` | AA | **8.13:1** | pass — the plan chip (ink on the agent fill) |
| `on.warning` `#fcf6e8` | `warning` `#684800` | AA | **7.75:1** | pass — the shell prefix chip (ink on amber) |
| `on.caution` `#fcf1e8` | `caution` `#753b07` | AA | **7.92:1** | pass — the secret chip |
| `on.selection` `#eaeefa` | `selection` `#1a3070` | AA | **10.65:1** | pass — selected text in every input |
| `success` `#135427` | `surface_raised` `#ded3bf` | AA | **6.10:1** | pass — the tasks chip, done |
| `warning` `#684800` | `surface_raised` `#ded3bf` | AA | **5.64:1** | pass — the tasks chip, attention; warn labels |
| `error` `#931c17` | `surface_raised` `#ded3bf` | AA | **5.86:1** | pass — an error on a chip |
| `success` `#135427` | `background` `#cdc0a8` | AA | **5.03:1** | pass — a success dot on a notification row |
| `warning` `#684800` | `background` `#cdc0a8` | AA | **4.65:1** | pass — a warning dot on a notification row |
| `error` `#931c17` | `background` `#cdc0a8` | AA | **4.84:1** | pass — an error dot on a notification row |
| `border_strong` `#655244` | `background` `#cdc0a8` | UI | **4.11:1** | pass — the focused pane's outline |
| `border` `#9c8d74` | `background` `#cdc0a8` | deco | **1.81:1** | pass — a hairline rule |
| `syntax.command` `#324d5c` | `surface` `#f3f2f0` | AA | **7.98:1** | pass — composer command, idle |
| `syntax.command` `#324d5c` | `surface_raised` `#ded3bf` | AA | **6.03:1** | pass — composer command, focused (you are typing) |
| `syntax.unknown` `#931c17` | `surface` `#f3f2f0` | AA | **7.76:1** | pass — composer unknown, idle |
| `syntax.unknown` `#931c17` | `surface_raised` `#ded3bf` | AA | **5.86:1** | pass — composer unknown, focused (you are typing) |
| `syntax.flag` `#684800` | `surface` `#f3f2f0` | AA | **7.46:1** | pass — composer flag, idle |
| `syntax.flag` `#684800` | `surface_raised` `#ded3bf` | AA | **5.64:1** | pass — composer flag, focused (you are typing) |
| `syntax.string` `#135427` | `surface` `#f3f2f0` | AA | **8.07:1** | pass — composer string, idle |
| `syntax.string` `#135427` | `surface_raised` `#ded3bf` | AA | **6.10:1** | pass — composer string, focused (you are typing) |
| `syntax.path` `#0f5f5a` | `surface` `#f3f2f0` | AA | **6.69:1** | pass — composer path, idle |
| `syntax.path` `#0f5f5a` | `surface_raised` `#ded3bf` | AA | **5.05:1** | pass — composer path, focused (you are typing) |
| `syntax.operator` `#5a554a` | `surface` `#f3f2f0` | AA | **6.63:1** | pass — composer operator, idle |
| `syntax.operator` `#5a554a` | `surface_raised` `#ded3bf` | AA | **5.00:1** | pass — composer operator, focused (you are typing) |
| `syntax.variable` `#4f4163` | `surface` `#f3f2f0` | AA | **8.27:1** | pass — composer variable, idle |
| `syntax.variable` `#4f4163` | `surface_raised` `#ded3bf` | AA | **6.25:1** | pass — composer variable, focused (you are typing) |
| `syntax.agent` `#4f4163` | `surface` `#f3f2f0` | AA | **8.27:1** | pass — composer agent, idle |
| `syntax.agent` `#4f4163` | `surface_raised` `#ded3bf` | AA | **6.25:1** | pass — composer agent, focused (you are typing) |
| `syntax.token` `#324d5c` | `surface` `#f3f2f0` | AA | **7.98:1** | pass — composer token, idle |
| `syntax.token` `#324d5c` | `surface_raised` `#ded3bf` | AA | **6.03:1** | pass — composer token, focused (you are typing) |
| `term.fg` `#1f1c16` | `term.bg` `#f3f2f0` | AA | **15.19:1** | pass — terminal output |
| `term.cursor` `#23211a` | `term.bg` `#f3f2f0` | UI | **14.40:1** | pass — the cursor block |
| `ansi1` `#a8201a` | `term.bg` `#f3f2f0` | AA | **6.50:1** | pass — ANSI 1 as text |
| `ansi2` `#18602f` | `term.bg` `#f3f2f0` | AA | **6.82:1** | pass — ANSI 2 as text |
| `ansi3` `#7a5400` | `term.bg` `#f3f2f0` | AA | **6.06:1** | pass — ANSI 3 as text |
| `ansi4` `#0a52a1` | `term.bg` `#f3f2f0` | AA | **6.86:1** | pass — ANSI 4 as text |
| `ansi5` `#7a2f8f` | `term.bg` `#f3f2f0` | AA | **7.13:1** | pass — ANSI 5 as text |
| `ansi6` `#0f5f5a` | `term.bg` `#f3f2f0` | AA | **6.69:1** | pass — ANSI 6 as text |
| `ansi7` `#4a4638` | `term.bg` `#f3f2f0` | AA | **8.44:1** | pass — ANSI 7 as text |
| `ansi9` `#8a140f` | `term.bg` `#f3f2f0` | AA | **8.58:1** | pass — ANSI 9 as text |
| `ansi10` `#12522a` | `term.bg` `#f3f2f0` | AA | **8.28:1** | pass — ANSI 10 as text |
| `ansi11` `#5e4200` | `term.bg` `#f3f2f0` | AA | **8.32:1** | pass — ANSI 11 as text |
| `ansi12` `#06408a` | `term.bg` `#f3f2f0` | AA | **8.90:1** | pass — ANSI 12 as text |
| `ansi13` `#5f2270` | `term.bg` `#f3f2f0` | AA | **9.63:1** | pass — ANSI 13 as text |
| `ansi14` `#0a4a46` | `term.bg` `#f3f2f0` | AA | **9.02:1** | pass — ANSI 14 as text |
| `ansi15` `#14120d` | `term.bg` `#f3f2f0` | AA | **16.73:1** | pass — ANSI 15 as text |
| `ansi8` `#6e6858` | `term.bg` `#f3f2f0` | UI | **4.96:1** | pass — ANSI 8, the dim colour |

| distinctness | ΔE76 | |
|---|---|---|
| `accent` vs `warning` | 83.0 | ok |
| `accent` vs `error` | 81.5 | ok |
| `border_strong` vs `warning` | 31.3 | ok |
| `border_strong` vs `error` | 48.7 | ok |
| `surface_raised` vs `warning` | 60.8 | ok |
| `border` vs `error` | 57.2 | ok |
| `shell` vs `agent` | 20.3 | ok |
