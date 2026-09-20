# Options › About (#8WQK)

Driven under Xvfb against a fresh profile, build `2026-09-20.09H.04`.

- `implementer-01-palette-about.png` — the palette (Ctrl+Shift+A) with "about" typed. The first hit
  is **About Relay**, and its detail line already carries the number: "Relay · Version 0.1.0 ·
  build 2026-09-20.09H.04 · licence · what it is built on". This is the discoverable path: the
  Options search skips Info rows on purpose (`src/SettingsPane.cpp:704` — a search hit there is
  something you press), and an About page is all Info rows, so the palette is what finds it.
- `implementer-02-about-page.png` — Enter on that hit opens Options on the About tab, last in the
  strip: what Relay is and its licence, the build line (build id, version, running since, the
  binary's path), the parts line (engine, Qt, distribution, architecture), and Copy.

Caught and fixed while driving it: the engine read blank, because `relay::defaultEngineCore()` is
empty unless `--engine-core` or `RELAY_ENGINE_CORE` named one — it is the *core*, not the engine.
The row now says "Relay engine", or "Relay engine (ghostty)" when a core was chosen, which is the
wording the pane header already uses (`src/Pane.h:2231`).

Not automated: the settings pane has no test harness that builds a window, so this page is proven
by the screenshots. `Copy` is the one behaviour a QA pass should press — the clipboard should hold
the same six lines, and the notice should name the version and build.
