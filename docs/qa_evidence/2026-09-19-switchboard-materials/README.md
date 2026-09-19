# The Switchboard's materials, live (card `#8E4Q`, owner decision D7, 2026-09-19)

Owner, 2026-09-19, on the dead `[board]` table in the two new themes: **"yeah build that out"**.
This is the implementer's evidence for the two commits that did:

| commit | what |
|---|---|
| `4ff6114` | `[board]` becomes theme data every theme carries: `face`, `metal`, `metal_dim` as first-class tokens, derived from a theme's own chrome when it names none, measured in `tests/theme_test.cpp` |
| `fd4db1c` | the Switchboard is painted with them (`src/Theme.cpp` stylesheet, `src/BoardPane.cpp` delegate and empty board), with the live-switch and plumbing tests |

Design: `docs/SWITCHBOARD-AESTHETIC.md` §3.1–3.4 and interventions 3 and 5. Token rules and the
measurements below: `docs/THEMES.md` §6.

## What was driven

`qa-drive.sh` (kept here as run; `BUILD=<build dir> OUT=<dir> THEMES="<ids>" ./qa-drive.sh`). It is
the standard Xvfb harness: its own `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR` under `/tmp/claude-1000/v-board-x`, `RELAY_KEYRING=off`, display
`:87` at 1400x900, and `relay --clean-shell --fresh --workspace <fixture>`. The build is a clean
`git archive` of the landed tree, never the shared `build/` (`qa-live-log.txt` has the binary's
sha256).

Two fixtures, both real git repos with a `switchboard/board.yaml`: one with seven cards, one per
section (each in the folder its status belongs in, so the board has no problems banner), and one
with a board and no cards at all.

Per theme: the list of rows, the pointer resting on a section header, a card open, and the empty
board. The terminal pane is in every shot on purpose — the point of the face is that the board is a
different object from the window it is in, and a screenshot of the board alone cannot show that.

## What the shots show

| file | what to look at |
|---|---|
| `*-01-board.png` | the board pane's ground is the theme's `board.face`, not `@bg`: a warm panel beside the cool terminal in Dark Copper and Relay Dark, a phenolic cream beside the paper in IBM Beige and Relay Light. The rules over the section names, and the two rules that frame the list, are unlit brass |
| `*-02-section-hover.png` | the pointer on `DISCUSSING`: that section's rule is lit brass and every other rule stays unlit — exactly one lit rule in the pane. (Qt's tooltip for the header is in the frame; it is the existing "click to fold" tip, not part of this change) |
| `*-03-card.png` | a card open. The pane is still the face; the card's document and its reply box are `@surface` with `@text` on them. No material goes behind anything a person reads (§2.2) |
| `*-04-empty-board.png` | intervention 5, built: one unlit jack per section — brass ring, collar shade, a small dark hole — over its engraved name, then "No cards yet." Six of the seven sections fit at this width; the columns that do not fit are left off rather than elided to stubs |

Four themes: `dark-copper` (the default, and the one whose chrome is the same metal as its board),
`ibm-beige` (the light candidate, oxidised bronze), `relay-light` (a cool light theme with a warm
board — the case the `[board]` fallback had to get right) and `relay-dark` (the proposal's own
bakelite and brass, `#17140f` / `#c8a45c` / `#6b5637`).

### Reading them, and what changed because of that

- The first cut of the jack was a ring with the whole disc filled as the socket. On Dark Copper it
  read as a jack; on Relay Light and IBM Beige the fill and the pale bronze ring were within a few
  percent of each other and the jacks read as **grey bullets**. The hole is now a small disc mixed
  out of whichever of the face and the metal is already the darker, so the ring survives on a light
  ground. That is the only change the screenshots forced.
- The lit rule is a genuine step brighter than the unlit ones at every size tested, and it is one at
  a time: no theme ends up with a row of bright brass lines.
- Nothing warm reaches the terminal grid or the composer in any shot (§2.2), and the amber flag on
  `#K5EE` ("Amber still means one thing") is still the only amber in the pane.

## The measurements

Reproduce with the same arithmetic the tests use (`tests/theme_test.cpp`, `contrast()` /
`deltaE()`), or with `docs/qa_evidence/2026-09-18-copper-and-beige-themes/contrast.py`.

| theme | `board.face` | `board.metal` on face | `board.metal_dim` on face | tightest ui token on the face | metal vs `warning` |
|---|---|---|---|---|---|
| Dark Copper | `#1a1210` | `#c08556` **5.91** | `#6b4a33` 2.33 | 5.38 (accent) | dE 23.7 |
| IBM Beige | `#e0d6bd` | `#63492b` **5.77** | `#8a7550` 3.06 | 5.71 (agent) | dE 20.1 |
| Relay Dark | `#17140f` | `#c8a45c` **7.80** | `#6b5637` 2.63 | 5.66 (link) | dE 10.6 |
| Relay Light | `#f0ece3` | `#5a3f1a` **8.24** | `#a5967e` 2.45 | **4.80** (success) | dE 28.4 |
| Gruvbox Dark | `#1d2021` | `#d79921` **6.61** | `#7a5c21` 2.64 | 6.49 (action) | dE 15.4 |

What each column is held to, and where:

- **metal on face ≥ 4.5:1** — the metal has to be legible *as text*, which is what makes an enamel
  label honest rather than a texture (§3.2). `everyShippedThemeWearsTheBoardMaterials`.
- **the tightest ui token on the face ≥ 4.5:1** — the face is a ground a person reads on: a row's
  title, its id, its badges and its status mark are painted straight onto it. The board face is now
  a fourth ground in `everyShippedThemeKeepsItsTextLegible`, beside `background`, `surface` and
  `surface_raised`. The worst pair in any shipped theme is Relay Light's success green at 4.80:1.
- **metal_dim ≥ 1.4:1 on the face and under ¾ of the lit metal's contrast** — unlit hardware has to
  be visible and can never be mistaken for something lit.
- **metal dE ≥ 10 from `warning`** — brass is structure; amber means one thing, and it is not this.

A theme that names no `[board]` table gets all three derived from its own chrome, and the derivation
is measured against all five shipped palettes with their `[board]` tables cut out
(`everyShippedThemeCouldDeriveItsBoardMaterials`), plus a user theme in
`aThemeThatNamesNoBoardMaterialsDerivesThem`.

## Checklist for QA

- [ ] Open the Switchboard in each shipped theme: the pane's ground is the board face, not the
      window's, and the card document inside an open card is still `@surface`.
- [ ] Rest the pointer on a section header: that rule lights and no other does.
- [ ] Open a project whose board has no cards: the jack row reads as rings with holes, unlit, in
      both a dark and a light theme.
- [ ] Switch theme with the Switchboard open (Options › Appearance, or `/theme`): the face, the
      rules and the jacks all follow without a restart.
- [ ] Put `board_material = false` in a copy of a theme under `~/.config/relay/themes/`: the board
      falls back to `surface` with hairline `border` rules, nothing moves and nothing resizes.
