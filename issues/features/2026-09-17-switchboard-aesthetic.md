---
id: 8E4Q
type: work
status: discussing
labels: [feature, design]
component: [gui]
milestone: desktop-alpha
workstream: terminal
waiting_on: owner
rank: zzy
created: '2026-09-17'
acceptance: the owner has answered the open questions below and each approved intervention is split into its own card
source: 'owner, 2026-09-17: how far should the retro telephone-operator switchboard aesthetic go inside the Relay app itself?'
links: {plans: [], commits: [4ff6114, fd4db1c], evidence: [docs/qa_evidence/2026-09-19-switchboard-materials], related: [0JA7, W5N2], github: null}
---
# How far the switchboard aesthetic goes inside the app

Proposal: [`docs/SWITCHBOARD-AESTHETIC.md`](../../docs/SWITCHBOARD-AESTHETIC.md). Design, except
for the two parts that are built: the Switchboard's typography (2026-09-17) and the board's
materials (2026-09-19, below).

## Summary

The metaphor is already structural (destinations, the Switchboard tracker, the cyan/violet caret),
and the website now states it literally with a brass-and-bakelite patch panel. The proposal draws
the line between metaphor and costume with three tests — the all-day test, the information test and
the hairline test — and concludes that materials belong in **chrome and rare moments** only: the
terminal grid, the prompt box, the syntax colours, card and thread text, and the tab bar stay plain.

It defines four materials (bakelite, brass, enamel label, cord), three new `Theme.h` tokens, a
light-theme oxidised-bronze variant, and a `board.material: false` switch that degrades every
proposed widget to hairlines — so a plain theme loses nothing but fills.

Ranked verdicts: **do** the pane-header jack strip with a busy lamp, the (currently unstyled)
Switchboard column and card typography, remote pairing as a call being patched through (`#W5N2`),
and the empty Switchboard; **maybe** the app icon, a bakelite pane-header ground, onboarding;
**no** to brass status-strip chips, an annunciator-drop notification, sound by default, and any
texture on the terminal.

**First to build:** the pane-header line indicator plus its lamp — one widget, all state already
exists, replaces a bare path label with the product's central idea.

**Refused:** an animated cord that swings between the jacks on every mode change. `refreshDestinationColor()`
re-runs on every router verdict as you type, so it would strobe, it delays the one signal that must
be instant, and it encodes nothing the colour change does not.

Two findings that stand on their own, independent of any of this:

- `#3ec5f0` on white is **2.01:1** and `#b48ef7` is **2.58:1** — the destination colours are
  unreadable in a light theme and need their own pair (`#0a6183` / `#5c3f9e` suggested). Belongs
  to `#0JA7`.
- `app/style.css` (the remote web client) uses a third palette — `#7aa2f7`, 12px radius — matching
  neither the app nor the site. Worth fixing before adding anything new.

## Decided and built: the board's materials (2026-09-19)

Owner, on review item D7 — the `[board]` table that two themes carried with a comment saying nothing
painted it: **"yeah build that out"**. Landed in `4ff6114` (the tokens) and `fd4db1c` (the
painting), evidence in `docs/qa_evidence/2026-09-19-switchboard-materials/`.

- `[board] face / metal / metal_dim` and `[flags] board_material` are first-class theme data, in
  all five shipped themes, and derived from a theme's own chrome when it names none. That answers
  **t:b4** for the board (brass is a fourth, non-semantic axis there, measured ΔE 10+ from the
  amber in every theme) and **t:g5** (a per-theme token, not a global setting: brass that works on
  charcoal is 2.35:1 on paper, so the choice belongs to the theme).
- The Switchboard pane is painted on the face; its engraved rules are the hardware, lit only under
  the pointer; the empty board is intervention 5's unpatched board, one unlit jack per section.
- Card and thread text are untouched, on `@surface` and `@text` (§2.2).

Still open, and still the owner's: the pane-header jack strip and its lamp (t:a1, t:c7 — the "build
first" item, and a different surface from this one), the icon (t:d2) and sound (t:f3).

## Tasks

Open questions for the owner; each answer becomes a decision below, and the approved ones split
into their own cards.

- [ ] Does the pane header become a jack strip at all, or does the path label stay as it is? <!-- t:a1 -->
- [x] Brass as a fourth, non-semantic colour axis — accepted, or is the app's palette already full enough? <!-- t:b4 -->
- [ ] Is a busy lamp wanted given the thinking overlay already reports elapsed seconds? <!-- t:c7 -->
- [ ] App icon: keep the chevron, or try a jack? (a 24/32/48px side-by-side would settle it) <!-- t:d2 -->
- [x] Should the Switchboard pane's typography land now, before the rest of this is decided? <!-- t:e9 -->
- [ ] Any appetite for sound at all, even opt-in and default off? <!-- t:f3 -->
- [x] Should `board.material` be a per-theme token, or one global "plain chrome" setting? <!-- t:g5 -->
