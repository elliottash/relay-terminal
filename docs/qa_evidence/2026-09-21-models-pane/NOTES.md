# The models pane, driven (card #MDL1 t:a11, design 5.8)

`drive.sh` under Xvfb `:720`, an isolated `HOME`/`XDG_*`/`TMPDIR` at `/tmp/claude-1000/mp` (short:
`XDG_RUNTIME_DIR` holds a unix socket and the limit is 108 bytes), `RELAY_KEYRING=off`, fake
provider keys in the environment and no network turn — every step is a pane draw, a tab or a key.
The binary is `build/relay` built from the working tree by `scripts/relay-build`; `conf-before.txt`
is the whole settings file it started from, and its point is what is **not** in it:
`instructions/onboarded` is unset, which with `--workspace` (no layout to restore) is the first-run
condition the window reads.

| shot | what it shows |
| --- | --- |
| `a-first-run.png` | **First run**: a terminal pane at the left, the models pane at the right, `for: project` in its header, three tabs, and **providers** in front — a fresh profile has no key, so step 1 is the first thing to do. The providers tab is Options › Models' own section (its blurb, the providers fold, profiles, defaults) drawn by Options' own renderer with its tab row and footer taken away. |
| `b0-focused.png` | Ctrl+Shift+M from the terminal: the pane is re-targeted at it and focused, on **priorities**. |
| `b-closed.png` | Ctrl+Shift+M again, with the focus in it: **it closes**, and the terminal fills the window ("typing it again closes the pane"). |
| `c-reopened-priorities.png` | Ctrl+Shift+M once more from the terminal: open again, serving it, on priorities at its class — the class tabs `high · main · flash · lite` on a second row, the list numbered with its `in box` cutoff, the level list beside it, "show this class in the box", and a footer that spells the keys. |
| `d-providers.png` | Alt+1 — providers. |
| `e-available.png` | Alt+2 — available: the tick column, one row per model grouped by provider, the sort menu, "+ add a model by id…", and every model's **name in full** (`kimi-for-coding-highspeed`, `gpt-5.6-terra`), with the pane's own model bold and marked `· current`. |
| `f-priorities.png` | Alt+3 — back to priorities, on the class that was being looked at. |
| `g0-highlighted.png` | Rank 2 (`glm-5.3`) clicked: **a click only highlights**. The terminal pane's chip still reads `claude-fable-5.1`. |
| `g-served-pane-switched.png` | Enter: the **served pane** switched — its chip reads `glm-5.3`, `100% left` — and the models pane stayed exactly where it was. |
| `h-escape-focus-back.png` | Escape: the caret is back in the terminal pane's prompt box and the models pane is **still open**. |
| `i-options-models-row.png` | Options (Ctrl+Shift+O), "models and priorities" typed in its search: the row that replaced the five lists on the page. |
| `i2-row-opens-priorities-on-main.png` | That row pressed: the models pane goes to **priorities on main** — not to the mode the pane is in, because a page is in no mode. |
| `i1-provider-models-link.png` | Back in Options, "available models" typed: the per-provider **models… (N of M available)** links, one under each provider. |
| `j-link-lands-on-available.png` | One pressed: the models pane's **available** tab, filtered to that provider (`kimi`), which is how step 2 is reached from step 1. |

## What the runs changed in the code

Three faults this evidence found, each fixed before the shots above were taken:

1. **Ctrl+Tab did nothing.** The first run pressed it three times to walk the three tabs and stayed
   on the same one: Ctrl+Tab is the window's **Next tab** (Keymap `tab.next`) and never reaches a
   pane. The tabs are **Alt+1/2/3** now, and ←/→ where the class row is not in front.
2. **Alt+2 typed "2" into the providers tab's search box.** An event filter installed on the
   embedded `SettingsPane` never sees what its own `QLineEdit` swallows. The three tabs are a
   `QShortcut` with `Qt::WidgetWithChildrenShortcut` as well, which fires wherever the focus is.
3. **The model's name was elided to `claude-o…`.** Nine columns in half a window's width left the
   one thing rule 1 says every surface must print with 95 px. Hosted, the widget drops the
   `intelligence` and `tok/s` columns — what the sort menu sorts by, not what is read while
   picking, and both are in every cell's tooltip. `e-available.png` is the run after.

A fourth was caught by the first-run shot rather than by a key: the pane was built before its
worker had answered `presets`, so it had **no catalog at all** and `rebuild()` redrew the rows from
the one it was built with. `ModelPicker::setCatalog` is what a re-read calls now, and
`RelayWindow::refreshModelsPane` is wired to `SettingsWatch`, which both a `presets` answer and a
key added on the providers tab already fire.

## Not covered here

The **restored** models pane (`{"models": {"cwd", "tab"}}` in a saved layout) is exercised by the
first-run path — that layout node is built by `WindowManager::newWindowAt` and read by the same
`buildNode` branch a saved one takes — but not by a quit-and-reopen round trip, which needs a
second Relay start against the same profile.
