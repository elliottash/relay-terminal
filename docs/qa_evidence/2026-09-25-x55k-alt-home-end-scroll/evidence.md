# X55K — Alt+Home / Alt+End console scroll jumps

Commit: 3a7cb28ff5a57419cd59c58ef7f980dbbd6b9200

## Build
```
$ RELAY_SESSION=scrlhome scripts/relay-build
[100%] Built target relay
relay-build: built in 87s
```

## Keymap test (binding, defaults, acts-inside-programs policy)
```
$ ctest --test-dir build -R "^keymap$"
1/1 Test #16: keymap ........................... Passed 0.11s
$ ./build/relay-keymap-tests
PASS   : KeymapTests::altHomeEndScrollActsInsidePrograms()
PASS   : KeymapTests::stopKeyActsInsidePrograms()
PASS   : KeymapTests::programKeysGivePlainAltArrowsToTheProgram()
Totals: ... (0 failed)
```

## Engine suite (scrollToTop/scrollToBottom view primitives)
```
$ ./build/engine/relay-engine-tests
Totals: 77 passed, 0 failed, 0 skipped
```

## Manual check
The new `terminal.scrollTop` / `terminal.scrollBottom` actions appear in Settings > Shortcuts (terminal group), default Alt+Home / Alt+End; pressing them moves the focused console view to the scrollback top / newest output, including while a program runs and while the composer has focus.
