# #7BYT — Alt+click adds output material to the agent context

Landed: `1cc5b1d2772a` (2026-09-25), verified in land.py's build slot on the exact tree
(tip + this change only; `BoardPane.h` at its committed state).

## Automated

```
$ scripts/relay-build --target relay-backends-tests --target relay-engine-tests
$ ./build/relay-backends-tests
********* Finished testing of BackendsTests *********
Totals: 16 passed, 0 failed, 0 skipped, 0 blacklisted

$ ctest --test-dir build -R relay-engine-tests
100% tests passed, 0 tests failed out of 1        # all classes, libvterm + relay core
```

- `BackendsTests::clickActionFollowsTheModifiers` — Alt → AddToPrompt, Ctrl → Navigate,
  Shift → External, plain → Open; Alt beats Ctrl beats Shift.
- `BackendsTests::folderClickMenuNamesBothChoicesAndTheirChords`,
  `BackendsTests::fileClickMenuNamesTheChords` — `Add to prompt  Alt+click` entries,
  `Ctrl+click` navigate labels, greyed without a prompt target / shell.
- `ViewTest::altDragHandsTheSelectionToTheHost` — Alt+drag emits `selectionActivated("EXAMPLE")`;
  Ctrl+Alt+drag selects the same text without emitting.
- `ViewTest::altClickCarriesItsModifier` — Alt+click still follows a link with its modifier.

## By hand (Try it)

See the card's `## Try it`. The cross-pane target order and the shell→agent mode flip need a
live window; there is no unit harness for a real `RelayWindow`.
