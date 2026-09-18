---
id: K13B
type: work
status: needs-qa-llm
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: Claude Opus 5 (1M context), 2026-09-17
rank: zzk1
created: '2026-09-17'
acceptance: the live site describes the current build with fresh screenshots that show no personal paths
source: '`issues/feature_intake.txt`, 2026-09-17: "update the web site with the new engine / features"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Update the website for the new engine and features

relay-terminal.ai still describes the Konsole-based build. Update the copy and screenshots for: Relay's own
terminal engine, the prompt-box-only input model, tasks, conversation search, the Switchboard, model roles
and tiers, session restore. Keep the install section in step with the beta build (#P4GP).

## Implemented

`site/` only (hand-written `index.html` + `style.css` + `assets/`, no build step). **Not deployed** —
`./deploy.sh` is the owner's step.

- **Copy rewritten for the current build.** The page no longer sells "Relay embeds Konsole's real
  terminal". It leads with Relay's own engine (libghostty-vt or a patched libvterm behind one emulator
  interface, chosen per pane, default), with KonsolePart named only as the `--engine=konsole` option, and
  covers shell integration (OSC 7/133), the prompt-box-only input model and Take control (Ctrl+H), masked
  password prompts, routing (`!` / `*` / Ctrl+I, caret and syntax colouring), tasks
  (`Tasks 3/5 (1 deferred, 1 failed)`), conversation and terminal-history search (Ctrl+Shift+O, Ctrl+F),
  Main/Flash/Lite model tiers with the API-keys window and the keyring, session restore, the Switchboard
  (described as in progress: format, threads and the `relay-board` checker exist, the pane does not),
  voice transcription, panes/tabs/palette/keymap presets/Ctrl+?, and the unchanged privacy position
  (no telemetry, no accounts, BYOK, GPL-3.0-or-later).
- `<title>`, meta description and the OG title/description updated with it.
- **Install section** kept honest: no packaged build exists, so it says so and points at building from
  source. Qt6+KF6 or Qt5+KF5 auto-detect, CMake 3.22+, Python 3.10+, Bash; `konsole-kpart` listed as
  needed only for `--engine=konsole`; syntax highlighting and PDF preview as optional; the engine-core
  note (libvterm needs nothing, libghostty-vt needs Zig). The "beta build coming" line is what #P4GP
  replaces with a download.
- **Screenshots replaced** with fresh captures from this build (see below):
  `assets/agent-inline.png` (hero), `assets/splits.png`, `assets/palette.png` and a new
  `assets/conversations.png`. `assets/file-panes.png` is gone — the file preview is in `splits.png` now.

### Screenshot harness

`docs/qa_evidence/2026-09-17-website-update/drive.sh` (+ `stub-provider.py`) drives the real binary under
Xvfb and writes `shot-*.png` beside itself. Three things it had to get right:

- **The window id.** Relay owns four X windows and `xdotool search --name Relay` also matches an 8x19
  stub; capturing that one is what produces an all-black PNG. The script captures the largest window.
  With the right id, a frameless window captures fine and no window manager is needed.
- **No identity in the frame.** A fixed sandbox `HOME=/tmp/relay-demo` (not `mktemp -d`, whose random
  name would show in the conversations window), isolated XDG dirs, `RELAY_KEYRING=off`, `PS1='\w $ '`,
  and a demo project at `~/project`. No username, hostname, personal path or key appears.
- **A real agent turn, no account.** `stub-provider.py` answers `/v1/chat/completions` on 127.0.0.1:
  turn 1 runs `make` (which really fails in the demo project), turn 2 writes the fixed `greet.c` (a real
  diff), turn 3 answers. Real worker, real tools, no network and no key.

## Implementer evidence (not a QA verdict)

- `docs/qa_evidence/2026-09-17-website-update/`: `drive.sh`, `stub-provider.py`, the five captures
  (`shot-01-agent-inline`, `shot-02-splits`, `shot-03-file-panes`, `shot-04-palette`,
  `shot-05-conversations`), `windows.log` and `relay-stderr.log` from the final run.
- Built from this tree (`cmake --build build`, Qt5/KF5) and driven on a free display picked from
  `/tmp/.X11-unix`; the four shots used on the page come from that one run.
- Rendered with headless Chrome at 360px and 1280px: no horizontal overflow, the `pre` blocks scroll
  inside their cards, the grid reflows to one column.
- Every `href` in the page resolves (four anchors that exist, four GitHub URLs, the stylesheet and the
  icon); every `src` exists in `site/assets/`.
- `grep -i konsole site/index.html`: every hit is Konsole as an option or as a keymap preset.
- `python3 scripts/relay-board.py check`: clean.

## QA checklist

1. **No stale engine claim.** Read the page start to finish. Nothing says Relay embeds, wraps or depends
   on Konsole's terminal for normal use; every Konsole mention is the optional engine, the keymap preset,
   or the "not a fork" line.
2. **Copy against the build.** For each claim on the page, check the app or the docs: `--engine=relay` is
   the default (`src/TerminalBackends.cpp`), Ctrl+I cycles auto → terminal → agent, Ctrl+H takes control,
   Ctrl+Shift+O searches conversations *and* terminal history, Ctrl+F is find-in-pane, Ctrl+? lists every
   shortcut, Right Alt is the voice key, and the eight named providers are the ones in
   `backend/relay_core/presets.py`.
3. **Switchboard honesty.** The page must not promise a Switchboard pane inside Relay. Confirm the wording
   matches what shipped in #23XM (format, threads, checker; no UI).
4. **Install section.** On a clean Ubuntu 24.04 and a Qt6 machine, the two `apt` lines plus
   `./scripts/build.sh` produce a working `./build/relay`. Remove `konsole-kpart` and confirm Relay still
   starts (its own engine) and that only `--engine=konsole` fails. Confirm no download link promises a
   file that does not exist.
5. **Screenshots.** Open each of the four PNGs at full size: no username, hostname, home directory, real
   project path, API key or key fragment anywhere, including window titles, cwd chips and the path line in
   the conversations window. They show the current UI (own frame with the tab row title bar, no permanent
   status bar).
6. **Re-run the harness.** `docs/qa_evidence/2026-09-17-website-update/drive.sh` on a machine with Xvfb,
   xdotool and ImageMagick produces five non-black PNGs and leaves no `/tmp/relay-demo` behind. It must
   refuse a display it did not create and refuse to run if `/tmp/relay-demo` already exists.
7. **Rendering.** Load `site/index.html` in a browser at 360px, 720px and 1280px: no horizontal page
   scroll, the nav stays usable, the gallery reflows, images are not stretched (the intrinsic sizes match
   the `width`/`height` attributes: 1320x860, 1320x860, 1320x860 shown at 660, 1091x640 shown at 660x387).
8. **Links.** Every link on the page resolves, including the four GitHub URLs.
9. **Not deployed.** `git status` shows changes only under `site/`, `docs/qa_evidence/2026-09-17-website-update/`
   and this card. `deploy.sh` is untouched, and the live site still shows the old page until the owner runs it.
