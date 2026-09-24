---
id: Z8P4
type: work
status: needs-verification
labels: [feature, composer, ui]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex guest in Relay, 2026-09-23
links: {plans: [], commits: [99bcdc01f93a77ad082ab627e71afc2f5d088f1d], evidence: [docs/qa_evidence/2026-09-23-prompt-grow/01-expanded.png], related: [], github: null}
---
# Let the prompt box expand for long prompts

## Issue
the system prompt box should expand more to accommodate a longer prompt, i would say, up to two-thirds of the pane's vertical space

## Done means
- A long prompt grows the input area beyond its former eight-line limit.
- The input area stops near two-thirds of the pane height and scrolls beyond that.
- Resizing the pane updates the limit without hiding the terminal or losing draft text.

## Plan
**Goal:** Let the composer show more of a long draft while keeping terminal space.

**Findings:** `Pane` sets eight lines in `src/Pane.h`; `RichEditor` computes a fixed height in `src/RichEditor.cpp`.

**Steps:** Make the editor's maximum depend on pane height, then cover growth, cap, and resizing with an editor test.

**Risks:** A small split pane has limited room, so retain the editor's compact minimum.

**Verify:** Targeted editor test, Relay build, and a screenshot of the live pane with a long draft.

## Execution Summary
The composer now grows with a long draft and caps its editor height based on the pane height. It recalculates when a pane is resized or text wraps. The isolated Relay capture shows a long draft with a scrollbar while the terminal remains visible.

![Long draft occupying about two-thirds of the pane](docs/qa_evidence/2026-09-23-prompt-grow/01-expanded.png)

## Tests
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^editor$' --output-on-failure` — passed, including growth, height cap, and width reflow.
- `scripts/relay-build --target relay` — passed.
- Manual Xvfb capture: `docs/qa_evidence/2026-09-23-prompt-grow/01-expanded.png` — long draft scrolls below its cap; terminal remains visible.
