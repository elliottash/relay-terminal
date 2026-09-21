# #S53Z implementer evidence

Labels render as bare words. Row badges and meta links copy `label:<name>` and show that exact term in the notice and toast. Body and thread `#word` tokens remain plain unless they name a card. Card-reference clicks, row/header copy buttons, and the `y` shortcut retain their existing behavior.

The old copy helper also served card IDs, so the implementation separates `copyCardReference` from `copyTag`; simply changing the old helper would have broken ID copies. Existing badge and filter-chip text was already bare.

Validation:

- `scripts/relay-build` passed; final code rebuilt with `scripts/relay-build --target relay relay-board-tests`.
- `ctest --test-dir build -R '^board$'`: final run through `TestsCommands.run_and_wait(['ctest:board'])` records the result in Switchboard test history; see `ctest.log`. The plan's `boardmodel` name matches no registered test; `board` is the actual suite.
- Real BoardView widgets under Xvfb/xcb, with a fresh `XDG_CONFIG_HOME`: `build/relay-board-tests labelClicksCopyFiltersAndCardRefsZoom theRefCopyButtonCopiesTheIdInTheRowsAndTheHeader`. See `xvfb.log` and `labels.png` (visually inspected). The drive clicks the visible meta label, row badge, and document card reference; checks clipboard, notice/toast, and filtering; and preserves explicit Markdown links and fenced code. It also checks the row/header ID-copy buttons.
- The first regression run failed because the new assertion incorrectly looked for the meta anchor inside the separate body document. Corrected the test to inspect and click the actual QLabel; the final runs pass.

Screenshot reproduction: set `RELAY_LABEL_TEST_SCREENSHOT` to an absolute PNG path for the focused Xvfb invocation above. This is a BoardView widget integration drive with fixture worker events, not a provider-backed end-to-end session.

Switchboard validation passed. `tests_check` returned only a timing notice (p95 2.58 s); no missing or failing tests and no new signals.
