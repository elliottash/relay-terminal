# Blank lines between transcript blocks — implementer evidence (#5AWD, 2026-09-19)

`drive.sh` boots Relay under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`
and `TMPDIR`, points the pane's provider at `stub-provider.py` (loopback, OpenAI-compatible SSE:
prose in three `content` deltas, then indexed `tool_calls`), types three prompts and a `/recap`
into one session and shoots after each. OCR of the pane body is in `implementer-notes.txt`; the
worker log under `logs/`; the app's stderr as `relay.log`.

The rule (`src/TranscriptGaps.h`, tested by `tests/transcriptgaps_test.cpp`): one blank line
before a block whose kind differs from the block before it — the user's ✦ line, the `▸ model`
header, the agent's prose, ▸ tool rows — never before the first block, never between two blocks of
the same kind (a run of tool rows stays single-spaced), and never right after the header, which
sits on top of whatever follows it. Notes, errors, diffs and tool output carry no kind and stay
attached to the block above them.

## Second pass (same day, owner QA report)

The owner's screenshot of a real turn showed no gaps at all — and every boundary in it was one the
first pass had left attached *by design*: the `✦ N tool calls · …` turn link (printed with
`Ink::Note`, no kind) and the `Recap · …` block (`Ink::Recap`, unmapped). The landed rule was live
in their GUI (binary built after the fix landed); it simply had nothing to fire on there. The
owner's words: "in between agent messages, user messages, or tool calls, there should be a blank
line". So: the turn link now begins a `Call` block (`Pane::printTurnLink`), and `Ink::Recap`
begins a `Recap` block (its own kind: the span header, the summary and the `Next ·` / `Open ·`
lines are one block). `drive.sh` grew a fourth scene: `/recap` after a turn.

| Shot | Claim |
| --- | --- |
| `implementer-01-walk.png` | One turn: `✦ walk through the files` · blank · `▸ stub` directly on top of the first prose line · blank · `▸ read 2 files` and `▸ ran ls -la` single-spaced · blank · prose · blank · `▸ edited alpha.py` with its inline diff attached · blank · the final prose |
| `implementer-02-second.png` | A second prompt after the turn closed: `✦ second prompt, no tools` is set off from the previous turn's last line by a blank row (the shell prompt row in between was erased, as before) |
| `implementer-03-tools.png` | A turn whose first step is a tool call: `▸ stub` sits directly on top of `▸ ran echo …` (no gap after the header), then a blank line before the closing prose, and — new — a blank line between that prose and `✦ 1 tool call · 1 s  (Ctrl+click)` |
| `implementer-04-recap.png` | After the turn: a blank line between the `✦ 1 tool call` link and `Recap · 18:45 → 18:46 · <1m` (the recap is its own block) |

The OCR in `implementer-notes.txt` shows both new gaps as empty rows (03: between the `Done: …`
prose and the `✦ 1 tool call` link; 04: between the link and `Recap ·`). OCR cannot be trusted for
single vs double spacing of styled rows, so the unchanged gaps rest on the first pass's byte trace
(below); the rule itself — including the `Recap` kind and `gapBefore(Call, Recap)` — is under
ctest. Not in a shot: a turn that *ends* on a tool row puts the link directly on the run it
sums up (`gapBefore(Call, Call)` is false), which the unit test covers.

## What the bytes said (first pass, unchanged by the second)

A run with a temporary display trace (not landed) showed the exact sequence for the first turn:
the ✦ line, `\r\n` (the gap), `▸ stub`, the prose deltas, `\r\n` (gap), `▸ reading alpha.py…`
rewritten **in place** (`\r\x1b[2K`) to `▸ read alpha.py` and then to `▸ read 2 files`, `\r\n`,
`▸ running ls -la…` rewritten to `▸ ran ls -la`, `\r\n` (gap), prose, `\r\n` (gap), the edit row and
its diff, `\r\n` (gap), the final prose, the turn link. The in-place rewrite matters: the first
version of this change printed the gap from inside `drawCallRow()`, after `LineCursor::start()`,
which told the cursor "something else printed" and left a stale `▸ reading alpha.py…` row above
every merged result. The gap now prints before `start()`/`result()` are asked.

## Suites

`ctest --test-dir build`: all green including the new `transcriptgaps` test (see the card thread
for the count at landing time). `./scripts/test.sh` is unaffected (no backend change).
