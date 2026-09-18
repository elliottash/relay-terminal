# The model box swaps Main ↔ Flash, and the gear opens the model options modal

Implementer evidence for `issues/changes/needs_qa_llm/2026-09-18-model-dropdown-selection.md` (#M2C1),
owner report 2026-09-18: *"selecting model options in the model dropdown didnt do anything. the main
use case for that is going to be swapping between the main and flash models."*

These are **implementer** screenshots, not a QA verdict. Run `drive.sh` to reproduce:

```
docs/qa_evidence/2026-09-18-model-dropdown-selection/drive.sh [build-dir]
```

It starts its own Xvfb display, an isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`,
`RELAY_KEYRING=off` and a literal non-key string in `RELAY_GLM_CODING_API_KEY` so exactly one preset
shows as stored. No prompt is ever submitted, so no provider is contacted and no real key is involved.
Needs Xvfb, xdotool and ImageMagick. `COMBO_X`/`COMBO_Y` are the model chip's position in the 1400×880
layout; override them if the strip moves.

| File | What it shows |
|---|---|
| `implementer-a-list-open.png` | the open list: `glm-5.3`, `✓ Main agent · glm-5.3`, `Flash agent · glm-5.3-flash`, `⚙ Model options…` |
| `implementer-b-flash.png` | the Flash row picked: the chip reads "Flash agent · glm-5.3-flash" |
| `implementer-c-main.png` | the Main row picked: the chip is back to "glm-5.3" |
| `implementer-d-flash-again.png` | Flash a second time, byte-identical to `b` — the swap is repeatable in both directions |
| `implementer-e-still-flash.png` | the same pane later, still open, text typed in the composer: still Flash |
| `implementer-f-model-options.png` | the ⚙ row opened the Main / Flash / Lite modal; the chip stayed on the pane's agent |

`relay-stderr.log` is the run's stderr (empty on a clean run).

Before the fix, every one of these rows did nothing at all: `selectModel` returns early on a `role:`
or `gear:` id, and nothing else handled them.
