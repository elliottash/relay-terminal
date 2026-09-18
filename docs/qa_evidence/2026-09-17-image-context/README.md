# Evidence — Image context in agent prompts (`EM1E`)

Implementer evidence for
[`issues/features/needs_qa_llm/2026-09-17-image-context.md`](../../../issues/features/needs_qa_llm/2026-09-17-image-context.md).
**Not a QA verdict.** Files prefixed `implementer-` were produced by the implementing model
(Claude Opus 5, via Claude Code, 2026-09-17).

| File | What it shows |
|---|---|
| `implementer-images-tests.txt` | `relay-images-tests` (13 passed) and `relay-editor-tests` (15 passed) under offscreen Qt: image sniffing by bytes, capture naming, PNG writing, what a paste or drop carries, the cache sweep, the cap shared with the worker, and the composer turning an image paste into `@path` tokens. |
| `implementer-test-images-py.txt` | `tests/test_images.py`: 27 passed — attachment loading, the content parts and their caps, the GLM Flash swap and swap-back, the refusal, the configured vision model, and the post-turn replacement. Offline against a recording provider. |
| `implementer-live-glm.txt` | **Live**, against the user's own keys: the GLM swap to `glm-5.3-flash` and back with a real answer read from the picture, and the refusal on a preset without vision (nothing sent). No key material. |
| `implementer-driver.sh` | The Xvfb driver that produced the screenshots: isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/`XDG_CACHE_HOME`, started on the stored `glm-coding` key. |
| `implementer-relay.log` | The GUI log from that run (`gui_start` → `configured model=glm-5.3` → one turn `done`). |
| `implementer-01-startup.png` | Relay under Xvfb in the isolated profile, configured on Z.AI Coding Plan. |
| `implementer-02-palette-screenshot-action.png` | The actions palette filtered by "screenshot": **Screenshot this pane · Ctrl+Shift+G**. |
| `implementer-03-pane-screenshot-attached.png` | Running it from the palette: the capture's `@path` is in the prompt box and the hint left on screen is "Next time: Ctrl+Shift+G · screenshot this pane". |
| `implementer-04-pasted-image.png` | Ctrl+V with a PNG on the clipboard: it is written to `…/cache/RelayTerminal/relay/images/relay-paste-*.png`, its `@path` is inserted, and the pane says "Image attached · it goes to the agent with your next prompt". |
| `implementer-05-prompt-ready.png` | The same prompt box with the question typed after the token. |
| `implementer-06-image-turn-running.png` | The turn running: "🖼 Image in this prompt · this turn runs on glm-5.3-flash, then back to glm-5.3." and the model chip reading **🖼 glm-5.3-flash · this turn**. |
| `implementer-07-image-turn-answer.png` | The answer — "Red", read from the picture, no tools — and the chip back to **glm-5.3**. |
| `implementer-08-vision-model-row.png` | Settings › Models: **Vision model · glm-5.3-flash** on its own row beside Main / Flash / Lite, set to "Automatic". |

## What was verified live against a provider

The user's keyring holds keys for `glm-coding`, `kimi` and `openrouter`. Both live paths that those
keys can reach were exercised; see `implementer-live-glm.txt` for the transcripts.

- **The GLM swap.** A prompt carrying a 96×96 red PNG on a pane running `glm-5.3` was served by
  `glm-5.3-flash`, answered "Red" from the image itself, and the pane was back on `glm-5.3` for the
  next turn. Done twice: once through the worker API, once through the GUI (screenshots 06 and 07).
- **The refusal.** On `openrouter` (`deepseek/deepseek-v4.1-flash`, text-only) with no vision model,
  the turn was refused with a message and **no request was made**.

## Gaps for QA to close

- **Drag-and-drop was not exercised live.** `xdotool` cannot originate an XDND drag, so the drop path
  is covered by unit tests only (`relay::images::fromMimeData` with a URL list, and the composer's
  `dropEvent` → `insertFromMimeData` hook). The hint it shows ("Next time: Ctrl+V …") has therefore
  never been seen on screen. Dragging a PNG from a file manager into the prompt box is the first
  thing to try.
- **The configured vision model on a real endpoint.** Pinning `roles/vision` to another provider is
  unit-tested but was not run live.
- **Non-PNG images.** Every live and scripted attachment was a PNG. JPEG, WebP and GIF are sniffed
  and unit-tested but were never sent to a provider.
- **The caps.** 3 MiB per image, 4 images and 6 MiB per turn are unit-tested; no oversized image was
  pushed through the GUI.
- **Cosmetic:** the turn header and the "Thinking…" overlay still name the pane's own model
  (`glm-5.3`) while an image turn runs on `glm-5.3-flash`. The routing line above the turn and the
  model chip both name the serving model, so nothing is hidden, but the two labels disagree.
  Deliberately left alone: another change in flight (`JRWQ`) owns the pane header.
- **Multi-image turns** (up to 4) were never sent live.
