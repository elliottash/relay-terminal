---
id: EM1E
type: work
status: needs-qa-llm
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17
rank: '42'
created: '2026-09-17'
acceptance: an image pasted or dropped into the composer reaches the model; on GLM 5.3 the turn is sent to GLM 5.3 Flash automatically; presets without vision say so instead of failing. Verified live against Z.AI (swap and swap-back, answer read from the picture) and OpenRouter (refusal, nothing sent); a non-Claude model QA session runs the checklist below and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt`, 2026-09-17: "we need image context (glm 5.3 swaps to glm 5.3 flash for that)"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-17-image-context/'], related: [], github: null}
---
# Image context in agent prompts

## Open questions
1. Inputs: paste, drag-and-drop, file path, and a "screenshot this pane" action?
2. GLM: swap to GLM 5.3 Flash only for turns that carry an image (recommended), then back?
3. Presets without vision: route that turn to a configured vision model, or refuse with a message?
4. Keep images in session history (context cost) or replace with a text description after the turn?

## Decisions (owner, 2026-09-17)
All recommendations accepted: paste, drag-and-drop, file paths and a "screenshot this pane" action; GLM swaps to GLM 5.3
Flash only for turns with an image and says so; presets without vision use the configured vision model, else refuse
with a message; images stay for their turn and are then replaced by a short description plus the file path. The user
can choose a **vision model** separate from the main model in Agent options.

## Behavior as implemented (2026-09-17)

Protocol: [`docs/AGENT-SESSIONS-PROTOCOL.md` section 17](../../../docs/AGENT-SESSIONS-PROTOCOL.md).
Architecture: [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md), section 11 "Image context".

**Four inputs, one path.** Pasting or dropping a picture into the prompt box writes it to
`$XDG_CACHE_HOME/relay/images` (captures older than 7 days are swept) and inserts its `@path` token;
an image file *named* by a drop or typed as `@path` is attached where it is and never copied;
`agent.screenshotPane` (**Ctrl+Shift+G**, Actions › "Screenshot this pane") grabs the pane as drawn
and attaches that. Everything after that is the existing `ask {attachments}` plumbing, so there is no
new message on the wire and one code path to reason about.

**The worker decides by bytes, not by file name.** PNG, JPEG, WebP and GIF become
`kind: "image"` attachments; anything else is text as before. An image is sent as an
OpenAI-compatible `image_url` content part whose URL is an inlined base64 data URL — never an http
URL, so a picture of the user's screen reaches the configured provider and nobody else. Caps: 3 MiB
per image, 4 images and 6 MiB per turn, checked on both sides.

**Model for the turn.** Decided once, before the first request, from the model id
(`presets.model_supports_vision`):

| Case | What happens |
|---|---|
| The pane's model reads images | nothing changes, no event |
| It does not, and a vision model resolves | that turn only runs on it, then the pane goes back (`vision_route` / `vision_route_ended`) |
| It does not, and none resolves | refused before anything is sent (`vision_unavailable` + `error`) |

The vision model is the `vision` role; its default is the provider's own image model, which on
`glm` and `glm-coding` is `glm-5.3-flash` — the owner's "GLM swaps to GLM 5.3 Flash for that turn".
It has its own **Vision model** row beside Main / Flash / Lite in Settings › Models, set to
"Automatic" or pinned to any provider with a stored key. A vision model the user picked by hand wins
even over a main model that can read images.

The pane says so: a routing line above the turn ("🖼 Image in this prompt · this turn runs on
glm-5.3-flash, then back to glm-5.3.") and the model chip reading "🖼 glm-5.3-flash · this turn"
until the turn ends.

**Images live for one turn.** When the turn ends — done, error or cancelled — every image part is
replaced in place by one line naming the file, its type and size, so the conversation still records
that a picture was there, later turns cost nothing for it, and saved sessions stay plain text. For
context accounting an image counts as a flat constant, never as the length of its base64, so a
screenshot cannot compact its own turn.

**Shortcut hints** (WARP.md standing rule): dropping a file hints the paste shortcut, and reaching
"Screenshot this pane" from the palette hints Ctrl+Shift+G. The palette's hint now fires *after* the
action rather than before it, because an action that says something itself used to overwrite its own
hint on the same toast.

## Implementer check (not a QA verdict)

Claude Opus 5 via Claude Code, 2026-09-17. Evidence:
[`docs/qa_evidence/2026-09-17-image-context/`](../../../docs/qa_evidence/2026-09-17-image-context/).

- `./scripts/test.sh`: 535 passed (27 of them new in `tests/test_images.py`).
- `ctest` against a build configured in this worktree: 17/17 passed, including the new `images` test
  (13 cases) and `editor` (15, three new).
- Live against the user's own keys: on Z.AI Coding Plan a prompt carrying a red PNG was served by
  `glm-5.3-flash`, answered "Red" from the picture, and the pane was back on `glm-5.3` for the next
  turn — once through the worker API and once through the GUI. On OpenRouter
  (`deepseek/deepseek-v4.1-flash`, no vision, no vision model) the turn was refused with a message
  and **no request was made**.
- GUI verified live under `xvfb-run` with an isolated `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`/
  `XDG_CACHE_HOME`; screenshots `implementer-01` … `implementer-08`.

Not verified: drag-and-drop on a real desktop (xdotool cannot originate an XDND drag), a pinned
vision model on a real endpoint, non-PNG images through a provider, the size caps through the GUI,
and multi-image turns. The turn header and the "Thinking…" overlay still name the pane's own model
while an image turn runs elsewhere; the routing line and the chip name the serving model, and the
pane header is owned by another change in flight (`JRWQ`).

## QA checklist

1. **Paste.** Copy a screenshot to the clipboard, click into the prompt box, Ctrl+V: an `@path`
   under `…/cache/RelayTerminal/relay/images/relay-paste-*.png` appears with "Image attached". Type
   a question and send it: the model answers about the picture.
2. **Drag-and-drop.** Drag a PNG from a file manager onto the prompt box. It must be attached *by
   its own path* (not copied into the cache), and the hint "Next time: Ctrl+V …" must show. Drag a
   `.txt` file instead: it must not be treated as an image.
3. **File path.** Type `@` and pick an image with the file picker; also try a path with a space in
   it, which must arrive quoted.
4. **Screenshot this pane.** Ctrl+Shift+G, and again from the palette. From the palette the hint
   "Next time: Ctrl+Shift+G" must be the thing left on screen; from the key it must not appear.
   Ask "what is on this screen?" and check the answer matches the pane.
5. **The GLM swap.** On a pane running `glm-5.3`, send a prompt with an image. The routing line must
   name `glm-5.3-flash`, the model chip must read "🖼 glm-5.3-flash · this turn", and after the turn
   the chip must be back to `glm-5.3`. Send a text-only prompt next and confirm from
   `~/.local/share/relay/logs/` (or the worker log) that it went to `glm-5.3`.
6. **Swap-back after a failure.** Start an image turn and press Esc. The pane must go back to its own
   model (`vision_route_ended` before `cancelled`), and the next turn must run on it.
7. **The refusal.** Switch the pane to a provider without vision and with Vision model on
   "Automatic" (OpenRouter's DeepSeek, Kimi, MiniMax). Send an image: the pane must say there is no
   vision model and name Settings › Models › Vision model, nothing must be sent, and the prompt must
   still be in the conversation so it can be re-sent.
8. **The vision model.** In Settings › Models, pin Vision model to a provider with a key and repeat
   step 7: the turn must now run there and come back. Set it back to Automatic.
9. **Images live for one turn.** After an image turn, ask a follow-up question about the picture
   ("what colour was it again?"). The agent should answer from what it said, and the conversation
   (Ctrl+Shift+O → open it, or the session JSON under `~/.local/share/relay/sessions`) must contain
   the "[Image attached earlier … and since removed from it: /path …]" line and **no base64**.
10. **Caps.** Attach an image larger than 3 MiB: the pane must say so before sending. Attach five
    images in one prompt: the turn must be refused with a message, not sent.
11. **Formats.** Repeat step 1 with a JPEG, a WebP and a GIF. Rename a PNG to `.jpg` and attach it:
    it must still be recognised as a PNG and work.
12. **Nothing leaks.** With `relay/logs` at debug level, confirm no base64 and no key appears in the
    log, and that the only host contacted for an image turn is the vision model's provider.
