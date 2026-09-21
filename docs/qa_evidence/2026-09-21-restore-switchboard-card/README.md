# Switchboard navigation restoration — R8PK

`ctest --test-dir build -R '^boardpane$' --output-on-failure`: passed.
The regression covers saved open-card/filter state, delayed board loading, closing the card before a reload, and a card missing from the reloaded board.

The same test run through `TestsCommands.run_and_wait(['ctest:boardpane'])` passed; `check_card('R8PK')` returned no findings, failures or blocks.

`python3 scripts/relay-board.py check`: no findings for R8PK; two pre-existing errors concern the non-Crockford id MDL1 and its thread filename.

Full `scripts/relay-build --target relay` passed. The land.py build of the exact committed tree also passed (d4ac69d7872aabf184794faa4cc8366f22f3bbe9).

Live check: isolated XDG config/data/runtime/cache, Xvfb :169. Seeded a saved Switchboard pane with R8PK open, launched Relay, verified the card title on screen with OCR, quit with SIGTERM, and asserted the saved navigation still names R8PK. Relaunched and repeated both checks: two passes. `reopened.png` shows the second launch.
