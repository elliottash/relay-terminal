---
id: J5DN
type: work
status: needs-qa-llm
labels: [bug, engine]
component: [engine]
workstream: terminal
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzu
created: '2026-09-19'
acceptance: a pane on GhosttyCore shows the same band behind a line the user typed as a libvterm pane, built and seen on a machine where that core compiles
source: 'noted while landing #bef461d, 2026-09-19'
links: {plans: [], commits: [bef461d], evidence: [docs/qa_evidence/2026-09-19-ghostty-row-role/], related: [K3RT], github: null}
---
# GhosttyCore does not parse the row-role OSC, so the band is missing under it

## Issue

`bef461d` marks a line the user typed with the private `OSC 7772;shell|agent` and keeps it in the
line's `relay_marks`, which `TerminalView` paints as a band. Only the vendored libvterm fork
implements it: `LibVtermCore` lets 7772 through its OSC allowlist and calls
`vterm_state_relay_mark_cursor_line()`, and the fork widened `relay_marks` from 4 bits to 8
(`vterm.h`, `state.c`). `GhosttyCore` has the same `Line::marks` field and the same OSC 133
handling, but nothing for 7772.

Under Ghostty the line is bold in the default foreground and unbanded. Nothing else breaks: an
unknown OSC is ignored.

## Why it is not done

GhosttyCore does not compile on this machine (`RELAY_ENGINE_WITH_GHOSTTY=OFF`; see the owner's
memory note "Engine cores on this machine"), so the change could not be built or seen. It needs a
session on a machine where that core builds.

## Approach

Mirror `LibVtermCore`: accept OSC 7772, map `shell` / `agent` to `MarkUserShell` / `MarkUserAgent`,
OR the bit into the cursor line's marks, and make sure all 8 bits survive the trip to history and
back (the libvterm side needed a mask widened in two places). `CoreTest::theRowRoleOscMarksItsLine`
is core-agnostic and will cover it once the core is in the test's core list.

## QA checklist
Needs a machine where the Ghostty core builds (sphinxpad: `~/vt-prefix` has libghostty-vt
f9a3f24 + Zig 0.16.0; `~/relay-terminal/build-ghostty` is configured with the core on).

1. **Core tests.** `ctest --test-dir build-ghostty -R '^relay-engine-tests$'` passes with both
   cores in `availableVtCores()`; `CoreTest::theRowRoleOscMarksItsLine(ghostty)` passes including
   the history round-trip and the scrolled-viewport rows, and no `promptMark` event fires for a
   role (`osc133PromptMarks` unchanged).
2. **Painted band, core parity.** Run `docs/qa_evidence/2026-09-19-ghostty-row-role/README.md`'s
   reproduce steps for both cores: one violet band row (agent) and one cyan (shell) on the dark
   scheme, plain rows untouched, and the same rows recoloured after the switch to the light pair —
   ghostty's PNGs byte-identical to libvterm's.
3. **In the app, on a ghostty pane.** Send a message (a `User` line) and a `! command`: each row
   the user typed sits on the destination-coloured band, bold, in the band's ink; scroll it into
   history and the band is still there; `/theme` to a light theme recolours it in place; a reply
   from the program under it is unbanded. OSC 133 prompt rows still sit on `promptBand` when shell
   integration is on.
4. **Nothing else moved.** Unknown OSC 7772 bodies (`;nonsense`) mark nothing and emit nothing;
   selection, hyperlink runs and `scrollToPrompt` behave as before on both cores.
