---
id: SQ3D
type: work
status: needs-verification
labels: [feature, ui, terminal]
assignee: claude-code
rank: m
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [27a61344382a877c28ccc219160497925c4beec2], evidence: [docs/qa_evidence/2026-09-21-slash-command-ink/], related: [D7AV], github: null}
---
# The `/command` in an echoed prompt is inked like the one you typed

## Issue
when you send a slash command (eg /deliver), use a different color with it in the printed agent prompt.

## Plan
The composer already tints a leading `/command` with the theme's `[syntax] token` colour
(`InputHighlighter::highlightAgent`). The moment the turn starts, the pane prints the prompt back
as a banded row (`Pane::printInline`, `Ink::UserAgent`) in one flat ink, so the command stops
looking like a command. The owner's screenshot is that row.


1. **The pane inks the command.** In `printInline`'s User/UserAgent branch, the leading
   `/command` of the echoed line (after the `✦ ` mark) is written with an *indexed* SGR colour,
   not a 24-bit one: an indexed colour is resolved against the theme's own terminal palette when
   the view paints, so it follows a theme switch, which a written RGB never can (the same reason
   `Ink::Ask` is indexed bold yellow). Every echo path goes through `printInline` — the turn's own
   echo, a steer, a replayed transcript — so all of them get it from one place.
2. **The view keeps it legible on the band.** A user row wears its role's band, and the band flips
   light (Relay Dark, Gruvbox, Dark Copper) to dark (Relay Light, IBM Beige) between themes, so a
   palette colour chosen for the terminal ground can land unreadable on it. `legibleOn()` in
   `engine/view/FaintInk.h` — the mirror of `faintInk()`, same contrast machinery — moves a
   written ink toward black or white only as far as it must to clear 4.5:1 on the ground it is
   actually drawn on, keeping its hue. Both paint paths apply it on a role row: the grid rows and
   the re-wrapped prose rows (#R2WQ).

## Execution Summary
`Pane::printInline` writes the leading `/command` of a line it echoes to the agent with SGR 96 —
a palette *index*, so the theme's own palette decides the hue when the view paints and a theme
switch recolours it like the rest of the row. `agentSlashSpan()` finds that span by the composer's
own rule (`^/[A-Za-z][\w-]*` after the `✦ ` mark), and leaves a path (`/usr/bin/env`) alone.
`legibleOn()` in `engine/view/FaintInk.h` — the mirror of `faintInk()`, on the same contrast
machinery — moves an ink toward black or white only as far as it must to clear 4.5:1 on the ground
it is drawn on; both paint paths run a coloured run on a role row through it, the grid's rows
(`TerminalView.cpp:751`) and the prose rows the layer owns after a re-wrap (`:1144`).

Commit `27a6134`, landed through `scripts/land.py` with its build gate: the exact committed tree
configures and builds `relay`. The id first written on this card (`SLQ3`) is not a Crockford id —
`L` is not in the alphabet — so the card, its thread and every reference in the code were renamed
to `#SQ3D` in the follow-up commit.

## Tests
`xvfb-run ./build/engine/relay-engine-tests` — 181 passed, 0 failed (the whole engine binary).
`RELAY_ENGINE_TEST=FaintInkTest ./build/engine/relay-engine-tests` — 24 passed.
`RELAY_ENGINE_TEST=ViewTest ./build/engine/relay-engine-tests aCommandInAUserRowKeepsItsHueAndClearsItsBand`
— passed, and checked to fail with either of the two view branches disabled, so both paint paths
are really covered.
`python3 docs/qa_evidence/2026-09-21-slash-command-ink/analyse.py` — PASS: in both themes the
command is painted in exactly `legibleOn(palette[14], band)`, at 4.51:1 on the band, while the
rest of the row keeps the role's ink.

## QA checklist
- [ ] Send a prompt that opens with a slash command (`/deliver something`): the command is inked
      differently from the rest of the echoed line, and matches what the composer showed.
- [ ] Switch the theme (`/light`, `/dark`, Options › Appearance) and look at that row again: the
      band and both inks follow the theme, and the command stays readable on a light and a dark band.
- [ ] A prompt with no command, and a shell line (`! make`), look exactly as they did before.
- [ ] A prompt that opens with a path (`/usr/bin/env python`) is not inked as a command.
- [ ] Narrow the pane so the row re-wraps: the command keeps its ink on the re-wrapped row.
