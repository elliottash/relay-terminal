---
id: CYM9
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
priority: 2
rank: zzzzzzzzzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [47bb59fa], evidence: [docs/qa_evidence/2026-09-20-switchboard-delete-card/], related: [], github: null}
---
# switchboard needs a delete button for issues

## Issue
switchboard needs a delete button for issues. right now, i dont see how to delete them.

## Plan
**Goal.** Let the owner delete a card from the Switchboard: a Delete button on the open card plus a Delete-key path on the selected card, both confirmed, removing the card file and its thread from disk — undoable for 30 s like every other write. Agents keep no delete tool.

**Findings.**

- No delete exists anywhere: `backend/relay_core/board_tools.py` writes create/update/move/priority/comment/merge/split/sections; `backend/relay_core/board_protocol.py` `TYPES` (line 36) has no `board_delete`; `src/BoardPane.cpp` contains no "Delete" string at all. Closing today is `m` → move to `done`/`dropped` (`BoardView::moveSelected`, BoardPane.cpp:4742) — the path the owner could not find.
- The undo machinery is already both halves of an undoable delete: every write snapshots file bytes in `WriteRecord` (board_tools.py:859; `_record`/`_record_path` 1661/1669), restorable for `UNDO_SECONDS = 30` (line 98). `undo()` (2400) rewrites `record.before` bytes, unlinks the file for a `create` (2414–2418), and merge/split already restore extra files through `record.others`.
- Owner-only writes that are not agent tools are a settled pattern: `board_priority` calls `BoardTools.set_priority` directly (`_write`, board_protocol.py:1194). The agent toolset stays `board_list/read/create_card/update_card/move_card/comment` (protocol §19 table ~2311; "No delete tool" note ~2139).
- GUI plumbing: `m_pendingNotes` → `board_written` (BoardPane.cpp:3738–3764) shows the notice with Undo and sets `m_lastWrite`; `board_changed` `removed` (3616–3649) already drops rows and closes an open detail — but shows its own "#X was removed" error notice, which would clobber the Undo toast; `undoLast()` (4476) sends `board_undo`; `board_undone` (3767) already says "Undone: #X is back as it was".
- Card detail: title-row `m_edit` "✎ Edit (e)" (1236–1242); detail keys e/p/x/v (1924–1938) — `d` is already bound at 1942, so check it before reusing the letter. List keys n/a/e/p/x/v/m/c/y/t/o live in `handleBoardKey` (4879–5083). Threads are separate files, `threads/<id>.md` (`Board.thread_path`, board.py:1089). Confirm-dialog precedents: `Conversations.cpp` `remove` (~1983), `FilePanes.cpp` `deleteEntry` (549).

**Steps.**

1. `backend/relay_core/board_tools.py` — add `BoardTools.delete_card(card_id, reason)` beside `set_priority` (~2060): resolve the card, read the card bytes and the thread bytes (`board.thread_path(card.id, card.private)`), unlink both (and the other privacy variant if present), record via `_record("delete", card, summary, before_bytes, 0)` keeping the thread bytes on the record the way merge/split keep `others`, and return `{id, removed: true, path, write_id}`. No tool spec, no `run()` registration — owner-only, like `set_priority`.
2. Same file, `undo()` (2400): for action `"delete"`, recreate the parent directory if the last card in it went, `B._atomic_write` the card bytes back, restore the thread file from the recorded bytes; the existing `board_undone` + `_changed` tail then reports the card as an upsert.
3. `backend/relay_core/board_protocol.py` — add `"board_delete"` to `TYPES` (36) and a branch in `_write` (1160–1220): refuse first via `_forge_busy` and `_busy_error(rid, "the delete", card_id)` (a running plan/discuss/cleanup/page-agent turn blocks it), then `tools.delete_card(...)`, then the same `board_written {kind: "board_delete"}` + `_changed(write_id)` tail the other writes use. `INIT_WRITES` is unchanged — a delete on an uninitialized board is the plain no-card error.
4. Docs: protocol §19.3 message table (~2017) gains `board_delete {id?, card, author?}` → `board_written` + `board_changed`; the "No delete tool" note (~2139) and `backend/relay_core/board_policy.md` rule 9 gain one clarifying line — agents still have no delete tool; the owner's GUI deletes through `board_delete`. Mirror it in `docs/SWITCHBOARD-DESIGN.md` (owner actions ~760, and the Undo section).
5. `src/BoardPane.cpp` `CardDetail` — a trash `QToolButton` beside `m_edit` in the title row (~1236), same styling, disabled while a turn runs on the card; it asks a `QMessageBox::question` naming the card ("Delete #ID “title”? The card file and its thread are removed from disk. Undo works for 30 seconds; git still has it if it was committed.") and then sends `board_delete`.
6. Same file — `BoardView::deleteSelected()`: sends `board_delete {id: requestId, card}` with the pending note "Deleted #%1"; wire the Delete key (and a free letter — `d` looks taken at 1942, verify) in `handleBoardKey` and the card detail, and add "Delete card…" at the foot of the `m` popup (`moveSelected`, 4742).
7. Same file — in the `board_changed` handler (3616–3649), skip the "#X was removed from the board." error notice for a card this pane itself just deleted (still close the detail and rebuild), so the Undo toast survives.
8. Standing rule (WARP.md): the Delete key is a fast path — add a hint in `src/Hints.cpp` (`board.delete`) covering the slow paths (the button, the `m` menu entry). Update `README.md`'s Switchboard key table (~350) and `docs/ARCHITECTURE.md` §10a.

**Risks.**

- This relaxes "Nothing is deleted" (`board_policy.md` rule 9) for the **owner only**. Recommendation: keep agents without a delete and give the owner this confirmed, undoable path — the same split as editing a card file by hand. Open question, see the thread.
- A card linked to a GitHub issue (`links.github`) leaves the issue open when deleted; the sync will not close it.
- After the 30 s Undo window the only recovery is git: a committed card comes back, an uncommitted one does not. The confirm dialog says so.
- Deleting a card another pane has open is already handled by the `removed` path; deleting mid-turn is refused (step 3) rather than racing the agent.

**Verify.**

- `python3 -m pytest tests/test_board_tools.py tests/test_board_protocol.py -k delete` — new cases: delete removes card + thread files and returns a `write_id`; `board_undo` restores both; `board_delete` on the wire answers `board_written` + `board_changed` with the id in `removed`; refusal while a card turn runs.
- `ctest --test-dir build -R boardmodel` — new case in `tests/boardmodel_test.cpp`: the detail's Delete button and the Delete key send `board_delete` for the right card; the notice carries Undo; cancelling the confirm changes nothing.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME` (WARP.md): open a board, delete a scratch card from the detail and from the list, watch the Undo toast restore it, and confirm a second pane's open detail closes with the "was removed" line.

## QA checklist

- [ ] `PYTHONPATH=backend:tests python3 -m unittest test_board_tools.UndoTests -k delete` — six delete cases: removal of card + thread (and the private thread variant), byte-exact undo, folder recreation, unknown-card refusal, no agent delete tool.
- [ ] `PYTHONPATH=backend:tests python3 -m unittest test_board_protocol.DeleteWriteTests` — five wire cases: `board_written {kind: board_delete}` + `board_changed` with the id in `removed`, `board_undo` restores card and thread, `board_busy` refusal while a turn runs on the card, unknown card, no-board error.
- [ ] `./build/relay-board-tests theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives` (or `ctest --test-dir build -R "^board$"`) — the Del key and the detail's Delete button send `board_delete` for the right card, the notice carries Undo and survives the removal events, another pane's removal still notices, cancel sends nothing.
- [ ] Live, if you want eyes on it: `bash docs/qa_evidence/2026-09-20-switchboard-delete-card/drive.sh` under Xvfb replays the whole card — its `ocr.txt` is the receipt of the run the implementer made (list delete, Undo restore byte-for-byte, detail delete taking the thread too, cancel, and the "was removed" notice for an outside deletion).
