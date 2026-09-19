# Per-tab themes (2026-09-19)

Owner: "add an option, on by default, that themes are tab specific … the theme that you have in the
options menu is the default for when relay opens and new tabs … you can see the theme visually in
the tab picker at the top … add a /theme command".

One Relay under Xvfb, the scripted provider from `../2026-09-19-echo-band/`, default theme Dark
Copper:

1. `1-tab1-beige.png` — `/theme beige` in the first tab.
2. `2-tab3-dark-copper-three-swatches.png` — a second tab (`/theme gruvbox` there) and a third, which
   starts on the default. Three swatches on the tab bar: beige over navy, Gruvbox's ground over its
   accent, charcoal over copper.
3. `3-back-to-tab2-gruvbox.png` — Ctrl+Shift+Tab: the window restyles to Gruvbox.
4. `4-back-to-tab1-beige.png` — and again to IBM Beige.

Design and rules: `docs/ARCHITECTURE.md` § 14, "A tab owns its theme".
