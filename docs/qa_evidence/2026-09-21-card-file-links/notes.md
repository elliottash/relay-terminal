# #K9KC implementer evidence

The saved reply in session df8af0c4932e4c1a8120358358571b40 uses an absolute Markdown target for #GWXM, not a relay://card target. Card-file activation now reads bounded front matter within the attached board and dispatches the card before the file handler. This does not require a warm card index. Ordinary documents, thread files, foreign board paths, missing files, malformed front matter and numeric-only IDs are excluded. Explicit line navigation remains a file action.

A second defect was reproduced in ViewTest: a known plain #ID returned an empty target after narrowing the pane. Explicit Markdown labels worked. Fold rows previously skipped the plain-text scanner because frameRowOf returned -1. The new scan maps UTF-16 offsets to graphemes and wrapped screen rows.

- Before fix: markdownLinkLabelsAreClickable failed with an empty target for the known card after rewrapping.
- After fix: `RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests markdownLinkLabelsAreClickable` passed, including a real mouse click on plain #GWXM after resizing and unknown #ZZZZ remaining inert.
- `ctest --test-dir build -R '^(boardworkspace|outputlinks)$' --output-on-failure`: 2/2 passed.
- `scripts/relay-build --target relay relay-engine-tests relay-boardworkspace-tests`: passed.

GUI verification uses the existing markdown-link-labels drive with CARDROW targeting the generated card's absolute Markdown path. Isolated Xvfb, HOME/XDG directories and loopback stub provider; no user profile or external provider requests. The first attempt exposed an outdated fixture default selecting the installed Claude harness; it was stopped and the private fixture was corrected to explicitly rank the stub model first.

GUI result: **3 PASS, 0 FAIL**. The CARDROW label linked to the generated card's absolute `.md` path; clicking it opened Switchboard on the matching title and ID. See `card-opened.png` and `gui-check.txt`.

The ViewTest resize-and-click regression also passed under Xvfb with `QT_QPA_PLATFORM=xcb` and isolated `XDG_CONFIG_HOME`; see `resize-click-test.txt`. The final committed tree passed land.py's full Relay build gate. Implementation commit: `1f71d6050a27b8b2045448e29b3dd18997f8b5bd`.

`TestsCommands('.').check_card('K9KC')` returned no findings, actions, failing tests or blocking signals. The board format check reported no findings for this card; existing warnings elsewhere were left unchanged.
