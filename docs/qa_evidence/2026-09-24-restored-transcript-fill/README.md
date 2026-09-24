# QA evidence — restored transcript cut off (#KDB4)

Commits: `4d6c210c` (fix), `dff42e06` (backend test).

## The reported fault, measured

Conversation `5fa1d5a6…` ("Verify image generation implementation", pane `69c2c9d3`),
8→10 turns over 13:01→17:50 on 2026-09-24. Its saved terminal text
(`sessions/83e6ce670835f99d/5fa1d5a6….scrollback.txt`) is a 5,000-line newest-kept
window; over the day it was observed starting mid-word at

```
the implementer. Next is the desktop again / st a loc / al gateway
```

(the tail of turn 7's last reply) and later, as the window kept sliding, at the
turn-9 recap — with no ✦ prompt row left in the window at all. Turns 1–6 were gone
from every restore.

## Probe (coveredFrom on the live data)

Reimplementation of `relay::transcriptreplay::coveredFrom` against the real file and
the real `conversation_get` items, 2026-09-24 18:15:

```
precise: None | reply-dated newest turn: 9
coveredFrom = 9 -> fill draws turns 0..8
```

The window is dated by its replies (whitespace-free containment); the fill now
covers everything above it. Earlier the same day, when the window still held a ✦
row, the precise path answered turn 8.

## Tests

```
$ ./build/relay-transcriptreplay-tests
Totals: 13 passed, 0 failed, 0 skipped, 0 blacklisted
  (new: beforeTurnDropsThatTurnAndLater, coveredFromMatchesTheFirstPromptRow,
   repliesDateTheWindowWhenNoPromptRowSurvives, aRepeatedPromptAnswersItsLastTurn)

$ PYTHONPATH=backend python3 -m unittest \
    tests.test_conv_index.IndexTests.test_conversation_limit_keeps_the_newest_entries \
    tests.test_conv_index.IndexTests.test_conversation_preview_highlights_and_filters_by_turn
OK   (the preview test still passes with the newest-kept ordering)

$ scripts/relay-build --target relay    → Built target relay
```

`land.py`'s verify gate built the exact tree of `4d6c210c` before it landed.
