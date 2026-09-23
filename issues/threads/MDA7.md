<!-- relay:entry 20260922T140000Z-a1 author=claude-code kind=question -->
### Claude Code · 2026-09-22 14:00
Filed from the owner's message, to start once #1MGS is done. Questions:

1. **Interactive charts: how interactive, at what cost?**
   (a) Optional QtWebEngine: a real live chart or Flask page embedded inline. It is fully
   interactive, but adds Chromium (100+ MB per platform, a GPU process, memory per view), must stay
   optional, and reverses the no-WebEngine stance.
   (b) No WebEngine: the chart shows inline as a static snapshot (rendered headless, or taken from
   the library's own PNG export); clicking it opens the live HTML or localhost URL in the browser
   (or Relay's browser pane, if one exists). This works everywhere and adds nothing.
   (c) Native: draw Vega-Lite specs ourselves with QPainter (pan, zoom, hover). This is the most
   work and covers only one chart grammar.
   **Recommendation: (b) now, and (a) later as an optional build flag if (b) feels too flat.**
2. **Audio playback through CLI tools (pw-play/paplay/aplay/ffplay), like voice capture, or add
   QtMultimedia?** Recommendation: CLI tools, for the same reasons Voice gave: no dependency, no
   permission.
3. **Items 3 and 5 on the card:** do SVG/GIF/PDF/matplotlib go in with the sound work (they are
   small), and which of math, tables, video and read-aloud do you want at all?
   Recommendation: yes to 3; for 5, math and tables first.
