# Dark Copper and IBM Beige: the audition (`#0JA7`)

Card: [`issues/features/needs_qa_llm/2026-09-17-color-themes.md`](../../../issues/features/needs_qa_llm/2026-09-17-color-themes.md)
("Audition" section). Design and verdict: [`docs/THEMES.md`](../../THEMES.md) §4, §5, §10.

**Not a QA verdict.** Produced by the implementing model, Claude Opus 5 (1M context), under Xvfb
with an isolated profile. A QA session from a different model family should check it.

## How the screenshots were made

`drive.sh [build-dir]`: one fresh Relay per theme, 1400×880, the same scene in all six, each
pinned to its theme through `theme/name` in an isolated `relay.conf`:

1. `ls --color=always -F`, then a failing `make`;
2. an agent turn that runs `make`, writes the fix (a real `write_file` diff, computed by Relay) and
   answers. The provider is `stub-provider.py` on 127.0.0.1: no network, no real key (the recipe
   from `../2026-09-17-website-update/`);
3. a shell line typed, not sent, into the focused composer, so every syntax colour and the status
   strip are on screen → `implementer-<theme>-a-session.png`;
4. `/switchboard` → `implementer-<theme>-b-switchboard.png`.

**The binary.** `main` at `c0ac7f7` does not build: `src/main.cpp` includes `MarkdownAnsi.h`, which
is not committed yet (an in-flight merge). The screenshots come from a throwaway build outside the
tree — `main` + this change + the owner's uncommitted `MarkdownAnsi.{h,cpp}`, injected by a CMake
`CMAKE_PROJECT_INCLUDE` overlay kept in the session scratchpad. No repository file was touched to
make it build. The unit tests ran in that build too.

## Files

| file | what it shows |
|---|---|
| `contact-all-six-session.png` | **the sheet to look at first**: all six themes, same scene, 3×2 |
| `head-to-head-dark.png` | Relay Dark (default) vs Dark Copper, larger |
| `head-to-head-light.png` | Relay Light vs IBM Beige, larger |
| `contact-composer-and-status-strip.png` | the bottom 150px of every theme at full size: the composer's syntax colours, the destination chip, the status strip |
| `contact-all-six-switchboard.png` | the Switchboard pane in each theme (see limits below) |
| `implementer-<theme>-a-session.png`, `-b-switchboard.png` | the twelve full-size captures |
| `contrast.py` | the measuring tool: WCAG ratios for every painted pair, including the ink-on-fill colours derived exactly as `src/Theme.cpp` does, plus CIELAB ΔE for colours that must not be confused |
| `contrast-check.txt` | `contrast.py check` for all six: both new themes 59/59; the incumbents' failures listed |
| `contrast-dark-copper.md`, `contrast-ibm-beige.md` | every pair, every value, plus the distinctness checks |
| `contrast-all-six.md` | every pair side by side across the six themes |
| `ref-switchboard.jpg`, `ref-beige.jpg` | the owner's two references, sampled in `docs/THEMES.md` §4.1 and §5.1 |
| `stub-provider.py` | the loopback provider |
| `relay-stderr-<theme>.log` | one per launch |

## What the screenshots found that the numbers did not

- **Agent text ignores the theme.** In both light themes the agent's answer is nearly invisible
  (1.13:1 on IBM Beige, 1.22:1 on Relay Light): the agent-turn colours are fixed 24-bit values for a
  dark ground (`src/main.cpp:5693-5704`). Shared by Relay Light, so it does not decide the light
  head-to-head, but it blocks any light theme from being the preferred one. Not fixed here.
- **Dark Copper is subtle alone.** Side by side with Relay Dark the difference is clear; alone it
  reads as Relay Dark with a warm composer, because the terminal grid is deliberately plain.

## Limits

- The Switchboard pane opens in every theme but stayed on "Loading the Switchboard…" for the 15 s
  wait in all six (a copied `issues/` tree in a sandbox without git history). The board shots show
  the pane frame and filter only.
- `stub-provider.py` answers every turn the same way, so the agent turn is identical across themes
  by design.
