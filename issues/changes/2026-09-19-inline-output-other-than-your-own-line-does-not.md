---
id: K3RT
type: work
status: ready
labels: [bug, theme]
component: [theme, terminal]
workstream: terminal
assignee: agent
rank: zzzzzzs
created: '2026-09-19'
acceptance: after a theme switch, the tool lines, notes, recaps, question cards and diff lines already in a pane's scrollback are drawn in the new theme's colours, as the user's own lines already are
source: 'found while landing #bef461d (the band that follows the theme), 2026-09-19'
links: {plans: [], commits: [bef461d], evidence: [docs/qa_evidence/2026-09-19-echo-band/], related: [], github: null}
---
# Inline output other than your own line still keeps the colours it was printed in

## Issue

A colour written into the terminal is frozen: the emulator cannot recolour its scrollback. `bef461d`
solved that for the one case the owner noticed — the line you typed — by giving the row a *role*
(`OSC 7772`, `MarkUserShell` / `MarkUserAgent`) and letting `TerminalView::paintRow` resolve the
band and the ink from the scheme in force at paint time.

Everything else `Pane::printInline` writes is still 24-bit RGB from the theme that was active when
it printed: `Ink::Tool`, `Ink::ToolOutput`, `Ink::Note`, `Ink::Recap` (all `TextMuted`),
`Ink::Agent` (`Text`), `Ink::DiffAdd` / `Ink::DiffRemove`, and the fold rows' palette. After a
theme switch those lines keep the old theme's colours — visible in
`docs/qa_evidence/2026-09-19-echo-band/follows-theme-2-after-light.png`, where the muted tool lines
fade almost into the beige ground.

Per-tab themes (`9a418c7`) make this easier to hit: a tab's theme now changes under output that is
already on screen, and `/theme` invites exactly that.

## Approach

Extend the role marks rather than inventing a second mechanism. `relay_marks` has 8 bits and two
are spent; the OSC 133 bits take four. Either add a role per ink family (muted chrome, agent prose,
diff add, diff remove) or widen the field — `VTermLineInfo::relay_marks` is a bitfield in the
vendored fork and `LibVtermCore` masks it in two places. Diff lines carry a background as well, so
they need the same whole-row treatment the band already does. `Ink::Ask` is already indexed and
follows the palette; it can stay as it is.

Watch: a role is per *row*, so an ink that changes mid-row (a tool line whose stats are dim) needs
either a second role or to keep its written colour. `MarkdownAnsi` is indexed throughout and is not
affected.
