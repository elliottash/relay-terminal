---
id: KSKH
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
session: d47c1a2d-5224-474f-a103-3fc0a9e65207
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-23'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-kskh/], related: [], github: null}
---
# Card consoles aren't independent: set_tab takes a card's persist key, and one card's writes print in another's console

## Issue
bug: switchboard agents arent independent across cards, so i cant plan multiple cards right now
Two faults found while planning four cards at once (#6WKR 18:05, #GBN4 18:07:41, #VH3S 18:07:51, #SSRQ 18:08:14, #WMXN 18:09:18, 2026-09-23). The card turns themselves ran concurrently and each wrote its own card's thread correctly — the coupling is in the console layer.

1. **A card console's configure poisons the board's tab id** (`backend/worker.py`, configure → `board.set_tab((context.persist_key …) or request.get("tab"))`). A tab console's `persist.key` is the tab id by another name, but a *card* console's key is `<tab>/card:<X>` (#CTRN decision 1) — and it won. From then on `board.tab` was `<tab>/card:<X>`, and `_card_session_file` keyed every later card session `<tab>/card:<X>/card:<Y>`. Proven on disk: today's card conversations were all filed under doubled keys — `t954201436b8f/card:6WKR/card:6WKR`, `…/card:GBN4/card:GBN4`, `…/card:VH3S/card:VH3S`, `…/card:SSRQ/card:SSRQ` (helper-sessions digests) — files no restart finds again, so "a card remembers its earlier turns" broke, and which file a card got depended on which card page happened to be open.

2. **One card's writes print inside another card's console** (`src/RelayWindow.h` `deliverToConsoles` + `Pane::noteBoardActivity`). A card turn's own events are tagged `surface: card:<X>` and routed to that card's console only, but the worker's board-wide events (`board_activity`, `board_thread_appended`, `board_changed`) carry no surface, so they broadcast to every console of the tab. `noteBoardActivity` prints "◆ #<id> · <summary>" as a status line and a toast and flips the card chip — so while #SSRQ's Discuss ran, #6WKR's "replaced `## Plan`" line appeared inside it mid-conversation. Owner's report (14:08, with screenshot `~/.cache/RelayTerminal/relay/images/relay-paste-20260923-140834.png`): "weird bug with two agents seemingly going at once".

Follow-up message (same fault, 14:08): "weird bug with two agents seemingly going at once".

## Done means
Each card's console conversation is keyed `<tab>/card:<ID>` however many card pages were opened before it, and a restart finds it again (check: the digest of `<tab>/card:<X>` names the session file a new CardSession adopts). A card console prints only its own card's write lines and status while other cards' turns run: `board_activity`/`card_id` events naming a foreign card are not delivered to a card console, so no "◆ #Y · …" line, toast or chip swap appears in card X's console mid-turn. Planning N cards at once keeps N consoles each showing only its own turn. Failure shows as the doubled-key files (`…/card:<X>/card:<Y>.json`) still being created, or another card's write line inside a card console.

## Execution Summary
Both faults fixed at their roots; the card turns themselves were already independent.

1. `board_chat.tab_of(persist_key, tab)` (new, beside `validate_tab`): a card-shaped persist key (`<tab>/card:<X>` or `card:<X>`) names a card conversation, not the tab, and no longer reaches `set_tab`; the tab id falls back to the top-level `tab` (which the GUI always sends, `RelayWindow.h` `startBoardWorker`). `backend/worker.py` calls it — one line moved, plus the import.
2. `relay::board::namedCardOf(event)` (new in `src/BoardPane.h`): the card an unsurfaced event names — `board_activity`'s `id`, or `card_id`. `RelayWindow::deliverToConsoles` now stops such an event at a card console whose card is not the one named, so another card's write line, toast and chip no longer print mid-conversation. Card-tagged turn events were already routed by `surface`; the Switchboard/Options/Sessions consoles keep every card's line (owner decision 1 on #AGNT) — no change for them.

No behavior change for: tab consoles' conversation keys, terminal panes' inline write lines (their own workers), the Board page's own rows and activity feed.

## Tests
- `tests/test_board_chat.py::TabOfTest` — 3 tests: a tab console's key is still the tab by another name; a card console's key (`<tab>/card:<X>` and bare `card:<X>`) never becomes the tab; no spellings is no tab. 3/3 OK.
- `tests/test_board_protocol.py::AskTests::test_a_card_conversation_is_keyed_by_the_tab_and_the_card_only` — a `board_ask` after `set_tab("t012…")` files the session under the digest of `t012…/card:<ID>` (the doubled-key shape would fail this). OK.
- `tests/boardpane_test.cpp::unsurfacedEventsNameTheirCard` — `namedCardOf` on `board_activity` (`id`), `board_thread_appended` (`card_id`), the tab's own news (empty), and a surface-routed card turn event. `ctest -R '^boardpane$'` Passed.
- Pre-existing failures in the shared checkout, not this change: `test_board_chat test_board_protocol` have 7 failures that fail identically with this change stashed; `scripts/relay-build --target relay` fails on 3 errors in another session's in-flight "run in background" work (`Pane::agentReady`, `WindowManager::refreshBackgroundTasks`/`backgroundCount`) — none in the files this change touched, and `relay-boardpane-tests` (which compiles the modified `src/BoardPane.h`) builds and passes.

Evidence: `docs/qa_evidence/2026-09-23-kskh/`.
