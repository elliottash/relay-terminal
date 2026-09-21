---
id: SLQ3
type: work
status: executing
labels: [feature, ui, terminal]
assignee: claude-code
rank: m
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [D7AV], github: null}
---
# The `/command` in an echoed prompt is inked like the one you typed

## Issue
when you send a slash command (eg /deliver), use a different color with it in the printed agent prompt.

## Context
The composer already tints a leading `/command` with the theme's `[syntax] token` colour
(`InputHighlighter::highlightAgent`). The moment the turn starts, the pane prints the prompt back
as a banded row (`Pane::printInline`, `Ink::UserAgent`) in one flat ink, so the command stops
looking like a command. The owner's screenshot is that row.

## Plan
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

## Tests
- `ctest -R relay-engine-tests` — the view test: a coloured run inside a role row keeps a hue of
  its own, clears 4.5:1 on the band, and is recoloured by a scheme switch like the rest of the row.
- Evidence: the pane with `/deliver …` echoed, in a light-band and a dark-band theme.
