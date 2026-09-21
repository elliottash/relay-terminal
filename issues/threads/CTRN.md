<!-- relay:entry 20260921T121500Z-a0 author=claude-code kind=event -->
- ✦ card created from the owner's question in the terminal, 2026-09-21: "are the terminal agent and switchboard agent the same system yet?" · the answer was "yes for the interface, the worker and the tools; no for card turns", and the card is the scope of closing that gap.

<!-- relay:entry 20260921T121500Z-a1 author=owner kind=decision -->
- ✦ owner decision: asked whether a card turn should stop being a per-card `Agent` and become an ordinary console turn — its transcript drawing the pane's bubbles, tool rows and queue, with the card's thread still written at both ends — the owner said "that sounds sensible to me, scope that".

<!-- relay:entry 20260921T121500Z-a2 author=claude-code kind=progress -->
- ✦ claimed by the `cardturns-plan` session (planning only; no code was changed and none is claimed).

<!-- relay:entry 20260921T121500Z-a3 author=claude-code kind=plan -->
The plan is on the card. What it decided, and why, in the order the plan states it:

1. **Whose conversation a card turn is — one per card, per tab.** Folding card turns into the
   tab's single conversation was the obvious cheap answer and it is the wrong one: the worker has
   one `TurnSupervisor` and it runs one turn at a time (`backend/relay_core/queue.py:193`), so two
   cards could not be planned at once. That is exactly the regression #DR4K was filed for ("if i
   was planning in one card, i couldnt plan in another card") and what #0Z13's cap removal
   ("remove the cap on number of agents in the switchboard") was protecting. It would also put a
   Plan turn's minutes-long tool trace into the conversation the board, Options, Actions and
   Sessions consoles all share. So the per-card `Agent` that `board_turns.CardSession` already
   holds stays; what it gains is a `TurnSupervisor`, and what the worker gains is a registry
   keyed by surface rather than one pair. The tab id is in the persistence key
   (`<tab id>/card:<ID>`) so that "one worker owns its own conversation files" stays true when two
   tabs show one card. `CardContext::spec()` already asks for a per-card key
   (`src/BoardPane.cpp:3970`) and its own comment anticipated this day.

2. **Who writes the thread — the worker, unchanged.** #AGNT's owner decision 2 said the thread is
   the record; the plan says the *writer* is the worker and gives four reasons the GUI cannot be
   one: a phone sends `board_ask` and has no `CardContext` (`remote/board_state.py:87-88`,
   remote §17.4), a turn can end with no GUI attached, `append_to_thread` takes an exclusive
   `flock` on the threads directory (`board.py:963-1001`), and the answer must go through
   `strip_tool_fragments` first (#VN69). `CardContext::turnFinished` stays the view refresh it
   already is. The owner's entry goes on being written at submit rather than at turn start, so
   `test_the_question_is_recorded_before_the_agent_sees_it` survives untouched; the consequence of
   a queue — a withdrawn prompt leaving a question with no answer — is stated rather than worked
   around, because that is what a failed turn already leaves.

3. **Where the stage rule lives — a per-turn refusal, not a narrower tool list.** The precedent is
   `readonly`, which §33.2 already describes in the sentence that decides it: *the tool list is
   unchanged*. `Agent.set_card_turn(mode, card)` joins `set_readonly` at the same two lines of
   `queue.py`, opens the `CardScope` that already exists, and `_check_card_scope` and
   `CardScope.refusal` do not move — so `tests/test_board_tools.py:2255-2300` passes unedited,
   which is the proof the stage machine did not move. What retires is `Agent.tools()`'s
   `card_scope` branch and `CardScope.tool_specs`. The cost is said plainly: a Plan turn is
   offered tools it will be refused, as a `readonly` turn already is.

4. **What the card page shows.** The finding that decided it: today the same bytes are drawn
   **twice** — `RelayWindow::deliverToConsoles` fans every worker event to every console of the
   tab with no `surface` filter (`src/RelayWindow.h:7796-7809`, deliberately), and
   `BoardView::handleEvent` also renders the card-tagged stream into the thread view
   (`src/BoardPane.cpp:6473-6534`), where every tool call collapses to one elided progress line.
   So the transcript streams and the thread view settles; the thread view's live tail and the four
   `append*` methods go; one predicate is added to `deliverToConsoles` so a card's events reach
   that card's console only — the filter #AGNT's decision 1 said could be added back in one line
   if it were ever needed.

5. **Execute and Verify are untouched.** They were never card turns: they open a terminal pane
   with `board::executeTask` / `verifyTask` text and the card id as an `ask {cards:[id]}`
   attachment, then claim the card with the pane token (#R9G7). The plan forbids the steps from
   touching them and puts `tests/boardexecute_test.cpp` on the must-pass-unchanged list.

6. **`board_ask` stays the wire verb.** This is the one place the plan departs from the shape the
   question was scoped in, and it is recorded as its own numbered decision on the card so the
   owner can overrule it. Changing the verb to `ask {surface, mode}` would mean a compatibility
   shim, a second path to the two thread writes, and a rewrite of remote §17.3/§17.4 — for no
   behaviour the owner asked for, because everything he would see follows from the turn being
   supervised, not from the message it arrived in. Only the four queue ops and `cancel` gain a
   `surface`.

Seven steps, disjoint file sets, order A=1 · B=2,3 · C=4,5 · D=6 · E=7. **No step opens
`src/Pane.h`**, and only step 5 opens `src/RelayWindow.h`, where it is two edits — the busy strip
staying on the card page is what buys that.

<!-- relay:entry 20260921T121500Z-a4 author=claude-code kind=question -->
Six questions are written out on the card under `## Planning notes`, each with its options, the
recommendation the plan is written against and the step that changes if the answer differs. Five
are the owner's:

1. **Whose conversation is a card turn?** — recommendation: one per card, persisted per (tab,
   card). Answering "the tab's one conversation" instead would undo #DR4K and #0Z13 and should
   reopen #DR4K in the same commit.
2. **Who writes the thread?** — recommendation: the worker, as today, with `turnFinished` staying
   a view refresh.
3. **Should a Plan turn be offered tools it will be refused?** — recommendation: yes, for a stable
   prompt-cache prefix, with the refusal naming Execute. Answering no keeps
   `CardScope.tool_specs` and costs a re-prefill on every mode change.
4. **Does the card page keep its busy strip?** — recommendation: yes, with its #VZ69 wording and
   its ✕, minus the progress line the tool rows replace. It is also what keeps `src/Pane.h` out of
   this card entirely.
5. **Does a queued card prompt that is withdrawn leave its question on the thread?** —
   recommendation: yes, unchanged from what a failed turn leaves today.

The sixth is the plan's own and is flagged as such: `board_ask` stays the verb (above).

Nothing here is blocked on an answer — the steps can start against the recommendations, and each
says which of them it would have to revisit.

<!-- relay:entry 20260921T190518Z-ds author=agent kind=event pane=switchboard -->
- ✦ agent moved this card · Planned → Executing · assignee agent

<!-- relay:entry 20260921T190518Z-dt author=agent kind=decision pane=switchboard -->
Owner, 2026-09-21: "all good with 1-6, go ahead" — every recommendation taken as written: (1) one conversation per card, persisted per (tab, card); (2) the worker keeps writing the thread, `turnFinished` only refreshes the view; (3) a Plan turn is offered the full tool list and refused at call time with the refusal naming Execute; (4) the card page keeps its busy strip (#VZ69 wording and ✕) minus the progress line; (5) a withdrawn queued prompt's question stays on the thread; (6) `board_ask` stays the wire verb. Execution starts with Opus subagents, one per step: 1–3 backend and 4–5 GUI in parallel, then 6 docs and 7 the live drive.

<!-- relay:entry 20260921T191321Z-rp author=claude-code kind=progress -->
**Step 1 landed** (`8f5322a9`): a `TurnSupervisor` can be asked for a card turn. The wire, verbatim, for the GUI side:

* `ask {mode, card}` — `TurnSupervisor.submit(..., mode="discuss"|"plan", card="<ID>")`, beside `surface`, `screen` and `readonly`. Both or neither: a `mode` with no `card` (or the reverse) is refused before anything is queued, as is either longer than 64 characters or holding a newline. The *spelling* of the mode is still the board's (`CARD_MODES`, `BoardTools.begin_card_turn`).
* The **ask** says `card`; every **event** says `card_id`, which is what 19.10 has always tagged a card turn's events with. Both fields are absent — not empty — on a turn that is not a card's, so a pane's wire is byte-for-byte what it was.
* `queued {…, mode, card_id}`, every `queue_changed` row in `items` and in `steering` `{id, preview, forced, origin, surface, mode, card_id}`, and the boundary pair `agent_started {id, turn_id, surface, mode, card_id}` / `agent_finished {id, outcome, surface, mode, card_id}` — from the item that is actually running, so a Plan queued behind a Discuss is bracketed as the Plan it is.
* A card turn's fields survive a steer round trip: `queue_steer`, `queue_unsteer` and a returned steer all carry `mode`/`card` back onto the requeued item.

`Agent.set_card_turn(mode, card_id)` joins `set_readonly` on the two lines of `queue.py` that bracket `agent.ask`, and `set_card_turn(None, None)` closes it in the same `finally` — including when the ask raises. It opens the `CardScope` that already existed, so `_check_card_scope` and `CardScope.refusal` did not move and `tests/test_board_tools.py`'s `CardTurnScopeTests` is untouched. `CARD_BLOCKED` in `agent.py`, beside `READONLY_BLOCKED`, is the executor's half: `_prepare` refuses `write_file`, `edit_file`, `run_command`, the subagent tools, `set_keybinding`, `command_output` and `stop_command` with `CardScope.refusal`'s sentence, which names Execute (owner decision 3). The refusal does not wait for a board to be attached. The tool **list** is unchanged, which is the whole point of the decision.

Tests: `tests/test_queue.py` gained four cases (`test_a_queued_card_turn_carries_its_mode_and_card`, `test_the_card_scope_is_opened_for_the_turn_and_closed_after_it`, `test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why`, `test_a_mode_without_a_card_is_refused_before_anything_is_queued`). `tests.test_queue tests.test_agent tests.test_board_chat tests.test_app_tools tests.test_roles tests.test_board_protocol tests.test_board_tools tests.test_agent_context tests.test_board_turns` — 711 tests, all green, and green before the step too (the #8EJ4 failure set is empty on this tree).

<!-- relay:entry 20260921T191825Z-9g author=claude-code kind=progress -->
**Step 4 landed** (`21660474`): the card page stops drawing the turn.

What the card page draws now: the card, the strip and the **settled** thread. `appendEntry` (`board_thread_appended`) is the only way anything reaches the thread view; `appendDelta`, `appendThinking`, `finishThinking`, `sealThinking`, `render()`'s live tail, `LiveThinking`/`insertThinking`, `sanitizeTrace` and the 40 ms coalescing timer are gone, and `BoardView::handleEvent`'s card block lost its `delta`, `thinking_delta`, `thinking_done`, `status`, `tool_started` and `tool_result` arms (the terminal arm and `m_cardTurns` stay — the list's working marker, the Execute/Verify refusal, `cardBusyChanged()` and `onTurnEnded` read them, and `CardTurn` is now `{mode, unsent}`). The busy strip keeps #VZ69's wording and its ✕ and lost the progress line: the `boardBusyWhat` widget, `setProgress` and `m_progress` are deleted, and a test pins that the label does not come back. `ensureCardConsole` no longer calls `setTranscriptHiddenUntilUsed(true)` — the transcript is the live view of the turn now.

What the GUI sends, for step 2's session to read against: `board_ask {card, mode, surface: "card:<ID>", text?}` and `board_cancel {card, surface: "card:<ID>"}` — the verb unchanged (decision 6), `surface` added beside the card so the op names the supervisor it is for. `CardContext::spec()` now asks for `persist {scope: "helper", key: "<tab id>/card:<ID>"}` (decision 1); when the page has not been told a tab id it asks for `card:<ID>` alone and step 5's `TabConsoleContext` supplies the tab.

**One thing the GUI cannot fix from here**, for whoever lands step 2: a card supervisor's `queue_changed` (and `queue_ack`) carry `surface` on each *row* but nothing on the envelope (`queue.py:_changed_locked`), and `Pane` takes a `queue_changed` whole. Step 5's router therefore also reads the rows: an envelope with no `surface` whose rows all name one `card:<ID>` is delivered to that card's console only. If step 2 tags the envelope as well, that fallback simply stops being reached — but without one of the two, the card's §12 queue strip is drawn in the board console too.

Tests: `theThinkingTraceRunsInTheCardsThread` → `theThinkingTraceRunsInTheCardsConsole` (every event of a running turn is fed and the thread view does not move; the settled entry lands with its `model=`; the console's surface is `card:K7Q2` and its key `tab-7/card:K7Q2`; the transcript is never hidden). `theBoxDiscussesAndTheRowPlansOrLeavesTheBoard` and `theCardPageAsksForACardConsoleAndItsActionsFollowTheCard` gained the `surface`, the persist key and the "the progress line is back" guard. `aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen` keeps two turns at once and loses only the per-card streamed text. Nothing was deleted outright. `ctest -R '^board$|^boardpane$|^boardworkspace$|^consolemode$'` green; `boardexecute` fails 3 of 5 exactly as #48S3's thread records (its `findChildren<QPushButton *>("boardExecute")` predates #AGNT's action row, and its CMake registration is still an uncommitted hunk in the shared tree) — unchanged by this commit.

<!-- relay:entry 20260921T192124Z-8w author=claude-code kind=progress -->
**Step 5 landed** (`d9577277`): a card's events reach that card's console, and its conversation is its own. Two edits in `src/RelayWindow.h`, as the plan said.

1. **`deliverToConsoles` routes by `surface`.** An event whose `surface` is `card:<ID>` is delivered to the console whose own `spec().surface` equals it and to no other console of the tab; an untagged event, and `switchboard` / `options` / `sessions`, broadcast exactly as before. The surface is read through the wrapper (`entry.context->spec().surface`), so it is the same string the console's `configure` block carried. A new `cardSurfaceOf(event)` answers "whose turn is this?" and also reads a `queue_changed`'s rows, because the worker tags the surface per row and nothing on the envelope: rows that all name one card make the envelope that card's. An envelope with **no** rows still broadcasts — it says nothing about whose it is — so if step 2 tags a card supervisor's envelopes, that is strictly better and the row fallback simply stops being reached.

2. **`TabConsoleContext::spec()` stops overwriting a card's persist key.** `<tab id>/card:<ID>` for a console whose host key names a card, the tab's id for every other console (the #AGNT override now applies to the helper surfaces only). The tab id is still the window's to supply — a view has no idea what a tab is — and a host that already put the tab in front (`BoardView::setTabId`, which step 4 made it do) is not prefixed twice.

Tests: `tests/boardworkspace_test.cpp` gains `aCardTurnsEventsReachThatCardsConsoleAndNoOther` and `aCardConsoleKeepsItsOwnConversationAndTheHelpersShareTheTabs`, pinned as source text the way every other rule about the window is in that file (it needs a whole Qt application and a worker to run). `theWindowsWrapperForwardsEveryContextVirtual` and `theConsolesOfATabShareOneWorkerAndOneConversation` pass unchanged. `ctest -R '^boardworkspace$'` green, and land.py's verify built the exact tree.

The GUI half of this card is complete and on `main` (`21660474`, `d9577277`). The live Xvfb drive (the card's Verify items 1, 2, 3, 5, 6) needs steps 2 and 3, since a second Enter is still refused with `board_busy` and the turn still runs on `CardTurns.start`'s bare thread until then; it runs as soon as they land and its evidence goes to `docs/qa_evidence/2026-09-21-card-turns-console/`.

<!-- relay:entry 20260921T193601Z-0y author=claude-code kind=progress -->
**Step 2 landed** (`1b9571ce`), **step 3** (`cb866888`), and a follow-up on the queue row (`ab0834e5`). The wire, verbatim, for the GUI side — this is everything a card console has to speak now:

**A card turn is an ordinary supervised turn.** Each `CardSession` holds a `TurnSupervisor` of its own (a deque and a daemon thread, like the pane's), so a card has the §12 queue, steering, interrupts, a request ledger and a per-card Stop. Two cards still run at once, uncounted (#DR4K, #0Z13).

* **The verb is unchanged**: `board_ask {card, text, mode}`, `board_cancel {card}`. A second prompt on a busy card no longer answers `error {code: "board_busy"}` — it queues. `board_busy` still comes back for a cleanup, for the console's own turn, and for a *write* (delete, priority) to a card with work on it.
* **Queue ops and cancel are addressed by `surface`**: `queue_remove {item, surface}`, `queue_move {item, to, surface}`, `queue_steer {item, surface}`, `queue_unsteer {request, as_request, surface}`, `queue_clear {surface}`, `resume_queue {surface}`, `cancel {surface}`. `surface: "card:<ID>"` reaches that card's queue; anything else (a pane, a tab console, no surface at all) reaches the worker's own supervisor exactly as before. `resume_queue {surface}` matters: a card's queue pauses after a failed turn, as a pane's does.
* **Every event of a card's turn carries `surface: "card:<ID>"` and `card_id`**, and the mode-tagged ones carry `mode`. The tagged sets are unchanged (`board_turns.CARD_TAGGED`, `MODE_TAGGED`) and the supervisor's own events are tagged too (`QUEUE_TAGGED`): `queued`, `queue_changed`, `queue_ack`, `steer_delivered`, `steer_returned`, `steer_removed`, `steer_escalated`, `interrupting`, `agent_started`, `agent_finished`. So `queue_changed {surface: "card:<ID>", running, paused, items, steering}` says *whose* queue changed — a `queue_changed` with no rows would otherwise address the wrong strip.
* **A queue row** is `{id, preview, forced, origin, surface, mode, card_id}` (`steering` rows the same without `forced`). `preview` is **the owner's words**, not the prompt: `board_ask` builds the model's prompt out of the card's seed block and the mode's brief, and the row and the request ledger show what was typed (`ab0834e5`; it read "[Switchboard card #CRD1 — …] You are Relay's Switchboard agent…" until the live drive caught it).
* **The boundary pair** is `agent_started {id, turn_id, surface, mode, card_id}` / `agent_finished {id, outcome, surface, mode, card_id}` — from the item that is actually running, so a Plan queued behind a Discuss is bracketed as the Plan it is. The turn id is the queue item id, as it is for a pane.
* **A withdrawn queued prompt keeps its question on the thread** (owner decision 5): the entry is written at submit, so `queue_remove` leaves a question with no answer — what a failed turn already leaves.

**The conversation** is one per card, persisted per (tab, card): `persist {scope: "helper", key: "<tab id>/card:<ID>"}`, the tab id from `configure {tab}` (or `context.persist.key`). It is `agent_context.helper_file`'s store, the same one a tab's console uses — not `relay/sessions/`. A worker that has not been told its tab keeps the ephemeral conversation it had before.

**The tool list is one list.** `Agent.tools()`'s `card_scope` branch and `CardScope.tool_specs` are deleted, and `"card"` has left `agent_context.SCOPES` — a `configure {context: {scope: "card"}}` is mapped to `console` rather than refused, for one release (`RETIRED_SCOPES`), and `context {name: "card"}` now defaults to `scope: "console"`. Asserted both ways round the turn in `tests/test_board_protocol.py::CardScopeAgentTests`: a Discuss, a Plan and an ordinary console turn on one agent are offered **byte-identical** tools (23 on the live drive's card console, `write_file` among them). What a mode may not call is refused when it is called, with `CardScope.refusal`, which names Execute; `CardScope.allows`, `_check_card_scope`, `begin_card_turn` and the Plan's `## Plan` rule did not move, and `CardTurnScopeTests` passes unedited.

`BoardTools.console` is now its own flag (`begin_console` sets it) and `end_card_turn` puts the `ConsoleScope` back: "is this a console" used to be read off the very slot a card turn puts its `CardScope` in, so the board's half of the list changed under the turn and vanished between turns.

**Tests**: 717 across `test_queue`, `test_board_turns`, `test_board_protocol`, `test_board_tools`, `test_agent_context`, `test_agent`, `test_board_chat`, `test_app_tools`, `test_roles` — green in the checkout and on a clean `git archive` export of the landed tree; green before step 1 too (the #8EJ4 set is empty on this tree). `test_board_protocol.py:1233`'s `test_a_card_turn_offers_no_commands_no_writes_and_no_subagents` was rewritten **in step 3** (not step 2) as the two cases above.

**Evidence**: `docs/qa_evidence/2026-09-21-card-turns-backend/` (`3f3d1dfc`) — `drive.py` runs `backend/worker.py` over NDJSON against a loopback stub: the queued second prompt, two cards at once, the `write_file` refusal, `cancel {surface: "card:CRD3"}` leaving #CRD4 running, the four per-card conversation files, and a normalised diff of a Discuss thread against one produced by the same driver on `17f082fe8310`, the tip before step 1 — additions only, no attribute or ordering moved.

Steps 4-5 (GUI) and 6 (docs) are untouched by these commits: no `src/` file and no `docs/` file but this evidence directory.

<!-- relay:entry 20260921T193937Z-xs author=claude-code kind=progress -->
Steps 1-3 are done and on `main`. Six commits: `8f5322a9` (step 1, the supervisor takes a card turn), `1b9571ce` (step 2, a supervisor per card), `cb866888` (step 3, one tool list), `ab0834e5` (the queue row shows what was typed), `4951b5b1` (a console keeps a console's board tools when a project is attached mid-conversation — found while making `BoardTools.console` its own flag), and `3f3d1dfc` + `4af1d7cc` (the evidence and its re-run on the landed tree).

Final gate: `RELAY_KEYRING=off PYTHONPATH=backend:tests python3 -m unittest tests.test_queue tests.test_board_turns tests.test_board_protocol tests.test_board_tools tests.test_agent_context tests.test_agent tests.test_board_chat tests.test_app_tools tests.test_roles` — **718 tests, OK**, on a clean `git archive main` export, not only in the shared checkout. (In the checkout itself four `tests/test_roles.py` cases fail on another session's uncommitted `roles.py`; they pass on the exported tree, so they are not this card's.)

Two things for whoever takes steps 4-7:

* **Step 4/5 (GUI)**: the events a card console must now read are in the entry above. The one that is easy to miss is `queue_changed {surface: "card:<ID>"}` — with a supervisor per card there are several queues in one worker, and a `queue_changed` with no rows is otherwise unaddressed.
* **Step 6 (docs)**: §19.10 gains the queue and loses "one turn at a time, per card"; §19.16's runner is a supervisor; §33.2 gains `mode`, `card` and `preview` beside `readonly` and keeps the sentence that matters (*the tool list is unchanged*); §33.3's table drops the `card` row and its third constraint becomes "a card's Plan turn is refused the writers at call time"; `SCOPES` is two names and `RETIRED_SCOPES` maps the third. `grep -rn '_build_card_agent|tool_scope="card"|CardScope.tool_specs' backend src docs tests` comes back with three prose mentions only: the note in `_build_card_console`'s docstring that says what it used to be, the same in `test_board_tools.py`, and `docs/qa_evidence/2026-09-20-helper-on-guest-main/NOTES.md`, which is a record of a day and should not be edited.

<!-- relay:entry 20260921T222351Z-kj author=claude-code kind=progress -->
**Steps 4 and 5 are done, and the live drive is in** (`dc083b31`, `421361bb`; evidence `docs/qa_evidence/2026-09-21-card-turns-console/`, 24 checks passed, 5 failed and every one of them explained in NOTES.md).

**What the drive found and `dc083b31` fixed.** Two things the card page still did that only made sense while the worker refused a second turn on the same card:

* **The page was refusing the second prompt itself.** `CardDetail::submit` dropped an agent line while `m_busy`, `plan()` returned early and the row greyed Plan out. The worker queues now (step 2), so those guards were the only thing left stopping the Issue #AGNT was filed for from reaching the last surface without it. Enter on a working card sends and runs when the turn before it ends; Plan pressed during a Discuss queues as a `plan` item (Planning notes 5); Execute and Verify still wait, because they hand the card to a terminal pane. The strip goes on naming the turn that is *running*.
* **The card's console had no size.** `updateConsoleHeight` sized the list page's console only — "a maximum alone is not a size" — and left the card's at its size hint, which was right while the turn was drawn in the thread view above it. It draws the turn now, and the drive found it two lines tall with the fold and the tool rows scrolled out of it. Same rule, both consoles.

**What the drive proves.** The turn is in the card's console (`c02a-fold.png`: `▸ ✦ thought for 1 s`, `▸ read fixture.txt · 1 line`, the answer streaming) while the thread view holds the owner's entry and nothing else (`c02-threadview.txt`, read off `boardCardDocument`'s own rectangle) and settles on the worker's entry with `mode=`/`model=`/`turn=` at the end. The strip is the label and the ✕ and the `boardBusyWhat` widget is gone. **One console is handed the turn's events** — one `pane=` id on `tool_started`/`agent_started`/`agent_finished` in `relay.log`, with the board's console open in the same tab; before step 5 every console of the tab got all three. Two cards work at once, ✕ names one, a second Enter queues and both turns land in order, and a Plan's `write_file` is refused in the sentence that names Execute with the turn carrying on.

**Three things this card cannot close from the GUI side.**

1. **A queued prompt on a card has no row on screen while it waits.** The §12 strip is drawn from the pane's *own* client-side queue (`Pane::m_entries`, filled when a line typed into its composer is held back), and a card's prompt never goes that way: `CardContext::submit` sends `board_ask`, so it waits in the worker's queue, which arrives as `queue_changed` — and `rebuildQueueStrip` does not read it. The prompt does queue and does run; what is missing is the row. It needs `src/Pane.h` (the strip reading `queue_changed`'s rows, or a host call that hands the pane a queued row), which no step of this card may open (Risks 8). **Verify item 2 is met in substance and not in the strip.**
2. **The card page keeps one console for whatever card is open**, so switching cards leaves the previous card's turn in the transcript under the new card's title. The conversation and the routing are per card — the context's `surface` and persist key follow the open card — but the scrollback is shared. Clearing it on a card change needs a `ConsoleHandle` call and `src/Pane.h`, so it is a decision rather than a fix to slip in here.
3. **The restart check is not decidable in this drive**: the restarted window came back with a *different tab id* (`r06/r07-tab-*.txt`), and a card's conversation is keyed per (tab, card), so a different conversation is the right answer to a different tab. The per-(tab, card) file itself is proved in the backend drive (its item 6), and this run shows the card's turn in a helper conversation file of its own and the thread complete after the restart.

Also for whoever writes step 6: `board_ask` and `board_cancel` now carry `surface: "card:<ID>"` beside `card`, and `TabConsoleContext` keys a card console's conversation `<tab id>/card:<ID>` while every other console keeps the tab's.
