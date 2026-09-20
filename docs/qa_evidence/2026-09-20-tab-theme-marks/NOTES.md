# Tab theme marks + persistent Randomize — implementer evidence, 2026-09-20

Two requests, one evidence run (the second is what makes the first visible):

1. Card #R4ND, owner: "i meant a persistent mode. it randomizes on each new tab." — a
   `theme/randomize_new_tab` switch in Options › Appearance, next to the cycling one. (The
   turn-2 summary in the conversation claimed this landed as `6083b4cd`; no such commit exists —
   this run is the mode's first real landing.)
2. Owner: "can we color the other inactive tabs with their respective themes (the clickable tab
   headers)" — src/ThemeTabBar.h: an inactive tab is washed in its own theme's accent with a 2px
   strip along its bottom edge.

`drive.sh` runs the build under Xvfb with an isolated profile (`theme/randomize_new_tab=true`
before first launch) and writes the screenshots here. Re-run: `drive.sh [build-dir]`.

What the run shows:

- **a → b:** one tab on the default (Dark Copper), then three Ctrl+T's. `tab-row-pixels.txt`
  samples the tab row of b: four tabs, four different grounds — the front tab's selected surface
  and, behind it, one wash per inactive tab, each a 16% blend of that tab's accent over the bar
  ground (the arithmetic checks out against the themes' accent tokens; every tab owns a theme
  because `addTab` pins the default when nothing chose one). Two runs of the script drew
  different theme sets, so the draw is genuinely random.
- **c:** two Ctrl+Shift+Tab's bring tab 2 to the front. The window rethemes to it (light ground
  in, dark ground out across the two runs), tab 2's mark is gone — the window around it is
  already its theme — and tabs 1, 3, 4 keep their own washes, now blended over the new ground.
- **d:** Options › Appearance searched for "random": the one-shot Randomize button and, below
  the cycling row, the new persistent "Start each new tab on a random theme" row, on.
- **e + themes.txt:** the cycling row switched on (searched "next theme" so only it matches);
  `relay.conf` afterwards holds `new_tab_new_theme=true` and `randomize_new_tab=false` — the
  rows switch each other off. (An earlier draft of this script searched "new tab", which matches
  both rows, and toggled the wrong one; the conf evidence caught it.)

Unit side (`ctest -R themeswitch`, 17 pass): `theNewTabDrawAvoidsTheDefaultAndThePreviousTab`
(300 draws never touch the avoided pair, every other theme comes up, and the empty-avoid
fallbacks) and `anInactiveTabWearsItsOwnTheme` (renders a WindowTabWidget offscreen: the themed
inactive tab's body and bottom strip read its accent against a plain inactive neighbour, and the
mark is gone when the tab comes to the front).

Not verified here: hover over a marked tab (the wash is translucent, so the hover ground shows
through — checked only in the unit test's plain-vs-themed comparison), and a profile with
user-installed themes.
