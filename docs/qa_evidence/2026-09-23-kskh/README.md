# #KSKH — card consoles aren't independent across cards

Owner's report (2026-09-23 14:04): "switchboard agents arent independent across cards, so i cant
plan multiple cards right now"; 14:08, with screenshot
`~/.cache/RelayTerminal/relay/images/relay-paste-20260923-140834.png`: "weird bug with two agents
seemingly going at once".

## What was actually running (worker.log, 2026-09-23)

Five card turns ran concurrently on the switchboard worker, each writing its own card's thread
correctly: #6WKR 18:05:10 (done 18:08:36), #GBN4 18:07:42, #VH3S 18:07:51 (done 18:10:36), #SSRQ
18:08:14 (a Discuss, done 18:09:00), #WMXN 18:09:18. The card turns were never the coupling.

## Fault 1 — a card console's configure poisons the board's tab id

`backend/worker.py` passed the asking console's `persist.key` to `board.set_tab`. A tab console's
key is the tab id by another name; a card console's is `<tab>/card:<X>` (#CTRN decision 1), and it
won. From then on `_card_session_file` keyed every card session `<tab>/card:<X>/card:<Y>`.

Proven on disk — every card conversation of that afternoon was filed under a doubled key
(blake2b digests of the keys, `relay-helper` personalisation, match the session files in
`~/.local/share/relay/helper-sessions/83e6ce670835f99d/`):

- `43c54f7d…` = digest of `t954201436b8f/card:6WKR/card:6WKR` (meta title: `[Board card #6WKR — …]`)
- `477c6c37…` = digest of `t954201436b8f/card:GBN4/card:GBN4`
- `d62abb02…` = digest of `t954201436b8f/card:VH3S/card:VH3S`
- `447de757…` = digest of `t954201436b8f/card:SSRQ/card:SSRQ`

Fix: `board_chat.tab_of(persist_key, tab)` — a card-shaped key names a card conversation, not the
tab; `worker.py` now calls it. `tests/test_board_chat.py::TabOfTest` (3 tests) and
`tests/test_board_protocol.py::AskTests::test_a_card_conversation_is_keyed_by_the_tab_and_the_card_only`
pin the rule and the `<tab>/card:<ID>` key a restart must find.

## Fault 2 — one card's writes print inside another card's console

`board_activity` (and other board-wide events) carry no `surface`, so
`RelayWindow::deliverToConsoles` broadcast them to every console of the tab, and
`Pane::noteBoardActivity` printed "◆ #<id> · <summary>" as a status line + toast and flipped the
card chip. The screenshot's mid-conversation row is #6WKR's "replaced `## Plan`" (its
`board_update_card`, 18:08:23) inside #SSRQ's Discuss console ("Let me check the codebase for
MCP…" is #SSRQ's own stream; its thread note confirms the answer).

Fix: `relay::board::namedCardOf(event)` (src/BoardPane.h) +
`deliverToConsoles` skips an unsurfaced event that names a foreign card on a card console only —
the Switchboard/Options/Sessions consoles keep every card's line (owner decision 1 on #AGNT).
`tests/boardpane_test.cpp::unsurfacedEventsNameTheirCard` covers the helper.

## Test runs (2026-09-23, this checkout)

- `python3 -m unittest test_board_chat.TabOfTest` — 3/3 OK.
- `python3 -m unittest test_board_protocol.AskTests.test_a_card_conversation_is_keyed_by_the_tab_and_the_card_only` — OK.
- `ctest --test-dir build -R '^boardpane$'` — Passed (includes the new slot).
- `python3 -m unittest test_board_chat test_board_protocol` — 7 failures, **all verified
  pre-existing**: with this change stashed (`git stash push -- backend/relay_core/board_chat.py
  backend/worker.py tests/…`), the same 7 fail identically (another session's in-flight edits:
  session_protocol.py, board_protocol.py, board_tools.py).
- `scripts/relay-build --target relay` — fails on 3 errors in another session's in-flight
  "run in background" work (`Pane::agentReady`, `WindowManager::refreshBackgroundTasks`,
  `WindowManager::backgroundCount`); the compiler parsed this change's `deliverToConsoles`
  (RelayWindow.h ~9068) and flagged only line 11547+ and WindowManagerImpl.h. The
  `relay-boardpane-tests` target, which compiles the modified BoardPane.h, builds and passes.
