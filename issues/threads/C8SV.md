<!-- relay:entry 20260923T165340Z-c8 author=codex kind=note -->
### Codex · 2026-09-23 16:53
Filed the measured crash separately from #N6R8's session recovery defect. The adjacent Globals event is temporal evidence, not a proven cause; the available frames do not support a code fix yet.

<!-- relay:entry 20260923T172051Z-c9 author=codex kind=evidence -->
### Codex · 2026-09-23 17:20
The owner said, "the crash happened when i clicked a link to 'keep' a memory". The 16:44:18.931 `globals_suggestion_accepted` event confirms backend acceptance reached the pane just before the 16:44:18.932 SIGSEGV. Inspected transcript link dispatch and reply handling; no failing application frame survives. Backend acceptance tests (21) and `globalspane` pass; `consolemode` fails earlier at an unrelated queued-prompt assertion. Reproduction under `scripts/relay-debug` is the next useful step before changing the GUI handler.
