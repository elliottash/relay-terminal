# Thinking as a streaming fold — implementer evidence (issue T8CN, 2026-09-19)

`drive.sh` boots Relay under Xvfb with an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, points the pane's provider at `stub-provider.py`
(loopback, `reasoning_content` deltas over SSE), and shoots each scene. OCR notes are
in `implementer-notes.txt`; worker logs per scene under `logs-<scene>/`; each scene's
sandbox `relay.conf` as `relay-<scene>.conf`. The final run is the whole file (one
pass, after the two fixes below).

## What each shot shows

| Shot | Claim |
| --- | --- |
| `implementer-fold-stream.png` | Mid-turn: anchor `▸ ✦ thinking…` at column 0, the fold open under it with live markdown (heading, bullets, link), header on "Relaying…" |
| `implementer-fold-done.png` | Turn ended: the same row rewritten in place to `▸ ✦ thought for 6 s`, fold collapsed, the answer in bright ink above the muted fold text |
| `implementer-fold-reopened.png` | Alt+R reopens the fold: full markdown, "open in pane" link to the turn view |
| `implementer-fold-closed.png` | Alt+R again: collapsed, with the toast "Reasoning folded away · Alt+R shows it again" (bottom right — small text, see below) |
| `implementer-always-open.png` | `agent/thinking_display=always`: the fold is still open after the turn ended |
| `implementer-never-line.png` | `agent/thinking_display=never`: no fold, no anchor — the single `✦ thought for 1 s (Ctrl+click)` line (the link opens the turn pane) |
| `implementer-never-toast.png` | Alt+R in never mode answers "Thinking display is off · Options › General turns it on" |
| `implementer-migrate-never.png` + `migrated-relay.conf` | A settings file with `agent/show_thinking=false` behaves as never, and the key is migrated in place to `thinking_display=never` (`migrated-relay.conf` is the file the app rewrote) |
| `implementer-twoblocks.png` | Reasoning, a partial answer, more reasoning: two anchors, each closed with its own `✦ thought for N s` |
| `implementer-theme-<id>.png` ×5 | The open fold in dark-copper, gruvbox-dark, ibm-beige, relay-dark, relay-light: line spacing 5, terminal margin 16, muted markdown ink per theme |

Toasts are 1600 ms and small, so their text is read off the shot with a diff:
`compare -fuzz 8% -metric AE implementer-fold-done.png implementer-fold-closed.png`
then OCR the changed bottom-right crop — the never-toast the same way against
`implementer-never-line.png`.

## Two fixes this run found

1. **`stub-provider.py`** — the SSE parts were materialized with `list()` before the
   headers were written, so the deliberate per-chunk sleeps paced the setup, not the
   stream: every chunk arrived at once and the "mid-turn" shot saw nothing. Now the
   generator is iterated lazily.
2. **`backend/relay_core/provider.py`** — `_stream` closed a reasoning block when the
   answer began but never reopened its state when reasoning resumed after it, so a
   second block (GLM-style interleave) streamed `thinking_delta`s that were never
   closed by `thinking_done`: the pane's second anchor read "✦ thinking…" for the rest
   of the session. Each block now gets its own `thinking_done` (chars reset per block,
   the clock restarted at the block's first delta).
   Regression test: `tests/test_routing_thinking_skills.py::
   ThinkingStreamTests.test_reasoning_resuming_after_the_answer_gets_its_own_done`.

## Suites

`./scripts/test.sh` green (2554 tests, after re-running past a mid-edit race with the
co-session's `remote/wire.py` classification); `ctest --test-dir build` 55/55.
