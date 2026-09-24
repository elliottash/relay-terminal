---
id: K7VJ
type: work
status: needs-qa-llm
labels: [change, bug, theme]
component: [theme]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: On both light themes the terminal and agent colours are told apart from each other and from ordinary text at a glance; every shipped theme still clears 4.5:1 on background, surface, surface_raised and every moulded face; relay-theme-tests and relay-panestatus-tests pass and app/pane-theme.css is regenerated
source: 'owner, 2026-09-18: "in the light theme, the terminal and agent colors (cyan and violet) are too dark and desaturated" / "i cant really tell them apart from each other or from regular dark text" / "no, i meant the ibm beige theme" / "the ibm beige terminal and agents are still a little hard to pick out for me"'
links: {plans: [], commits: [cae5879, 879518d, 278822], evidence: [docs/qa_evidence/2026-09-18-options-and-actions-side-by-side/], related: [0JA7], github: null}
---
# The light themes' terminal and agent colours are colours, not greys

## Issue

Owner, 2026-09-18: "in the light theme, the terminal and agent colors (cyan and violet) are too
dark and desaturated" — "i cant really tell them apart from each other or from regular dark
text" — "no, i meant the ibm beige theme" — and on the first correction, "the ibm beige terminal
and agents are still a little hard to pick out for me".

## Report

Measured rather than argued. CIELAB chroma of the destination pair, and its distance from the
text it was being confused with:

| | chroma | dE from `text` | dE from `text_muted` | dE pair |
|---|---|---|---|---|
| IBM Beige, before | 13.3 / 22.5 | 27 / 33 | 21 / 29 | 20.3 |
| Relay Light, before | 29.3 / 82.2 | 42 / 83 | — | 75 |
| IBM Beige, now | 60.1 / 99.8 | 68 / 106 | 68 / 106 | 51.6 |
| Relay Light, now | 44.8 / 102.3 | 51 / 103 | — | 72 |

`text_muted` on IBM Beige is chroma 8.9, so a "slate" at 13.3 was, to the eye, the grey it sat
next to. The pair was also exactly at the dE 20 floor the theme's own test enforces.

The physical constraint is real and worth recording. Each theme's darkest ground fixes the
lightness of anything painted on it at 4.5:1 — L 43.5 on Relay Light's chip face, L 33 on IBM
Beige's case — and at a fixed lightness the only free variable is chroma, which has a ceiling per
hue. A teal-cyan is the worst case: it cannot pass chroma 35 at either lightness, which is why
both themes' first cut read as slate. Turning the hue a few degrees bluer is what buys the
chroma, and it is what both fixes did.

## Change

**Relay Light** (cae5879, restored in 0278822 after an unrelated commit reverted it): shell and
accent `#097193` → `#006ab1`, agent `#6c3fc9` → `#7c3aed`, matched at L 43.5 / 43.4. The
composer's `command`/`token` and `variable`/`agent` syntax, `selection`, `accent_hover` and the
terminal cursor follow the pair, so the theme has one cyan rather than two.

**IBM Beige** (879518d, then the second pass): the greyed pair the owner first asked for —
"terminal is dark gray-blue and agent is dark gray-violet" — is withdrawn by the same owner.
`#324d5c` / `#4f4163` → `#005380` / `#7220ad` → `#0049a9` / `#7500c3`. The second pass is the
ceiling: both sit at 4.6:1 on the case beige and one step more chroma at either hue fails AA. The
blue lands on HSV 209, which is where a 1990s machine's own blue was (EGA `#0000aa`), and it stays
dE 21 from the Windows 95 navy that remains the theme's chrome.

The shell blue is dE 12 from the terminal's own ANSI 4. They are cousins on purpose: one is chip
chrome and one is grid content, and they are never read against each other.

If the pair still has to work harder, the next lever is a tinted chip behind it (`@shellSoft` and
`@agentSoft` already exist in `src/Theme.cpp`), not more chroma — there is none left.

## Evidence

- `relay-theme-tests` (28) — the contrast contract for every shipped theme on background,
  surface, surface_raised, and for IBM Beige on all four moulded plastic faces besides; the
  destination pair's dE 20 floor and "shell is the cooler of the two".
- `relay-panestatus-tests` (18) — lit-button legibility per theme.
- `tests/test_web_theme.py` — `app/pane-theme.css` regenerated from the theme files in the same
  commit (Relay Light only: the web pane ships Dark and Light).
- `docs/qa_evidence/2026-09-18-options-and-actions-side-by-side/implementer-beige-1-shell.png`
  and `-2-agent.png` — the composer on the beige ground, the command word in the new blue and the
  caret in the new purple, beside ordinary text.

## QA checklist

- [ ] On IBM Beige, a shell command in the composer and an agent prompt are two obviously
      different colours, and neither is mistaken for the muted grey beside it.
- [ ] The same on Relay Light.
- [ ] The Windows 95 navy (selection, primary buttons) is still clearly not the terminal blue.
- [ ] Both light themes' terminals are unchanged: the ANSI ramp still inverts, `\e[37m` is dark.
- [ ] Nothing else moved: success, warning, error and the Actions red-orange are as they were.
- [ ] A theme with no `shell`/`agent` of its own still inherits sensibly (Gruvbox, Dark Copper).
