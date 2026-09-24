---
id: MTCS
type: work
status: needs-verification
labels: [bug, restore, engine]
assignee: agent
implemented_by: glm/glm-5.3
session: 13d4b665-1022-4d5a-90bc-7ec293856c1d
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/], related: [], github: null}
---
# Word wrap lost when a pane is restored: save prose blocks' logical lines with the scrollback

## Issue
word wrap is not preserved across restoring a pane. can we save text in a way that word wrap still works and you can resize a pane and it looks OK

## Done means
- A pane whose saved text contained Relay's own word-wrapped output (prose blocks, #R2WQ) restores, and resizing the restored pane re-wraps that output instead of leaving hard-wrapped rows at the old width.
- The saved scrollback file still restores in a build without this change (rows unchanged; the added trailer is ignorable), and a file written by an older Relay restores as before (no trailer).
- Replay stays safe: a hand-edited file's trailer can only carry text, SGR and link targets for rows that are themselves in the file — same surface as today's rows.
- Tests: serializer keeps `relay://prose/` links through `linesToSavedAnsi`+`restorableAnsi`; view/backend enumerate prose blocks; windowstate round-trips the trailer and keeps it under the byte clamp.

## Plan
Cause: at print time each inline block of Relay's own output is wrapped in an OSC 8 `relay://prose/<token>/<seq>` run and `setProseBlock(uri, logicalLines, printColumns)` hands the pre-wrap lines to the view's fold layer, which re-lays them out on resize (#R2WQ). At save time `lineToSavedAnsi` keeps SGR + image/media links only, so the prose runs are stripped, and the logical lines were never persisted anywhere — a restored pane replays fixed-width rows.

1. **Engine, keep the anchors**: `lineToSavedAnsi` and `restorableAnsi` (engine/core/AnsiSerializer) also keep `relay://prose/` OSC 8 runs (fragment forms included). An unregistered prose URI is inert: the fold layer only acts on URIs it has a block for.
2. **Engine, enumerate**: `FoldLayer::proseBlocks()` → (uri, FoldLines, printColumns); `TerminalView::proseBlocks()`; `virtual TerminalBackend::proseBlocks()` (default empty, ghostty core unaffected); `VTermBackend` override.
3. **Files**: `windowstate::writeScrollback(id, rows, prose)` and `sessiontext::write(path, rows, prose)` append a trailer after the rows: separator line `\x1b_relay-prose\x1b\\` (cannot occur in a serialized row: rows only contain CSI-m and OSC-8), then one compact JSON object per block — uri, columns, lines of {role, spans of {text, sgr, link}}. Read side: `readScrollback` still returns rows only (trailer split out), new `windowstate::readProseBlocks(id)` / `sessiontext::readProse(path)`; byte clamp accounts for the trailer (rows shrink, records survive at the tail).
4. **Pane**: `saveScrollback`/`saveSessionText` pass `m_backend->proseBlocks()`; constructor and conversation replay carry `m_restoredProse`; `replayRestoredScrollback` registers each block with `setProseBlock` after writing the rows (same order as live printing); banked surfaces (`clearTranscript`) and the fork stash carry their prose too.
5. **Tests**: AnsiSerializer (links survive save+replay), ViewTest (enumeration + restore-then-resize re-wrap), host windowstate test (round-trip, clamp, old-format file).

## Execution Summary
Landed in `1d80a83b` (verify build of the exact tree passed; land.py `--confirm` review for the contested `src/Pane.h` hunks, all mine).

- `engine/core/AnsiSerializer.{h,cpp}`: `lineToSavedAnsi` and `restorableAnsi` keep `relay://prose/` OSC 8 runs (fragment/label forms included; an unregistered URI is inert).
- `engine/TerminalBackend.h`: new `relay::ProseBlock` {uri, lines, printColumns} beside `kProsePrefix`; new `virtual QVector<ProseBlock> proseBlocks() const` (default empty) next to `setProseBlock`.
- `engine/view/FoldLayer.{h,cpp}`, `engine/view/TerminalView.{h,cpp}`, `engine/backend/VTermBackend.{h,cpp}`: enumeration plumbed; `savedLines` walks prose runs beside image/media so saves keep the anchors.
- `src/WindowState.{h,cpp}`: `writeScrollback`/`sessiontext::write` take the blocks and write the trailer (separator `\x1b_relay-prose\x1b\\` + one compact JSON record per block: uri, columns, role, spans of text/sgr/link/fg/bg/flags); records count against the byte cap and are bounded to half of it; `readScrollback` still returns rows only; new `readScrollbackProse`/`sessiontext::readProse` drop records whose URI is not under `kProsePrefix`.
- `CMakeLists.txt`: `relay-windowstate` gains `engine/` on its include path for the FoldLine types (declaration only, same pattern as relay-calllines) and Qt Gui for QColor.
- `src/Pane.h`, `src/PaneSession.cpp`: `saveScrollback`/`saveSessionText` write the pane's blocks; ctor, conversation replay (`queueSessionTextReplay`/`queueTextReplay`), banked surfaces (`clearTranscript`) and the fork stash carry them; `replayRestoredScrollback` registers each block with `setProseBlock` after the rows are written — at the saved width it stands aside (rows byte-for-byte), at any other width it re-wraps.

Note: another session is mid-edit in `src/Conversations.cpp` in this checkout; the land verify tree used the tip's version, as designed.

## Tasks

- [x] Serializer keeps relay://prose/ links through save and replay <!-- t:0t -->
- [x] proseBlocks() enumeration through FoldLayer/TerminalView/VTermBackend <!-- t:w4 -->
- [x] Trailer format in windowstate + sessiontext, with caps and hand-edit hardening <!-- t:xs -->
- [x] Pane saves/replays/re-registers blocks (scrollback, session text, banking, fork) <!-- t:dx -->
- [x] Tests: CoreTest, ViewTest round-trip, windowstate round-trip <!-- t:ae -->

## Tests
- `CoreTest::proseRunsKeepTheirLinksThroughRestore` — a prose run (with a `#l=` label fragment) survives `lineToSavedAnsi` + `restorableAnsi` and replays into a fresh core with its URI intact.
- `ViewTest::proseWrapSurvivesSavedTextAndReplay` — the round trip the pane performs: block printed at 12 columns, saved (anchors present, `proseBlocks()` non-empty), rows replayed through `restorableAnsi` into a fresh 40-column backend, blocks handed to `setProseBlock`: the 12-wide rows are gone, the block is re-wrapped to 40, and it re-wraps again at 16.
- `windowstate_test.cpp::proseBlocksSurviveSaveReadAndRestore` — trailer round-trip (uri, columns, role, span text/sgr/link); `readScrollback` never returns trailer lines; a trailer-less file restores as before; hand-edited records under a non-prose URI and malformed lines are dropped; the conversation store round-trips; rows gone means file gone.
- Suites green at `1d80a83b`: `ctest -R "windowstate|relay-engine-tests"` (full engine suite incl. image/media/fold tests) plus `conversations`, `activepaneclose`, `panetabnavigation`. Evidence: `docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/`.

## Try it
**Open**: the Relay window this delivery left on your display (a fresh one: `docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/stage.sh`). It is a sandboxed profile — your own Relay state is untouched — whose single pane was restored from saved text carrying two word-wrapped blocks beside plain build output.

**One task**: drag that window's right edge — make it distinctly narrower, then distinctly wider, a couple of times — and watch the indented paragraph and the heading line above it (the dim text between the build output and the end-of-restore mark).

**One question**: does that wrapped text follow the pane's edge in both directions while the plain build-output rows above it stay put, and does anything about how it re-wraps look wrong (words duplicated or lost, uneven indent, rows left at the old width)?

Evidence (screenshots at two widths, stage.sh, scrollback-with-prose.txt, expected.md) is in `docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/`; put what you saw there too. To close the window: `kill $(cat /tmp/rl-mtcs.last.pid)`. The staged instance's scrollback record: `docs/qa_evidence/2026-09-24-prose-wrap-restore-MTCS/scrollback-with-prose.txt`.
