# #SRA7 implementer evidence

Root cause: libvterm reset its scroll offset to zero in resize(); the view retained a numeric visual top across folded/prose layout changes, so the content moved underneath it.

The core now maps the first cell of the top history row through reflow and clamps if history is trimmed. Ghostty uses its tracked grid reference for the same anchor. The view keeps a fold URI plus logical line/cell, or the core-mapped real row. Live-bottom following remains intact.

## Verification

- `scripts/relay-build --target relay-engine-tests` passed.
- Focused CoreTest: 5 cases passed with libvterm (height/width, continuation rows, blank wrapped rows, trimming and bottom follow; existing cursor/reflow cases).
- Focused ViewTest: 7 cases passed under Xvfb with isolated XDG_CONFIG_HOME (plain history, insertion folds, prose print-width transitions, existing fold/search/prose regressions).
- Exact commands and test output: [tests.txt](tests.txt).
- Staged widget screenshots: [before](fold-before.png), [30 columns](fold-width-30.png), [80 columns](fold-width-80.png), [50 columns](fold-width-50.png). Inspected the 30-column capture: `detail 12` remains at the top and wraps below it.
- The exact-tree landing gate built the app and engine tests with Ghostty enabled and passed 4 core and 7 view cases on each core. Output is in [ghostty-tests.txt](ghostty-tests.txt). The follow-up also tests blank wrapped rows.
- Repository board check reports existing unrelated errors/warnings; no #SRA7 card/thread findings. Board/tests MCP tools are unavailable; evidence is recorded via the file fallback. The local TestsCommands.check_card equivalent is used for the card check.

These are implementer checks, not independent QA or a sphinxpad retest.

Implementation: `a7fd665d48d598aabb056e1dc869a9af7b8cd5f2`. A final-review follow-up maps empty source rows while processing each row, so an all-space first wrapped row is not incorrectly mapped to the end of the logical line.
