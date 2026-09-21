# Switchboard navigation restoration — R8PK

`ctest --test-dir build -R '^boardpane$' --output-on-failure`: passed.
The regression covers saved open-card/filter state, delayed board loading, closing the card before a reload, and a card missing from the reloaded board.

The same test run through `TestsCommands.run_and_wait(['ctest:boardpane'])` passed; `check_card('R8PK')` returned no findings, failures or blocks.

`python3 scripts/relay-board.py check`: no findings for R8PK; two pre-existing errors concern the non-Crockford id MDL1 and its thread filename.
