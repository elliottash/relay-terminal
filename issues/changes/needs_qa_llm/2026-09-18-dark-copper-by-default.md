---
id: DKCU
type: work
status: needs-qa-llm
labels: [theme]
component: [gui, theme]
milestone: beta
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18, feature intake: "use dark copper by default on all builds"'
links: {plans: [], commits: [a6ab586], evidence: ['docs/qa_evidence/2026-09-18-dark-copper-default/'], related: [0JA7], github: null}
---
# Dark Copper is the theme every build starts on

## Issue

> use dark copper by default on all builds

## Behaviour as implemented

- **Desktop.** `relay::theme::defaultThemeId()` is `dark-copper`. A profile with no `theme/name` —
  every fresh install, whatever the package — starts on it, and nothing is written for it: the
  default stays a default, not a choice made on the user's behalf. A profile that chose a theme
  keeps it. A stale or missing theme name falls back to Dark Copper, then Relay Dark (whose values
  the compiled-in palette carries), then that compiled-in palette. Options › Appearance › Reset to
  defaults (#OPRS) goes back to Dark Copper too.
- **Browser app** (the phone and iPad view). `app/pane-theme.css` is generated with Dark Copper as
  the bare `.relay-pane` block, so the pane a browser draws matches a fresh desktop; Relay Dark and
  Relay Light remain as `data-theme` switches. The app's own shell (`app/style.css`: the header, the
  pairing and pane-list screens, the terminal grid) and the colour a phone paints its status bar
  with (`manifest.webmanifest`, `<meta name="theme-color">`) take Dark Copper's values, and three
  tints that were tuned to the old blue accent became copper-tinted surfaces.
- The site already wore Dark Copper and IBM Beige (26633c9); nothing changed there.

What the browser does not have is the desktop's *metal*: the milled gradient that makes copper read
as metal on the desktop's chrome is painted in C++ (`Theme.cpp`, "Metal (Dark Copper)"), so the web
view carries Dark Copper's colours on flat surfaces.

## Checks

- [ ] A fresh profile (no `relay.conf`) opens on Dark Copper; `relay.conf` has no `theme/name`
      until a theme is chosen.
- [ ] A profile with `theme/name=relay-light` still opens on Relay Light.
- [ ] `theme/name=solarized-dark` (removed) opens on Dark Copper with a whole palette.
- [ ] Options › Appearance › Reset to defaults lands on Dark Copper, live.
- [ ] The browser app: the pairing screen, the pane list and a pane are all charcoal and copper,
      and the phone's status bar is the same charcoal.
- [ ] `ctest` passes, including `relay-themeswitch-tests` and `backend-and-bash`
      (`tests/test_web_theme.py`).
