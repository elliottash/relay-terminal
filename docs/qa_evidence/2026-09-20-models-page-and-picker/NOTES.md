# Models page and picker — implementer evidence (2026-09-20)

Xvfb, isolated profile, two literal non-key strings in the environment so two presets are "stored";
no prompt is ever submitted. `drive.sh [build-dir]` reproduces every shot.

| shot | what it shows |
|---|---|
| implementer-a-box-open.png | the pane's model box open: `glm-5.3 (main)` / `glm-5.3-flash (flash)` role rows, then one row per catalog model in rank order, lower-case, `model · provider`; then `more models…` and `⚙ customize…` |
| implementer-b-picker.png | Ctrl+Shift+M: the picker — filter, sort menu (priority), columns model · provider · reasoning · intelligence · tok/s · left; the current row bold and selected; the reasoning buttons (low · high · max) for it; ☆ favorite, customize…, use, cancel |
| implementer-c-picker-filter.png | `flash` typed: one flat list of the two matching rows, the first selected |
| implementer-d-picker-sort.png | the sort menu open (priority, a to z, intelligence, speed, most used, subscription left) |
| implementer-e-options.png | `/models`: Options › Models — the providers group: only providers with a key (ordered by intelligence), Relay Free, OpenRouter always, Claude Code and Codex with `change login` / `test`; the rest behind `+ add provider` |
| implementer-g-options-checklist.png | the checklist group: one checkbox per provider ("3 of 3 models in the picker"; off hides all and folds the group), the models indented under it; the small "openrouter fallback" switch appears under a model only once an OpenRouter key is stored (none in this profile) |
| implementer-h-options-priority.png | the priority group: ranks with ↑ ↓, "main" and "fallback 1 · /swap goes here", and the movable "fallbacks end here" line after rank 2 |
| implementer-f-options-down.png | the same page at the end of the priority list (↑ ↓ per row, reset) and the defaults group |

Seen and left as is: the box shows every model of every usable provider until the user un-checks
some on the page (eighteen rows here, with Claude Code and Codex installed on this machine); the
`glm-5.3-flash (flash)` role row and the `glm-5.3 flash · z.ai (glm)` entry both exist, because
the first is the Flash *agent role* (Alt+F, keeps the pane's main model) and the second switches
the pane's main model.
