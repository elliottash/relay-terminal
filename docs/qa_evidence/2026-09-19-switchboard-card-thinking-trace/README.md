# The Switchboard card shows the turn's thinking trace in its thread (#9K5H)

Implementer evidence, driven live under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`. No provider account: the profile points a local model endpoint
at `stub-provider.py` on 127.0.0.1.

## What the change is

A Discuss or Plan turn on a card now streams its reasoning **into the card's thread** — the same
surface the answer and any mid-turn question land in — instead of the bare italic "thinking…"
placeholder the card had before. The block carries the terminal fold's own words (`✦ thinking…`
while it streams, `✦ thought for N s` when it ends), renders the tail of the trace muted and
italic, and is **sealed in place above any thread entry that lands under it**, so a question the
agent asks mid-turn reads after the thinking it came from (the owner's words: *"the thinking
should be in the thread in the card, so that the agent can ask you questions"*). The trace is a
live view only: it is never written to the card file, it disappears when the card is left, and it
is handed back by the view while the turn runs (protocol 19.16's per-card turns).

The worker already tagged `thinking_delta` / `thinking_done` with `card_id` (protocol 19.4,
`CARD_TAGGED` in `backend/relay_core/board_turns.py`); the gap was entirely in the GUI, which
routed only `delta`, `status`, `tool_started` and `tool_result`.

## Run it

```sh
docs/qa_evidence/2026-09-19-switchboard-card-thinking-trace/drive.sh [build-dir]
```

Needs Xvfb, xdotool, ImageMagick (tesseract only for reading the shots back). The sandbox
project has a board with one card, `#TRC1`, whose issue is the card's own words. The stub's
Discuss turn thinks ~5 s, posts a `question` comment with `board_comment`, thinks again and
answers; its Plan turn `board_read`s the card, thinks ~5 s, writes `## Plan` with the hash it
was told, and answers.

## The shots

| Shot | What it shows |
|---|---|
| `implementer-discuss-stream.png` | mid-block: the trace's tail under `✦ thinking…` in the thread, the strip naming the mode, before any answer |
| `implementer-question.png` | the mid-turn question entry landed; the first block is sealed **above** it, the second streams under it |
| `implementer-done.png` | the turn ended: sealed blocks and the answer in arrival order, the strip gone |
| `implementer-plan-stream.png` | a Plan turn's trace streaming while it plans — the card's core ask |
| `implementer-plan-written.png` | `## Plan` written on the card, the trace sealed above the closing answer |
| `card-after.md` / `thread-after.md` | the card and thread as the run left them: a `## Plan` section, the question and answers in the thread, and **no** trace text in either file |

## What was verified by machine

- `tests/boardmodel_test.cpp::theThinkingTraceRunsInTheCardsThread` — the trace streams, settles
  to `✦ thought for 4 s`, seals above a mid-turn `question` entry (index-ordered), survives
  leaving and reopening the card while the turn runs, and stays after `done` with the answer
  after it. Run: `QT_QPA_PLATFORM=offscreen ./build/relay-board-tests theThinkingTraceRunsInTheCardsThread`.
- The card file and thread file after the run contain no reasoning text (`card-after.md`,
  `thread-after.md`): the trace never reaches disk.

## Not verified

A real provider's reasoning stream (the stub sends `reasoning_content` deltas, the Kimi/GLM
spelling); a reasoning block longer than the 4,000-character rendered tail (the cut is named in
the view, the whole block is held up to 200,000 characters); two turns racing on one card
(refused by the worker before the view is involved, protocol 19.16).
