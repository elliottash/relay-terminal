# Website update for the new engine and features (#K13B)

Implementer evidence for the rewrite of `site/` — copy and screenshots for the current build.
Not a QA verdict. The site was **not deployed**; `./deploy.sh` is the owner's step.

## How the screenshots were taken

    docs/qa_evidence/2026-09-17-website-update/drive.sh [build-dir]

`drive.sh` starts this tree's `build/relay` under Xvfb on a display it picks from `/tmp/.X11-unix`
(and refuses one it did not create), drives it with `xdotool`, and captures with ImageMagick
`import`. It needs no window manager.

Nothing personal can reach a frame:

- `HOME=/tmp/relay-demo` (fixed, refused if it already exists — `mktemp -d` would put a random
  `/tmp/tmp.XXXXXXXX` into the conversations window, which prints the workspace path in full),
  isolated `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME`, `RELAY_KEYRING=off`.
- `PS1='\w $ '`, so the shell prompt is `~/project $` with no user and no host.
- The demo project is three files (`Makefile`, `greet.c` with a missing semicolon, `README.md`).
- `stub-provider.py` serves `/v1/chat/completions` on 127.0.0.1 so the agent turn in the hero shot
  is a real one — real worker, real `run_command`, real `write_file` diff — with no network, no
  account and no key. The key typed into the BYOK dialog is the literal string
  `loopback-stub-not-a-key`; the stub ignores the Authorization header.

Two things that cost time and are worth remembering:

- **Capture the right window.** Relay owns four X windows; `xdotool search --name Relay` also
  matches an 8x19 stub window, and capturing that one gives an all-black PNG. The script takes the
  largest window. Once the id is right, the frameless window captures fine without a WM — the
  black frame is a wrong-window symptom, not a missing-compositor one.
- **Keep new panes off the agent.** A new pane has no provider of its own, so any line the router
  cannot resolve locally (`./greet` before `make` has built it) goes to the agent and opens the
  BYOK dialog over the next four shots. `windows.log` makes that obvious.

## Files

| File | What |
|---|---|
| `drive.sh` | the harness |
| `stub-provider.py` | loopback OpenAI-compatible endpoint (make fails → write the fix → answer) |
| `shot-01-agent-inline.png` | hero: `make` fails, the request routes to the agent, tool call, diff, answer |
| `shot-02-splits.png` | three terminal panes, each with its own shell and prompt |
| `shot-03-file-panes.png` | the same plus a rendered Markdown preview pane |
| `shot-04-palette.png` | the actions palette sidebar |
| `shot-05-conversations.png` | conversation + terminal-history search, filtered on "greet" |
| `windows.log` | every mapped X window before each shot (a stray dialog is visible here) |
| `relay-stderr.log` | empty on the final run |

Used on the page: `shot-01` → `site/assets/agent-inline.png`, `shot-03` → `splits.png`,
`shot-04` → `palette.png`, `shot-05` → `conversations.png`. The old `file-panes.png` was dropped
because the preview pane is in `splits.png` now.

## Checks run

- Headless Chrome at 360px and 1280px: no horizontal page scroll; `pre` blocks scroll inside their
  cards; the feature grid reflows to one column.
- Every `href` resolves (four in-page anchors that exist, four GitHub URLs, stylesheet, icon) and
  every `src` exists in `site/assets/`.
- `grep -i konsole site/index.html`: every hit is the optional engine, the keymap preset or the
  "not a Konsole fork" line.
- `python3 scripts/relay-board.py check`: 86 cards, 0 errors, 0 warnings.
