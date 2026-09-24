---
id: 3H5T
type: work
status: needs-verification
labels: [bug, performance, remote]
assignee: claude-code
rank: ma
created: '2026-09-20'
source: 'Claude Code (#PF4K orchestrator), measured on the owner''s Pixel 8 over a LAN share, 2026-09-20'
links: {plans: [], commits: [79cae3ef, f5db362d, 47653e24, 13772bdf, 71c43fe2, 77b49d59, df2fca6e, 4536acce, c6e89ef5, 779dcef1], evidence: [docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md, docs/qa_evidence/2026-09-20-perf-fixes/phone-scroll/, docs/qa_evidence/2026-09-20-perf-fixes/phone-tools/, docs/qa_evidence/2026-09-20-perf-fixes/phone-js/], related: [PF4K, PPR4, GMCF], github: null}
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

## Result
All four items landed; every number is from the owner's Pixel 8 over a LAN share.

1. **Scroll primitive** (79cae3ef, f5db362d): `screen_diff` may carry `scroll: {top, bottom, by}`;
   clients declare `supports: ["screen_scroll"]` in `hello`/`knock`, and one attached client that
   did not takes it away for everyone on that hub (`docs/REMOTE-PROTOCOL.md` §6.5). A 24 s styled
   reply: 679,798 → 108,088 B on the bridge (−84 %); 200 frames parsed and applied on the phone
   730,890 → 61,690 B (−92 %); Chrome 32.4 → 29.7 % of a core (the bench has no Noise decrypt or
   WebSocket, so the CPU figure understates the live saving).
2. **Tool text** (13772bdf, 71c43fe2): a client that draws the screen is not sent `tool_output` /
   `tool_result`; a transcript-only share still is. Tool-heavy turn 1,460,642 → 138,451 B on the
   air, worst screen frame 3.1 s → 319 ms; the `sharedWithPhone()` arm of #PPR4 removed.
3. **`history_get`** (77b49d59, df2fca6e, 4536acce): one in flight, two cadences (1 s live, 250 ms
   scrolled up), the whole gap per request, 2 s back-off on `rate_limited`, refusals never shown.
   30 s of output: 160 → 30 requests, 40 → 0 refusals.
4. **`fit()`** (77b49d59): cached; 55.8 % → 0.0 % of non-idle JS; non-idle JS 1,241 → 724 ms.

## Open
- `toBottom()` is now the phone's largest per-frame layout cost (294–498 ms of the remaining
  non-idle JS); removing it needs a bottom-anchored layout — a card of its own.
- `src/RemotePane.h` (Relay watching a Relay pane) does not declare `screen_scroll`, so it keeps
  getting snapshots and, on a shared hub, turns the primitive off for a phone too. Scope call.
- `test_remote_browser.test_scrollback_pages_in_when_you_drag_the_terminal_down` is timing-flaky
  since the history cadence change (fails then passes on rerun).

## QA checklist
- [ ] Share a pane to the Pixel; stream a long reply: the phone keeps up, no torn rows, and `adb`-side bytes on the air are a fraction of a snapshot per frame (`phone-scroll/` harness) <!-- t:h1 -->
- [ ] Resize the desktop pane, clear the screen, switch to an alt-screen program (`less`) and back: the phone matches the desktop after each <!-- t:h2 -->
- [ ] A second viewer that is an old client (`relay remote` CLI) joins the same share: both viewers stay correct <!-- t:h3 -->
- [ ] Run a noisy command with Show tool output off while shared: the phone shows the desktop's `▸ ran … · N lines` line and no tool text crosses the wire <!-- t:h4 -->
- [ ] Drag the phone's terminal down through 2,000 lines of scrollback: pages fill with no hole and no `rate_limited` message appears under the composer <!-- t:h5 -->
- [ ] Rotate the phone: the grid refits once; scrolling stays smooth <!-- t:h6 -->
