# Icon ideas from the theme work (owner ask, 2026-09-18)

> "for the icon, give me some more ideas based on our more recent work on the color themes and metaphors"

Open `sheet.html` in a browser (or `sheet.png`) for all eight in both themes at 120, 32 and 16 px.
Each candidate is one SVG that takes its colours from CSS variables, so the same file renders in
Dark Copper or IBM Beige — which is the point: the icon should be able to wear the theme the app
is wearing, the way the site now does.

| | idea | what it says | 16 px |
|---|---|---|---|
| A | `a-patch` | the routing board from the site's hero: a line in, two jacks, one lit | a squiggle |
| B | `b-plug` | the operator's ¼" plug over its jack | fails |
| C | `c-stamp` | today's chevron struck into a metal plate instead of glowing | fails: a groove has no colour |
| D | `d-crt` | the machine itself — moulded bezel, warm screen, a chevron and a caret | **reads** |
| E | `e-field` | a jack field with one cord patched across it | reads as a board |
| F | `f-rocker` | the destination as a rocker switch, one half pressed | busy |
| G | `g-cord` | the chevron *is* a patch cord, ending in a plug tip the colour of the destination | **reads** |
| H | `h-two` | one line, two ends: shell filled, agent open | reads |

The 16 px column is the real test — a taskbar, a tab, a favicon. Anything with a plug body, a
groove or a two-tone bevel loses at that size, which knocks out B, C and F however good they look
at 120.

Colours used: Dark Copper `[board] face/metal/metal_dim` and `[material]`, IBM Beige the same plus
`[bevel]`. Nothing here is committed to the app; `data/icons/` is untouched.
