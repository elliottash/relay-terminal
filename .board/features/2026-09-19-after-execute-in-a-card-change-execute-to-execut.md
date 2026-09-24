---
id: HKAP
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
priority: 2
rank: zzzzzzzzzzzz
created: '2026-09-19'
links: {commits: [56b921f6, '31468891'], evidence: [docs/qa_evidence/2026-09-20-execute-pane-link/], github: null, plans: [], related: []}
---
# after "execute" in a card, change "execute" to "executing"

## Issue
after "execute" in a card, change "execute" to "executing" with the pane id in parenthesies. when you click on it, it takes you to the pane.

## Plan
**Goal**
After Execute hands a card to a terminal pane, the card's thread entry reads `Executing (<short pane id>) · handed to a new terminal pane beside the board`, and clicking the entry reveals that pane, in whatever window it lives in.

**Findings**
- `BoardView::executeCard` (src/BoardPane.cpp:4209) moves the card to in-progress, sends the worker `board_comment {kind: "progress", text: "Execute · handed to a new terminal pane …"}` (4233-4236), then calls `onExecuteCard(card, executeTask(...))` (4240). The transient move notice "Moved #… to In progress · Execute" (4228) is an undo label, not the thread.
- `BoardView::onExecuteCard` (src/BoardPane.h:52, wired at src/RelayWindow.h:4688-4697) creates the pane synchronously — `w->createPane({cwd, workspace, agent_role: main})`, `insertBeside`, `setActive`, `pane->startBoardTask(task, card)` — and returns `void` today. A pane's stable id is `Pane::sessionToken()` (src/Pane.h:664).
- The thread renderer draws entries with `insertLine(cursor, text, QTextCharFormat)` (src/BoardPane.cpp:2161; entry loop 2263-2330) — plain formats, no anchors yet. `m_doc` is a QTextBrowser whose `anchorClicked` (1406-1410) currently only opens external URLs.
- Thread entries persist arbitrary attrs: `Board.append_thread(..., **attrs)` writes them into the `<!-- relay:entry id k=v -->` marker (backend/relay_core/board.py:1127, 824-858) and they come back to the GUI (the renderer reads `entry.attrs`, BoardPane.cpp:2275). But `board_comment` rejects unknown fields (backend/relay_core/board_tools.py:2034-2036, schema at 244-252) and the GUI→worker handler forwards only id/kind/text (backend/relay_core/board_protocol.py:~1164).
- A pane can already be revealed by token from anywhere: `WindowManager::focusPane(token)` (src/WindowManagerImpl.h:238-244) — the notification tray uses it (src/RelayWindow.h:5907).
- The current strings are normative in docs/AGENT-SESSIONS-PROTOCOL.md (§19.10, ~2318-2326, and the `board_comment` schema row ~2019).

**Steps**
1. Report the pane back: change `onExecuteCard` to `std::function<QString(const QString &id, const QString &task)>` (src/BoardPane.h:52); in RelayWindow.h:4688 return `pane->sessionToken()`, empty on the failure path. Update the stub in tests/boardmodel_test.cpp:2075.
2. Write the entry with the pane: in `executeCard`, call `onExecuteCard` first, then send `board_comment {kind: "progress", pane_token: <token>, text: "Executing (<token left 8>) · handed to a new terminal pane beside the board"}`; with an empty token keep today's "Execute · …" wording. Short id = `token.left(8)`, the SessionInfo convention. The 4228 move notice keeps its wording.
3. Worker: forward and persist the field — GUI handler passes `pane_token` through (backend/relay_core/board_protocol.py:~1164); `board_comment`'s spec and `_comment` accept `pane_token` (≤64 chars, no whitespace or `>`) and hand it to `append_thread` (backend/relay_core/board_tools.py:244, 2034-2050).
4. Render the link: an entry whose attrs carry `pane_token` draws its whole line as an anchor — `QTextCharFormat::setAnchor(true)`, `setAnchorHref("relay-pane:" + token)`, link colour from the palette (src/BoardPane.cpp:2263-2330, insertLine at 2161).
5. Handle the click: in `m_doc`'s `anchorClicked` (src/BoardPane.cpp:1406) branch on scheme `relay-pane` *before* the external-open branch, calling a new `BoardView::onFocusPane(QString token)` callback (declared beside `onOpenPath`, ~1435), wired in RelayWindow.h next to `view->onOpenPath` (~4677) to `WindowManager::focusPane(token)`.
6. Docs: update §19.10 and the `board_comment` schema row in docs/AGENT-SESSIONS-PROTOCOL.md with the new text and the `pane_token` field.

**Risks**
- Stale links: the pane may be closed, or the card read in another window or on another machine. `focusPane` already no-ops when no pane has the token, so the click is inert — no crash. Optional polish, not this card: a "pane closed" notice in the status bar.
- Question for the owner: Verify does the same hand-off with "Verify · handed to a new terminal pane on …" (src/BoardPane.cpp:4266). Convert that entry too while the plumbing is in? Recommendation: yes — one more line in steps 2 and 4; say the word and it goes in.

**Verify**
- `scripts/relay-build`, then targeted: `ctest --test-dir build -R boardmodel` (extend the execute test at tests/boardmodel_test.cpp:~2060: returned token, new text, `pane_token` on the sent comment) and `python3 -m pytest tests/test_board_tools.py tests/test_board_protocol.py -k board_comment`.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: execute a card, see `Executing (xxxxxxxx)` in its thread, click it and land on the pane; close the pane, click again — nothing happens, no crash.

## QA checklist
- [ ] `ctest --test-dir build -R '^board$'` passes — the execute test asserts the returned token, `pane_token` on the sent comment, and the `Executing (pane-ses) ·` first line, plus the plain-note path.
- [ ] `PYTHONPATH=backend:tests python3 -m unittest test_board_tools.CommentTests test_board_protocol.WriteTests -k pane_token` — the 3 new tests pass (the two modules' other failures pre-date this card; lists in the implementer notes).
- [ ] Live, `docs/qa_evidence/2026-09-20-execute-pane-link/drive.sh`: Execute a card — its thread shows `Executing (<first 8 of the pane token>)` and the thread file carries `pane_token=<token>`; click the entry and land on that pane; close the pane, click again — inert, no crash (`ocr.txt` has the run's claims).
- [ ] A comment that is not a hand-off (any ordinary note) still renders as before — no anchor, no `pane_token` attr.
- [ ] A card whose pane could not open (no `onExecuteCard`) keeps the old `Execute · …` wording.
- [ ] Verify's hand-off, same treatment (31468891): `aQaLaneCardOffersVerifyOnTheRecommendedRunner` asserts the returned token, `pane_token` on the comment and the `Verifying (pane-ses) ·` line.
- [ ] Live, same drive step 4 (`drive.sh`): a needs-verification fixture card with an anthropic signature — key `v` opens the verifier's pane, the thread carries `pane_token=<token>`, the screen shows `Verifying (<first 8>)` (`ocr.txt`, shots 08–11).
