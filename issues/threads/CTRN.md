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
