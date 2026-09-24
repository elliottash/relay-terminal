# #QJXD — card ids in output stay unlinked when the pane's card index is stale

Commit: `aab23d78f3ed17f8342c044e037243765037bd0e` on `main` (land.py verify build: "the exact tree builds").

Paths: `src/OutputLinks.h` (UnknownCardRefresh gate), `src/Pane.h` (`lookupOutputCard` miss path, `requestCardIndex`), `tests/outputlinks_test.cpp`.

## What was checked, verbatim

```
$ scripts/relay-build --target relay
[100%] Built target relay
relay-build: built in 82s; 16 artifact(s) stamped back to 17:52:58, the build's start

$ scripts/relay-build --target relay-outputlinks-tests
[100%] Built target relay-outputlinks-tests

$ ctest --test-dir build -R '^outputlinks$' --output-on-failure
1/1 Test #56: outputlinks ......................   Passed    0.00 sec
100% tests passed, 0 tests failed out of 1

$ ./build/relay-outputlinks-tests -functions | grep -i 'refresh\|writtenAfter'
aCardWrittenAfterTheFirstScanLinksAfterRefresh()
unknownCardRefreshIsThrottled()
```

New coverage:
- `aCardWrittenAfterTheFirstScanLinksAfterRefresh` — a miss on `#ABCD` asks for exactly one board reread; after the index learns the card, the same text scans to `relay://card/ABCD`; a second unknown id inside the window sends nothing.
- `unknownCardRefreshIsThrottled` — no reread before 5 s have passed, one at the boundary, none after, one again at 10 s.

## Manual check for the verifier

1. Run a build with the commit, open a terminal pane in a repo with a board.
2. Print a card id that does not exist yet (e.g. `echo '#ZZZZ'`), then create card `#ZZZZ` on disk (or via another pane that writes files, not board tools).
3. Move the mouse over the id or let the view rescan: within ~5 s it underlines and opens the card. An id that is still not a card stays plain text.
