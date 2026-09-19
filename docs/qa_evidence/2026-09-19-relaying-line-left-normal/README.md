# #HQ2B the "Relaying…" line goes left and loses the bold — implementer evidence (2026-09-19)

Build `build/relay` (aarch64, Qt 6.4.2) with the #HQ2B changes; live GUI verification under
Xvfb with an isolated HOME / XDG_CONFIG_HOME / XDG_RUNTIME_DIR / TMPDIR, `RELAY_KEYRING=off`,
and a loopback-only stub model endpoint (no provider account). Harness copied from the
2026-09-19 relaying-status-language run (#4E13); its lessons (readiness probe, isolation off
in the sandbox) are carried over — see that run's README.

## What was asked

`issues/feature_intake.txt`, 2026-09-19: *"the \"Relaying -- [action]. . .\" should be at the
left and above the prompt box, more like how warp . claude does it. and not in bold."* The line
was already above the prompt box; what was wrong was the alignment (hard right, DemiBold).

## What was captured

`drive.sh [build-dir] [scene…]` drives six scenes; `measure.py` measures each capture and
writes `analysis.txt`:

| scene | state the line shows | colour family | busy left x | prompt text left x | Δ |
|---|---|---|---|---|---|
| idle | none — the control finds no "Relaying" row | — | — | — | pass |
| relaying | agent turn: "Relaying thinking… · 6 s · step 1/256 · Esc stops" | violet | 38 | 37 | 1 px |
| running | terminal program: "Relaying sleep…" | blue | 38 | 36 (the caret — the prompt's own pen origin) | 2 px |
| spawn | subagents: "Relaying waiting for 1 subagent…" | violet | 38 | 36 | 2 px |
| question | blocked on your answer: "Relaying waiting for your answer… · 7 s · Esc stops" | amber | 38 | 37 | 1 px |
| narrow | elision: "Relaying running sleep 120 && echo padding-… · Esc stops" in a 420 px window | violet | 38 | 37 | 1 px |

The acceptance is "same pen origin, ±2 px of glyph bearing": the busy line's capital R and the
placeholder's first glyph carry different left side bearings (≤2 px at this size), so their
leftmost *ink* pixels can differ by that much while the pen origin is shared. The origin itself
is pinned by the caret: in the captures where the prompt's blink is on (running), a 2 px-wide,
18 px-tall column — the caret, i.e. the prompt's pen origin — stands at x 36–37, one glyph
bearing left of the busy line's R at x 38. The busy row's own left edge is x = 38 in **every**
scene: the row does not move between states. The old right-aligned line drew the same text
against the right edge (x ≈ 1300–1410 in the card's reproduction).

The weight verdict (`measure.py`, scene relaying): the captured word "Relaying" has an ink
ratio of 0.205, against 0.243 for the fc-matched UI font at normal weight and 0.396 at
demibold — nearest normal by a wide margin. DemiBold is also simply gone from the painter:
`PaneBusyLine::paintEvent()` sets no font any more and draws with the widget's own.

The middle elision is intact: the narrow capture (420 px window) shows the long
"Relaying running sleep 120 && echo padding…" line elided with the head and the "Esc stops"
tail kept, and the line still starts on the prompt text's edge.

The pane header's state word (`PaneStateWord`, `src/PaneChrome.h`) keeps its DemiBold — a
different surface, per the card's decision.

## Files

- `drive.sh` — the Xvfb driver (scenes, readiness probe, crops, one retry on a flaky exec).
- `stub-provider.py` — loopback OpenAI-compatible endpoint; the first user message picks the
  turn (`slow`, `spawn`, `stuck` → an `ask_user` card never answered, `longrun` → a long
  `run_command`, `SUBTASK` for the subagent).
- `measure.py` — band finding (the busy row is the coloured band that OCRs to "Relaying";
  terminal ANSI text and strip chips also carry state colours), left-edge and caret
  measurement, the weight verdict, and the overall PASS/FAIL.
- `analysis.txt` — its output for the committed captures.
- `implementer-*.png` — full-window, composer (100 %) and composer (200 %) crops per scene.
- `logs/<scene>-{relay,worker}.log` — per-scene diagnostics.

## Tests

`ctest --test-dir build` on the tree that carried the change: 56/57 passed; `backend-and-bash`
timed out — the same environment hang the #4E13 run documented (a git-credential prompt
against the suite's local HTTPS server in a sandbox without tty or credentials), not an
assertion; retried with `GIT_TERMINAL_PROMPT=0` (result in the card's thread). The change is
GUI painting only (no protocol, no backend surface), and the closest C++ suites
(`panestate`, `pulsepaint`) passed in that run.

## Note on a shared worktree

The main worktree was being rewritten by another session's in-flight work while this card ran
(guest-model work; at one point it briefly clobbered the #HQ2B edits and left the tree not
compiling). The commit was therefore staged as `HEAD + the #HQ2B hunks only` and compiled in
an isolated `git worktree` at the pinned HEAD before committing; the captures above are from
a binary built with the change.
