# #4E13 "Relaying…" status language — implementer evidence (2026-09-19)

Branch `feature/guest-agents`, build `build/relay` (aarch64, Qt 5). Live GUI verification
under Xvfb (`:16x`, 1440x900) with an isolated HOME / XDG_CONFIG_HOME / XDG_RUNTIME_DIR /
TMPDIR, `RELAY_KEYRING=off`, and a loopback-only stub model endpoint (no provider account).

## What was captured

`drive.sh [build-dir] [scene…]` drives six scenes; the live ones are shot as six frames
~0.7 s apart and cropped into tab bar (`-bar`), pane header row (`-head`), and composer
(`-comp`, the bottom 240 px at 200 %). Raw numbers are in `implementer-notes.txt`
(pairwise frame-difference ranges) and `analysis.txt` (OCR + colour-family counts +
per-word label verdicts); run `python3 analyze.py` and `python3 labels_words.py` to
regenerate the latter two.

- `idle` — fresh pane: no busy line above the prompt (no blue/violet family pixels in the
  composer crop), ring glyph in the header.
- `running` — `sleep 600`: bold blue **"Relaying sleep…"** right-aligned above the prompt
  box (blue family 1196 px at the busy row, x≈1300-1410 of 1440), header shows the state
  word "Command running" beside a blue relay mark (head blue 2580 px). Tab bar and header
  both blink: frames differ in 0..548 px and 0..296 px (identical pairs = same blink
  phase; the mark is the only mover — the #V8KT six-frame method, see `drive.sh`).
- `relaying` — a 60 s stub agent turn: bold violet **"Relaying thinking… · 5 s · … · Esc
  stops"** above the prompt (violet family 2752 px, right-aligned), header "Relaying…"
  with the violet relay mark, blink ranges as above. The composer's frame diffs (320 px)
  are the seconds counter and the prompt placeholder's animated dots, not a state change.
- `spawn` — the turn ends leaving one subagent running: bold violet **"Relaying waiting
  for 1 subagent…"**, header "Subagents working (+1)", subagent rows in the status strip.
- `labels` / `labels-beige` — a stub reply carrying the labelled bolds, in relay-dark and
  ibm-beige. `labels_words.py` reads each word's own pixels: `Done:` green, `Need:`
  amber, `Problem:` red, plain `**bold**` uncoloured — in both themes, against each
  theme's own palette (so the colours resolve at paint time; nothing is burnt into the
  scrollback).

Cursor-flash-time-0 / reduce-motion behaviour of the blink is covered by unit tests
(`tests/panestatus_test.cpp`, `tests/pulsepaint_test.cpp`), not by these captures.

## Two harness lessons from this run (why `drive.sh` is not the #V8KT copy it started as)

1. **The pane's router must be ready before a scene submits.** The first attempt used a
   fixed `sleep 7`; every submit was refused with "Local router is not ready" (the text
   stays in the box — `src/Pane.h`, `requestRoute`), so all captures showed idle panes
   with a banner. `drive.sh` now probes end-to-end: submit `echo relayqaready`, OCR-poll
   the terminal area for the echo, retry up to ~80 s, then `clear` and run the scene.
2. **Per-pane isolation (#Y4RX) cannot start the worker inside this sandbox.** Isolation
   defaults on; its probe (`systemd-run --user --scope true`) succeeds because it
   inherits the desktop session's `DBUS_SESSION_BUS_ADDRESS`, but the real spawn strips
   that variable and the sandbox `XDG_RUNTIME_DIR` has no bus socket, so `systemd-run
   --user` exits 1 within ~50 ms (`relay.log`: `worker_exit code=1`; the worker's own
   stderr is discarded by design — `repro_worker.sh` reproduces the sandbox spawn with
   stderr visible and shows the plain `python3` worker is healthy). The sandbox
   `relay.conf` therefore sets `isolation/enabled=false`; these scenes do not test
   isolation. Per-scene `relay.log`/`worker.log` are kept in `logs/`.

## Files

- `drive.sh` — the Xvfb driver (scenes, readiness probe, crops, frame-diff ranges).
- `stub-provider.py` — loopback OpenAI-compatible endpoint deciding the turn from the
  first user message (copied from the #V8KT run, plus the `labels` reply).
- `analyze.py`, `labels_words.py`, `labels_bold_probe.py`, `inspect_region.py` — OCR and
  colour-family analysis; `labels_bold_probe.py` documents why the uncoloured check uses
  core-colour distance (subpixel fringes land near the pale label colours on every word).
- `repro_worker.sh` — reproduces the sandbox worker spawn with stderr visible.
- `implementer-*.png` — the captures; `implementer-notes.txt`, `analysis.txt` — numbers.
- `logs/<scene>-{relay,worker}.log` — per-scene diagnostics.

## Test suites

C++ `ctest --test-dir build`: 52/52 that ran passed, including the updated
`markdownansi`, `panestatus`, and `pulsepaint` tests. `backend-and-bash` was interrupted
at the owner's instruction after it hung on an interactive git-credential prompt against
its local HTTPS test server (environment, not an assertion), and `./scripts/test.sh`
(the same Python suite) was skipped at the owner's instruction for the same reason.
