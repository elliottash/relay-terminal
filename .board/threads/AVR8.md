<!-- relay:entry 20260923T024155Z-1d author=agent kind=progress model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
Claimed through POLICY file fallback. Traced availability checkboxes and Priorities rendering; previous provider-refresh fixes do not change these semantics. Checking intended membership behavior before implementation.

<!-- relay:entry 20260923T024302Z-yr author=agent kind=question model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
1. Should checking Available add the model to Priorities automatically and unchecking remove it, or should rankings stay saved and only the selectable candidates refresh? Recommendation: show enabled models immediately as unranked candidates, preserve existing order/effort, and make disabling explicit. Current behavior deliberately pins ranked models available and hides unranked models until search; this is a behavior choice, not simply a missing repaint.

<!-- relay:entry 20260923T024316Z-88 author=agent kind=event model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; appended to `## Planning notes`

<!-- relay:entry 20260923T024316Z-v6 author=agent kind=event model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
- ✦ agent moved this card · Executing → Discussing · Awaiting intended membership behavior: automatic rank addition/removal versus refreshing available candidates while preserving saved rankings.

<!-- relay:entry 20260923T024328Z-bg author=agent kind=evidence model=gpt-6-astra pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/3ffc4a43d1ee40adaa447ad5c96b02d8 -->
Baseline rebuilt via scripts/relay-build. xvfb-run -a ctest --test-dir build -R '^(modelcatalog|modelpicker|modelspane)$' --output-on-failure: 3/3 passed. Existing tests deliberately enforce ranked-model availability pins. No production code changed. Board check reports pre-existing errors/warnings elsewhere; no AVR8 finding.

<!-- relay:entry 20260923T024955Z-x7 author=agent kind=evidence model=claude-opus-5-5 pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/7bf6f0e142694a0182b29425bee9748e -->
Landed 5f0972ad (code+tests) and 907357d5 (docs). Owner did not pick between the two options; implemented the recommended one (keep rankings): the Available tick now governs ranked models (the terminal-list pin in `curation::isAvailable` is gone). Un-ticked ranked models keep their stored rank, are drawn greyed with "not available" in place on Priorities, and `curation::activeTierList` — now what `Pane::tiersObject` (worker tiers), `liveTier` (defaults/fallbacks) and `ModelRows` (the box) read — skips them. Lite is never filtered. Checking an unranked model does not auto-rank it; it is offered under "not in <class> — ctrl+enter adds" when typed, as before. Regression: tests/modelpicker_test.cpp `unTickingARankedModelGreysItOnPrioritiesAndKeepsItsRank` (untick on Available → setTier(classes) → row rank 2 greyed "not available" → re-tick → active again); modelcatalog tests rewritten for the new rule. `xvfb-run -a ctest -R '^(modelcatalog|modelpicker|modelspane|modelrows)$'`: 4/4 passed. land.py build gate built the exact tree (relay-modelpicker-tests). Full relay binary not rebuilt or driven live yet.

<!-- relay:entry 20260923T024956Z-29 author=agent kind=event model=claude-opus-5-5 pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/7bf6f0e142694a0182b29425bee9748e -->
- ✦ agent moved this card · Discussing → Needs verification · Available now governs ranked models; Priorities greys un-ticked ones at their rank; landed 5f0972ad + 907357d5 with targeted tests passing. · implemented_by anthropic/claude-opus-5-5 via claude-code

<!-- relay:entry 20260923T030852Z-gw author=agent kind=decision model=claude-opus-5-5 pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/84cd65e339354a2286d9015ca55a8099 -->
Owner, 2026-09-22: "i thought all available models would be in priority, so searching should happen in the available pane" and "the text box is hard to find, make it show up in the provider sections, as a +add model". Also: "it doesnt carsh, it just freezes it with the model searches".

<!-- relay:entry 20260923T030856Z-3s author=agent kind=evidence model=claude-opus-5-5 pane=540a6092 turn=19bd73c570494541b8433b0c939c9daf/84cd65e339354a2286d9015ca55a8099 -->
Landed 046e3cb9 + cf5c7b53. Priorities now draws, under each class's ranked rows, every available model that class may hold and does not rank ("not in <class> — + add puts it here"), untyped too; typing filters only that page and never the whole catalog. Available ends each API/local provider section with "+ add model" (opens the id prompt on that provider; none for guest harnesses or Relay Free). The freeze: every row re-read QSettings for ticks/lists/favorites/speeds, and Priorities searched OpenRouter's tail once per class section per keystroke; `curation::ReadScope` now memoises reads for one redraw. Measured offscreen with a 600-model OpenRouter catalog, per keystroke: priorities 390 → 5 ms (1,806 → 6–12 rows), available 300 → 70 ms. Tests: modelcatalog, modelpicker (51), modelspane (22), modelrows all pass; tests rewritten where they encoded the old typed-tail behaviour (`aTierTabsFilterStaysInTheAvailablePoolAndATickedTailModelJoinsIt`, `addModelEndsEveryProviderSectionOfTheAllTab`, `lateProviderCatalogBecomesSearchableInAvailableAndJoinsPriorities`). Full `scripts/relay-build` of relay: exit 0. Not yet driven live in a real window.

<!-- relay:entry 20260925T115216Z-qn author=owner kind=comment mode=discuss -->
This might be different be, try to reproduce

<!-- relay:entry 20260925T115340Z-6z author=agent kind=event model=glm-5.3 pane=switchboard turn=129600ce8932a0ad49bc7f119c0f5468/ff2c9d24dfa845dc971a0cd7abfcfccd -->
- ✦ agent updated this card · waiting_on: owner → (unset)

<!-- relay:entry 20260925T115340Z-cr author=agent kind=note model=glm-5.3 pane=switchboard turn=129600ce8932a0ad49bc7f119c0f5468/ff2c9d24dfa845dc971a0cd7abfcfccd -->
Owner suspects what they saw may be a different fault and asked for a live reproduction. The landed fix is in the tree on main (commits 5f0972ad, 907357d5, 046e3cb9, cf5c7b53), so reproduce on a fresh build, not whatever binary was running: build via scripts/relay-build, open the Models pane, tick/untick a ranked and an unranked model in Available, switch to Priorities and check greyed rows/ranks/refresh, and also try searching (the freeze path). If the card's scenario does not reproduce there, what the owner hit is a different fault — file it as a new card in bugs with the measured evidence rather than reopening this one blind.

<!-- relay:entry 20260925T115349Z-52 author=agent kind=comment mode=discuss model=glm-5.3 turn=129600ce8932a0ad49bc7f119c0f5468/ff2c9d24dfa845dc971a0cd7abfcfccd -->
The fix is in the tree on main (`activeTierList`, the un-pinned availability tick, commits `5f0972ad`–`cf5c7b53`), so a reproduction has to drive a freshly built binary — which I can't do from Discuss. Recording the ask on the card and clearing the stale `waiting_on`:

**Need:** a live run to reproduce — Discuss can't launch anything. Press **Run** on #AVR8 to hand it to a terminal pane; the note I just added tells it exactly what to do: fresh `scripts/relay-build` (the fix is on main, `5f0972ad`–`cf5c7b53` — if your running binary predates 2026-09-23 you'd still see the old behaviour), then tick/untick in Available → check Priorities, plus the search path for the freeze. If it doesn't reproduce there, it's a new card in bugs with the evidence, not a blind reopen of this one.

Card changes: cleared the stale `waiting_on: owner` and recorded the reproduction ask on #AVR8.
<!-- relay:entry 20260925T224713Z-oj author=agent kind=note -->
Verification attempt 2026-09-25 (rev 2db96643): BOUNCED — no landed work exists (no commits/tests/evidence; planning notes say "no production code changed yet" and a membership-vs-visibility clarification is still open). Moved needs-verification → discussing so it can be answered and implemented. Evidence: the card's own body; see also docs/qa_evidence/2026-09-25-verify-RND7/NOTES.md for the suite state of neighboring models cards.

