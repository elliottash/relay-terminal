---
id: JDN4
type: work
status: executing
assignee: agent
session: d74acb62-86ad-4e2a-ada3-2f9151add1f7
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'remote: iOS Safari'
links: {plans: [], commits: [669be841ba50], evidence: [], related: [], github: null}
---
# I like that it shows the previous command that’s running in the queue, but there…

## Issue
I like that it shows the previous command that’s running in the queue, but there shoild be like an uncollapse button to see the whole thing if it’s a long message.

## Discussion points
Map of where the collapsed text lives (2026-09-25).

**What is collapsed.** The queue strip above the composer: the `▸ running <message>` line and every QUEUE row under it. Each is one line, ellipsised, on both surfaces. Filed from iOS Safari, so the phone is the primary surface.

**Where it is drawn.**

- Phone: `app/pane.js` — the running line is `rp-queue-running` (:261-264), rows build `li.rp-row` with `rp-row-label` (:480-496); `app/pane.css` pins both to one line (`white-space: pre; text-overflow: ellipsis`, :172, :209). A row's tap already selects it and its `×` removes it, so the expand control is a third affordance (a chevron or long-press).
- Desktop: the running label is a `QLabel` elided to the strip's width (`src/PaneRuntime.cpp:1929`); the rows are painted by `QueueRowDelegate` in `src/Pane.h` (one line per row, ✦/$/steer glyphs).

**Two layers of truncation.** (1) The visual one-line elide — fixed by letting a row grow to multi-line. (2) The data cap: `QueueEntry::label()` in `src/PaneState.h` clips to `kLabelMax = 400` chars (`clip()`), and that clipped label is what the remote receives — so a truly long message cannot be shown in full from the strip without the state carrying more than the label. The full text is in the pane's queue model and is printed in the transcript when the turn starts.

**Precedents to copy.** The phone's reasoning header collapses/expands (`app/pane.js` header/tail); the desktop transcript folds runs under a ✦ line.

**Shape of the work.**

1. State: add the unclipped text beside `label` in the pane-state JSON (or raise the cap), so both surfaces can show the whole thing.
2. Phone: chevron on the running line and each row; expanded = `white-space: pre-wrap`, the row grows, the list already scrolls (6-row max-height).
3. Desktop: `QueueRowDelegate` `sizeHint`/`paint` multi-line when expanded; a disclosure click on the row.
4. Both: second tap collapses; the strip stays one line per row otherwise.

## Done means
- On the phone and on the desktop, the `▸ running` line and each queued row have an expand control. Tapping it shows that message's whole text, wrapped, in place in the strip. A second tap collapses it back to one line.
- "Whole" means untruncated. A 2,000-character queued prompt, expanded on the phone, ends with its real last words and not `…`.
- Collapsed rows look and act as they do today: a tap still selects the row, `×` still removes it, and desktop drag, reorder and keys still work.
- It has failed if the expanded text stops at about 400 characters with `…`, if expanding one row selects or removes it, or if a `pane_state` tick (10/s during a turn) collapses a row the user just opened.

## Plan
**Goal.** Add an expand control to the queue strip's running line and to each queued row, on the phone (primary) and the desktop. Expanding shows the message's untruncated text, wrapped, in place. This follows the two recommendations in the discussion (grow in place; carry the untruncated text). The owner has not answered them, so the plan assumes both. See Risks.

**Findings (read 2026-09-25).**
- Data: `Pane::queueRows()` (`src/Pane.h`, ~:9591) builds each row's `preview` from `.simplified()` text. The running row comes from `Pane::runningLabel()` (`src/Pane.h` ~:16996, `m_active.label()` / `m_itemPrompts` / `m_workerPrompts`). `QueueEntry::label()` (`src/Pane.h` :595) returns the full `text`.
- Publish: `PaneRuntime.cpp` ~:1832-1837 copies `row.preview` into `relay::panestate::Row::text` and the running row into `Inputs::running`. `PaneState.cpp` :136/:146 emits `label: clip(…, kLabelMax=400)`, so the phone never sees more than 400 characters.
- Phone: in `app/pane.js`, the running line is built at :262-264 and the rows at :480-505. Row rebuilds are skipped when the `signature` JSON (:475) is unchanged (#PKT5). `labelInto()` (:455) splits lead, glyph and text. `app/pane.css` pins `.rp-queue-running` (:172) and `.rp-row-label` (:204) to one line, and `.rp-rows` has a 6-row `max-height` (:183, :652 short viewport). The reasoning toggle (`rp-thinking-toggle`, :243, :1096) is the pattern for the button and its aria labels.
- Desktop: the running line is a `QLabel` named `queueRunning`, elided at `src/PaneRuntime.cpp` ~:2148 inside the strip rebuild. Rows are a `QListWidget` (`queueList`) painted by `QueueRowDelegate` (`src/Pane.h` :200-265): one line, `elidedText` at :254, fixed `sizeHint` height at :264, `→` and `×` hit rects on the right. Items are created at `PaneRuntime.cpp` ~:2250 with the full preview already in the tooltip.
- Tests: `tests/panestate_test.cpp` (ctest `panestate`) covers the message shape. `tests/test_remote_pane_state.py` covers the phone view (section 16).

**Steps.**
1. **State carries the full text.** Add `QString full` to `panestate::Row` and `QString runningFull` to `Inputs`, filled from the unsimplified text: `QueueEntry::text`, the steer's text, and `runningLabel()` before `.simplified()`. Add `QueueRow::full` in `queueRows()` if needed. In `PaneState.cpp`, emit `"full"` beside `"label"` only when it differs from the label, capped by a new `kFullMax = 16000` so one pasted log cannot turn a 10 Hz message into megabytes. Use `clip(…, kFullMax, false)` to keep newlines. Leave `label` as it is.
2. **Protocol doc.** In `docs/REMOTE-PROTOCOL.md` section 16, document `rows[].full` and `running.full`: optional, present only when longer than `label`, capped at 16,000 characters. Update `docs/AGENT-SESSIONS-PROTOCOL.md` only if it lists the row fields.
3. **Phone.** Add a chevron button (`rp-row-more`, `▾`/`▴`, aria "Show whole message"/"Show less") to the running line and to each row. Only draw it when `full` is present or the label is visibly overflowing (`scrollWidth > clientWidth`). Keep the expanded set in closure state keyed by row id (`expandedRows`, plus `runningExpanded`), and add it to the `signature` so a 10 Hz tick neither rebuilds away nor resets an open row. Drop ids that leave the list. Click handler: `stopPropagation()` so expanding does not select or `act()` the row. Expanded: render `full` (falling back to `label`) into the label with class `rp-expanded` → `white-space: pre-wrap; overflow-wrap: anywhere`. Give the expanded label its own `max-height` (about 12 lines) with `overflow-y: auto` so one long row cannot push the others out of the 6-row list. Clear `runningExpanded` when the running text changes.
4. **Desktop running line.** Replace the elided `QLabel` with a small row: a flat `▸`/`▾` `QToolButton` plus the label. Expanded: `setWordWrap(true)`, full text, capped height inside a `QScrollArea` when long. Keep `m_queueRunningExpanded` on `Pane` and reset it when `runningLabel()` changes. The strip is rebuilt often, so the flag must be what survives the rebuild.
5. **Desktop rows.** Add `FullTextRole` and `ExpandedRole` to `QueueRowDelegate`. When `ExpandedRole` is set, `sizeHint` uses `fontMetrics.boundingRect(textRect, Qt::TextWordWrap, full)` (capped at about 12 lines) and `paint` draws wrapped text top-aligned, with the glyph and the `→`/`×` rects pinned to the first line. Add a disclosure hit rect (`▾`, left of `→`) and handle it where the list already handles `×`/`→` clicks (find the `sendNowRect` click handler). Also accept a keyboard path: Space or `→` on the selected row toggles it, if neither is already bound; check the Keymap first. Hold the expanded set in `Pane` by row id so rebuilds at :2250 re-apply it, and call `doItemsLayout()` after a toggle. Drag-reorder must still work on an expanded row.
6. **Shortcut hint (standing rule).** If step 5 adds a key, register a hint ("Next time: <key>") for expanding a row with the mouse, following the registry in `docs/ARCHITECTURE.md` "Shortcut hints".

**Risks.**
- Owner decisions assumed: *grow in place* (not a sheet) and *untruncated* (up to 16,000 characters, not 400). If the owner wants a phone sheet for very long text instead, step 3 changes shape but steps 1-2 stand.
- Payload size: `full` rides every `pane_state` at up to 10 Hz. Sending it only when it differs from `label`, capped at 16k, should keep it bounded. Check the publisher's coalescing in `PaneState.cpp` and that `remote/wire.py` passes the new field.
- `QueueRowDelegate` variable-height rows interact with `InternalMove` drag and the list's own height calculation in the strip. Watch that the strip does not grow past its current maximum.
- `src/Pane.h` and `PaneRuntime.cpp` are hot files shared with other sessions. Claim them with `land.py begin` just before editing, and land promptly.

**Verify.**
- `tests/panestate_test.cpp`: `full` is absent for short rows. For a 2,000-character row it is present and ends with the row's real last characters. It is capped at `kFullMax`. `running.full` behaves the same. Run `python3 scripts/land.py try <me> --tests panestate`.
- `tests/test_remote_pane_state.py` (or the browser test it uses, `tests/browser.py`): queue a 2,000-character prompt. Tap the chevron and the row's text ends with the real last words. Selection is unchanged. A further `pane_state` tick keeps it expanded. A second tap collapses it. Tapping the row body still selects it.
- Live on the desktop under Xvfb with an isolated `XDG_CONFIG_HOME`: queue a long prompt behind a running turn, expand the running line and a row, and screenshot both collapsed and expanded. Check that `×`, `→` and drag still work on the expanded row. Put the evidence under `docs/qa_evidence/YYYY-MM-DD-queue-expand/`.
