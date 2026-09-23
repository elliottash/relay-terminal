# Model availability and priority investigation — MPA2

Reviewed fixes already present in main: `3cb7ff33` and `8d03da03`.
The relevant model source/test files had no uncommitted edits during verification.

Two reproduced defects were fixed by that work:

- Providers and Priorities could read different pane catalogs, with Priorities missing late
  helper updates. The served pane and helper fallback now agree on initial open and refresh.
- A finite Codex catalog with seven untiered models was classified as open-ended solely
  because of its size. Guest catalogs are exempt from that heuristic.

Fresh verification in this turn:

- `scripts/relay-build --target relay-modelcatalog-tests relay-modelpicker-tests relay-modelspane-tests`
- `ctest --test-dir build -R '^(modelcatalog|modelpicker|modelspane)$' --output-on-failure`: all three targets pass.
- OpenRouter backend tests: 13 pass. Guest-harness provider tests: 69 pass.
- Reran `docs/qa_evidence/2026-09-22-VPR7/stage.py`, directing output to
  `/tmp/relay-mpa2-stage/` to preserve the original evidence. The actual running app receives
  synthetic worker events, refreshes an already-open OpenRouter search without losing it,
  and shows all seven Codex models in Available. Passed.

The model tests use temporary QSettings and exercise unchecking/rechecking models, adding
OpenRouter models, adding Codex to Main/High, deleting ranks, reordering, and Ctrl+Z undo.
They preserve the intentional distinction between Available and assigned priority lists;
unranked models are added through search, and guest agents remain in Main/High.

No further application change was necessary. Sphinxpad SSH returned No route to host;
the new fixes were not deployed there in this turn. The earlier installed package
`fff7eb8f` predates these fixes. Test logs are alongside this file.
