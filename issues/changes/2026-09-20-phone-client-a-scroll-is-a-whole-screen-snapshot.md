---
id: 3H5T
type: work
status: executing
labels: [bug, performance, remote]
assignee: claude-code
rank: ma
created: '2026-09-20'
source: 'Claude Code (#PF4K orchestrator), measured on the owner''s Pixel 8 over a LAN share, 2026-09-20'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md], related: [PF4K, PPR4, GMCF], github: null}
---
# Phone client: a scroll is sent as a whole-screen snapshot, tool output is pushed to a phone that discards it, a history_get storm, fit() recalcs per frame

## Issue
do you want dev access to my google pixel for testing

## Findings
Measured on the owner's Pixel 8 (Android 17, Chrome via CDP) on a LAN share from a clean tip build under Xvfb with a stub provider; detail, harness and traces in `docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md`.

| | 20k-token prose reply | tool-heavy turn (20 × 2,000 lines) | 50 MB through the terminal |
|---|---|---|---|
| bytes to the phone | 1,502,138 | 1,460,642 | 707,515 |
| dominant message | `screen_snapshot` ×166 = 86 % | `agent` ×90 = 93 %, all discarded | `screen_snapshot` ×81 = 82 % |

1. **A scroll is sent as a whole-screen snapshot.** `ViewportFrame::full` (`engine/core/CellTypes.h:158`) → `ScreenJson.h:77`: a streamed reply produced 166 snapshots of 7,747 B against 86 diffs of 375 B. Chrome on the phone runs at 82.4 % of one core while a reply streams, 0.4 % idle. A scroll primitive on the wire (rows shifted by n, plus the new rows) takes ~1.50 MB to ~0.14 MB for that reply (−91 %).
2. **Tool output is pushed to a phone that discards it.** `remote/wire.py:243` forwards `tool_output`/`tool_result` text; the phone's only reader, `transcribe()` (`app/app.js:1023-1026`), runs only when the desktop does not advertise `screen` (`app/app.js:728`, `:480-482`), and a GUI pane share always does (`remote/host.py:542`, `remote/gui_host.py:216`). 1.33 MB per tool-heavy turn, and 3 of 45 screen markers arrived 1.1–3.1 s late behind it. Consequently the `|| sharedWithPhone()` arm of `Pane::needsToolOutputText()` (`src/Pane.h:4564`, #PPR4) buys nothing: the worker streams 28 KB of text per tool call while shared, for a phone that reads the screen mirror.
3. **`history_get` storm.** 188 requests in 5 s, 121 refused `rate_limited` (`app/screen.js:191/262/270/305` against `remote/host.py:58`), and the refusal is shown to the user under the composer.
4. **`fit()` forces a style recalc per frame** (`app/screen.js:119`): the top JS function, 22 % of non-idle JS.

Measured and fine: phone rendering (0 of 1,053 rAF intervals over 33 ms), an idle share (0 bytes, 0.4 % of a core), Noise/WebCrypto, the 50 MB firehose, latency (82 ms idle, 100 ms loaded).

## Plan
1. A scroll primitive in the screen protocol (`docs/REMOTE-PROTOCOL.md`): the frame carries "rows 0..k shifted up by n" plus the new rows; the phone applies it; a full snapshot only on resize, clear or a lost frame.
2. Stop forwarding tool text to a phone that has `screen`; drop the `sharedWithPhone()` arm so the worker sends counts while shared; keep text for the transcript-only (agent-companion) share.
3. Coalesce `history_get` on the phone (one in flight, the rest merged) and never show `rate_limited` to the user.
4. Cache `fit()`'s measurement; recompute on resize and font change only.
