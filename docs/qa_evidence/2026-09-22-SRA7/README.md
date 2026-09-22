# #SRA7 implementer evidence

Root cause: libvterm reset its scroll offset to zero in resize(); the view retained a numeric visual top across folded/prose layout changes, so the content moved underneath it.

The core now maps the first cell of the top history row through reflow and clamps if history is trimmed. Ghostty uses its tracked grid reference for the same anchor. The view keeps a fold URI plus logical line/cell, or the core-mapped real row. Live-bottom following remains intact.

## Verification

- `scripts/relay-build --target relay-engine-tests` passed.
- Focused CoreTest: 4 cases passed with libvterm (height/width, continuation rows, trimming and bottom follow; existing cursor/reflow cases).
- Focused ViewTest: 7 cases passed under Xvfb with isolated XDG_CONFIG_HOME (plain history, insertion folds, prose print-width transitions, existing fold/search/prose regressions).
- Exact commands and test output: [tests.txt](tests.txt).
- Staged widget screenshots: [before](fold-before.png), [30 columns](fold-width-30.png), [80 columns](fold-width-80.png), [50 columns](fold-width-50.png). Inspected the 30-column capture: `detail 12` remains at the top and wraps below it.
- Ghostty source passed syntax-only compilation against the locally available pinned API. Exact-tree commit verification is configured to build both the app and engine tests with Ghostty enabled and run the same focused cases on both cores before landing.
- Repository board check reports existing unrelated errors/warnings; no #SRA7 card/thread findings. Board/tests MCP tools are unavailable; evidence is recorded via the file fallback.

These are implementer checks, not independent QA or a sphinxpad retest.
