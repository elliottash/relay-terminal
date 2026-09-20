# Models page and picker — implementer evidence (2026-09-20)

Xvfb, isolated profile, two literal non-key strings in the environment so two presets are "stored";
no prompt is ever submitted. `drive.sh [build-dir]` reproduces every shot.

| shot | what it shows |
|---|---|
| implementer-a-box-open.png | the pane's model box open: `glm-5.3 (main)` / `glm-5.3-flash (flash)` role rows, then one row per catalog model in rank order, lower-case, `model · provider`; then `more models…` and `⚙ customize…` |
| implementer-b-picker.png | Ctrl+Alt+M: the picker — filter, sort menu, the model list, and the reasoning level as its own list at the right, preset to the tier lists' level for the highlighted model (→ moves into it, ← back, Enter uses both); ☆ favorite, customize…, use, cancel. The composer strip below shows the model box and, right of it, the thin level box (Alt+E) |
| implementer-c-picker-filter.png | `flash` typed: one flat list of the two matching rows, the first selected |
| implementer-d-picker-sort.png | the sort menu open (priority, a to z, intelligence, speed, most used, subscription left) |
| implementer-e-options.png | `/models`: Options › Models — every section heading folds; providers fold by default once one is set up (▸ providers), the checklist open beneath |
| implementer-g-options-checklist.png | the checklist group: one checkbox per provider ("3 of 3 models in the picker"; off hides all and folds the group), the models indented under it; the small "openrouter fallback" switch appears under a model only once an OpenRouter key is stored (none in this profile) |
| implementer-h-options-priority.png | the tier lists (evening): main models filled from the worker's defaults — fable · claude code at max, glm-5.3 at high, k3 at high, relay main at medium — then high models at max, flash at low; each row a grip, the level in the provider's words, and × |
| implementer-f-options-down.png | the same page at the end of the priority list (↑ ↓ per row, reset) and the defaults group |

Seen and left as is: the box shows every model of every usable provider until the user un-checks
some on the page (eighteen rows here, with Claude Code and Codex installed on this machine); the
`glm-5.3-flash (flash)` role row and the `glm-5.3 flash · z.ai (glm)` entry both exist, because
the first is the Flash *agent role* (Alt+F, keeps the pane's main model) and the second switches
the pane's main model.
