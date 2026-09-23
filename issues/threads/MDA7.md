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

<!-- relay:entry 20260923T031413Z-65 author=agent kind=decision model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/23c54dfce46e4eebbcab18c059094a17 -->
Owner: "yes to all of these ideas -- tables, video, read-aloud, and latex math -- definitely, add that now. for charts, (b) for now. for sound, if qt is better let me know". So scope is: the sound player, `relay-show`, SVG/GIF/PDF/matplotlib, tables, video, read-aloud and LaTeX math. Charts use (b): a static snapshot inline, live in the browser on click. On sound, Qt is better where it is built. QtMultimedia 6.8 (the Qt of the Windows and macOS installers, docs/BUILDING.md) bundles an FFmpeg backend with real pause, seek and position on all three platforms, where the CLI tools cannot seek or pause cleanly and Windows has none. So: QtMultimedia as an optional component (like Qt Pdf), with a CLI fallback (pw-play/paplay/aplay/ffplay) for Qt5 Linux builds like this machine, which has no QtMultimedia installed. Read-aloud does the same with QtTextToSpeech (the OS's own voices), falling back to spd-say/espeak-ng/say.

<!-- relay:entry 20260923T031420Z-h5 author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/23c54dfce46e4eebbcab18c059094a17 -->
- ✦ agent updated this card · waiting_on: owner → (unset); replaced `## Decisions`; tasks: 0/8 done

<!-- relay:entry 20260923T031424Z-bg author=agent kind=event model=claude-opus-5-5 pane=3abcc127 turn=3f5446ec7b1c4be7852e572edc6d0bac/23c54dfce46e4eebbcab18c059094a17 -->
- ✦ agent moved this card · Discussing → Ready to start · Owner decided scope, charts (b) and sound; the parts that need the #1MGS layer wait for it, read-aloud does not
