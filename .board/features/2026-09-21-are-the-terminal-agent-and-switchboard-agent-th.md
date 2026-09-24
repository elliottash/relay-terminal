---
id: CTRN
type: work
status: needs-verification
labels: [feature, agent, switchboard, architecture]
assignee: agent
rank: zzzzzzzzzzzzzzzk
created: '2026-09-21'
source: Owner, terminal, 2026-09-21, asking whether the two agents are one system yet
links: {plans: [], commits: [8f5322a9, 1b9571ce, cb866888, ab0834e5, 4951b5b1, 3f3d1dfc,
    4af1d7cc, 21660474, d9577277, dc083b31, 421361bb, cd75da79, 7a5d7f27, acda4552,
    5f834ce7], evidence: ['docs/qa_evidence/2026-09-21-card-turns-backend',
    'docs/qa_evidence/2026-09-21-card-turns-console',
    'docs/qa_evidence/2026-09-21-card-turns-final'], related: [AGNT, FEJQ, 8YQ9, 3XZV,
    VZ69, T71W, DR4K, 0Z13, VN69, R9G7, QRC1], github: null}
---
# A card turn is an ordinary console turn: the last surface that still has its own agent

## Issue

are the terminal agent and switchboard agent the same system yet?

— the answer given was: yes for the interface (every helper is a no-shell `Pane` with a
`relay::agent::Context`, #AGNT), the worker (`backend/worker.py`, one protocol) and the tools;
**no for card turns**, which still run on a per-card `Agent` of their own and draw into the card's
thread view instead of the console's transcript. Asked whether a card turn should become an
ordinary console turn, the owner: "that sounds sensible to me, scope that".

## Decisions

- **A card turn becomes an ordinary console turn** (owner, 2026-09-21: *"that sounds sensible to
  me, scope that"*). Discuss and Plan on a card stop being a second turn runner. The card's
  console draws the turn the way every other console draws one — the pane's thinking bubbles, its
  tool rows, its queue strip, Esc, the request ledger and Activity — and the card's thread goes on
  being the record, written by the worker with its `model=` and `turn=` provenance.

- **The thread stays the record, and the worker keeps writing it** (#AGNT owner decision 2,
  unchanged: *"keep the thread writes with their `model=`/`turn=` provenance, and give the card a
  real conversation behind them"*). `CardContext::turnFinished` goes on being a view refresh, not
  a writer — see Planning notes, question 2.

- **A card keeps its own conversation** (#DR4K, owner 2026-09-19: *"if i was planning in one card,
  i couldnt plan in another card"*; #0Z13, owner 2026-09-19: *"remove the cap on number of agents
  in the switchboard"*). Those two decisions are load-bearing for this card: they rule out folding
  card turns into the tab's single conversation, which is what would make two cards serial again.

- **A Plan turn still writes only its own `## Plan`** (19.20, #3XZV; #AGNT owner decision 3:
  *"yes, with the workspace it has, and never for a card's Plan turn"*). What changes is where
  that rule lives, not the rule.

## Discussion points

- Whether the card page should keep its own busy strip ("✦ Switchboarding · planning… ✕ Stop
  planning", #VZ69) once the console has a busy line of its own. The plan keeps it; see Planning
  notes, question 4.
- Whether a Plan turn should be *offered* tools it will be refused. The plan says yes, for the
  prompt cache; see Planning notes, question 3.

## Planning notes

Six questions, each with the options, the recommendation this plan is written against, and which
step changes if the owner answers differently. Questions 1–5 are the owner's; question 6 is
recorded because it is the one place this plan departs from the shape the question was scoped in.

### 1. Whose conversation is a card turn? — **recommendation: one per card, per tab**

Today a card turn runs on its own `Agent`, built per card by `board_protocol._build_card_agent`
(`backend/relay_core/board_protocol.py:643`) and held by `board_turns.CardSession` with an LRU of
six (`backend/relay_core/board_turns.py:44`). A console's turn runs on the worker's single `Agent`
through its single `TurnSupervisor` (`backend/worker.py:105`, `:337`).

- **(a) Card turns join the tab's one conversation.** Simplest — no new machinery at all. But the
  worker has **one** `TurnSupervisor` and it runs **one turn at a time**
  (`backend/relay_core/queue.py:193`), so two cards could no longer be planned at once. That is
  the exact regression #DR4K was filed for and #0Z13's cap removal was about, and it would also
  put a Plan turn's minutes-long tool trace into the conversation the board, Options, Actions and
  Sessions consoles all read (they share one conversation and one store, #AGNT decision 1,
  `src/RelayWindow.h:7557-7558`). **Rejected.**
- **(b) One conversation per card, persisted.** `persist {scope: "helper", key: "<tab id>/card:<ID>"}`.
  A card's Discuss keeps the memory of its own earlier turns across a restart, two cards run in
  parallel, and a Plan's trace stays out of everybody else's conversation. `CardContext::spec()`
  already asks for exactly this — `persistScope = "helper"`, `persistKey = "card:<ID>"`
  (`src/BoardPane.cpp:3970-3971`) — and its own comment says the window overwrites it "so the day
  decision 1 is revisited this page already says what it wants". **Recommended.**
- **(c) Per card, ephemeral as today.** Costs the same as (b) minus the file, and loses the
  conversation on every restart and every LRU eviction, which is the re-seeding this card is
  trying to stop paying for. Rejected.

**What (b) costs in the worker, measured rather than guessed.** `backend/worker.py` holds one
`Agent` (`:337`) and one `TurnSupervisor` (`:105`) — confirmed. `board_turns.CardTurns` already
holds up to six further `Agent`s, each with its own `BoardTools`, `cancel_event` and thread
(`board_turns.py:230-245`). So the per-card `Agent` is **not new**: it exists today and this card
keeps it. What is new is one `TurnSupervisor` per *live* card session — a `deque` and one daemon
thread each (`queue.py:48-68`) — created with the session and dropped with it by the same LRU.
Six sessions is six extra threads at the ceiling, against the dozen a tab of panes already runs.
The live check measures it (Verify, item 8).

**Why the tab id is in the key.** A tab owns one worker and that worker owns its conversation
files. Keying a card's conversation by the card alone would let two tabs on the same project adopt
the same session file from two workers, and the last one to save would win. `<tab id>/card:<ID>`
keeps "one worker, its own files" true and still gives the card its own memory. Two tabs showing
one card are then two conversations about one card — which is what two tabs showing one *pane's*
project already are.

*If the owner prefers (a)*: only step 2 changes (one supervisor, `surface` ignored for routing),
and #DR4K's card should be reopened in the same commit, because it would be undone.

### 2. What the thread gets, and who writes it — **recommendation: the worker, unchanged**

The bytes that must not move (`backend/relay_core/board.py:878`, `board_protocol.py:718`, and the
live evidence at `docs/qa_evidence/2026-09-21-agents-are-consoles/rerun-card/thread-DYNH-after.md`):

```
<!-- relay:entry 20260921T060614Z-pm author=owner kind=comment mode=discuss -->
say hello to the card

<!-- relay:entry 20260921T060614Z-r8 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260921T060615Z-k7 author=agent kind=comment mode=discuss model=stub turn=<session>/<turn> -->
Hello from the tab's one agent.
```

Attribute order is insertion order (`board.py:1217`), there is no heading line, and
`relay-board.py check` validates the entry id, the `kind` and the ordering but not `author`,
`mode`, `model` or `turn` (`board.py:1285`) — so drift would be silent, which is why the byte
check is in Verify rather than left to `check`.

**The worker keeps writing both ends.** Not `CardContext::turnFinished`, for four reasons, and the
remote design is the one that settles it:

1. **A phone has no `CardContext`.** A device sends `board_ask {id, text, mode}` and
   `board_cancel {id}` (`remote/board_state.py:87-88`) and sees the turn only as the two
   `board_thread_appended` events (`docs/REMOTE-PROTOCOL.md` §17.4: *"the phone sees the question
   land and the answer land, both as `board_thread_appended`"*). If the GUI wrote the thread, a
   turn a phone started would write nothing.
2. **A turn can end with no GUI attached.** A card session outlives the page that opened it (the
   LRU), and the worker outlives a closed card.
3. **The write needs the board's lock.** `append_to_thread` takes an exclusive `flock` on the
   threads *directory* and rewrites the file atomically (`board.py:963-1001`, #N5JJ). The GUI
   holds no such lock, and `issues/threads/<ID>.md` is a file other sessions and `merge=union`
   also write.
4. **The answer needs `strip_tool_fragments` first** (#VN69, `board_turns.py:275-278`): a provider
   that broke a tool call streams the fragment as content, and it must be cut before the thread
   write. That stripper is the worker's.

So `CardContext::turnFinished` stays what it already is — a `board_card_get` refresh
(`src/BoardPane.cpp:4974-4984`).

**When the owner's entry is written.** Today it goes in at `board_ask` time, before the model sees
it (`board_protocol.py:1834`), and `tests/test_board_protocol.py:601` pins that. With a queue on
the card, "submit" and "runs" stop being the same moment. **Keep the write at submit**: the owner
did ask, the record should say so, and a queued prompt that is withdrawn leaves a question with no
answer — which is exactly what a failed turn already leaves
(`test_a_failed_turn_writes_nothing`, `:679`). The alternative (write at turn start) was rejected
because it delays the `board_thread_appended` that answers a phone's `board_ask`.

### 3. Where the per-turn constraint lives — **recommendation: refuse at call time, never narrow the list**

`_build_card_agent` gives a card turn a whole agent whose `tool_scope` is `"card"`, and
`Agent.tools()` branches on it (`agent.py:1087-1100`) to offer the mode's list and nothing else.
That branch is what has to go, because a console holds one list and a card console is a console.

The precedent is already in the tree and is §33's own: **`readonly`**. `ask {readonly: true}` rides
on the queue item (`queue.py:131`, `:529-549`), the supervisor calls `agent.set_readonly(True)`
around the ask and `False` after, the board refuses its write tools with `board_readonly_turn`
(`board_tools.py:1601`) and `_prepare` refuses the executor's (`READONLY_BLOCKED`,
`agent.py:533`) — and §33.2 says in as many words: *"The tool **list** is unchanged."*

A card turn gets the same shape: `Agent.set_card_turn(mode, card_id)` and `set_card_turn(None,
None)`, called from the same two lines of `queue.py` that call `set_readonly`. It sets
`BoardTools.begin_card_turn(mode, card_id)` / `end_card_turn()` — which already exist
(`board_tools.py:1511`, `:1518`) — so `_check_card_scope` (`:1543`) and `CardScope.allows` /
`CardScope.refusal` (`:977-999`) keep working **untouched**, and `tests/test_board_tools.py:2255-2300`
stays green without an edit. Beside them a `CARD_BLOCKED` frozenset in `agent.py`, next to
`READONLY_BLOCKED`, refuses the executor's writers in `_prepare` with `CardScope.refusal`'s
sentence, which already names Execute.

**Why not go on narrowing the list.** Two reasons, and the second is the one that decides it:

- *Cache.* On the Local tier a change to the tool list re-prefills the whole request — the chat
  template renders the tools before the system prompt — and `agent.py:1120-1126` records it
  costing 13–14 s. Today a card conversation that goes Discuss → Plan → Discuss re-prefills on
  every switch, because `CARD_MODE_BOARD_TOOLS` differs per mode. Refusing at call time makes a
  card's prefix byte-identical to an ordinary console's, for every turn, in both directions.
- *The rule the owner set.* A context specialises an agent without fencing it. A per-surface list
  is the fence; a per-turn refusal with a sentence that says why is the constraint. 19.20 is a
  rule about the *stage*, and a stage rule belongs on the turn.

The cost is honest and should be said: a Plan turn is *offered* `run_command` and `write_file` and
refused if it calls them. That is what a `readonly` turn already is, the Plan brief
(`backend/relay_core/board_plan_brief.md`) already says what a Plan may do, and the refusal names
Execute. `tests/test_board_protocol.py:1233`
(`test_a_card_turn_offers_no_commands_no_writes_and_no_subagents`) pins the opposite and is
rewritten in step 3 to assert the new shape: the list is the console's, the call is refused with
`board_mode_refused`.

*If the owner says a Plan must not be offered what it may not call*: step 3 keeps
`CardScope.tool_specs` and the `agent.py` branch, and the plan loses only the cache claim.

### 4. What the card page shows — **recommendation: the transcript streams, the thread settles**

**Today the same bytes are drawn twice.** `RelayWindow::deliverToConsoles` hands every event of a
tab's worker to every console of that tab with **no `surface` filter** at all
(`src/RelayWindow.h:7796-7809`, and the comment says the absence is deliberate), while
`BoardView::handleEvent` also renders the card-tagged `delta`, `thinking_delta`, `status`,
`tool_started` and `tool_result` into the card's thread view (`src/BoardPane.cpp:6473-6534`). So a
Discuss turn's text appears in the thread view *and* in the transcript of every console in the tab
— including Options' — and neither drawing is the pane's: the thread view collapses every tool
call to one elided progress line (`turnProgressLine`, fed to `m_busyWhat`) and shows thinking as a
block it seals, not as the fold the terminal draws.

So:

- **The console's transcript is the live view.** Bubbles, tool rows, the queue strip, Esc — the
  pane's, unchanged, because the turn is an ordinary one.
- **The thread view shows settled entries only.** `board_thread_appended` → `appendEntry`
  (`src/BoardPane.cpp:2532`) stays; `appendDelta`, `appendThinking`, `finishThinking`,
  `sealThinking` (`:2545-2586`) and `render()`'s live tail (`:3680-3705`) go. No double display.
- **`setTranscriptHiddenUntilUsed(true)` on the card console goes** (`:4846-4847`): the transcript
  is now where the turn is, so hiding it until a byte arrives is only a flicker on the first turn.
- **A card's events reach that card's console and no other.** One predicate in
  `deliverToConsoles`: an event whose `surface` starts with `card:` goes only to the console whose
  `spec().surface` matches; everything else broadcasts as today. #AGNT decision 1 anticipated
  this — *"a filter is one predicate to add back if he dislikes it"* — and this is the one place
  it is needed, because under question 1 a card is genuinely a different conversation.
- **The busy strip stays.** It is the board's, it names the mode and it carries the ✕ the owner
  asked for (#VZ69: *"'stop' button isnt intuitive, it should be stop planning i guess, or there
  should be an X next to 'agent planning'"*), and keeping it is what lets this whole card land
  without opening `src/Pane.h`. The console's own busy line speaks for queued and running, as it
  does in every other console. Its progress line (`m_busyWhat`) loses its reason to exist once the
  tool rows are in the transcript, so it goes and the strip is label + ✕.
- **A long history.** The thread is the record and is drawn in full, as now. The transcript is this
  console's session: after a restart it is empty and the thread is complete — the same bargain a
  terminal pane makes with its scrollback. The *conversation* behind it persists (question 1), so
  the model still remembers; a second tab on the same card is a second conversation about one card
  and says so by having an empty transcript of its own.

### 5. Queueing, concurrency and Stop — **recommendation: the card's own queue, no cap**

- **A second Enter while a Discuss runs queues**, in the §12 strip. That is the Issue #AGNT was
  filed for, arriving on the last surface that did not have it. It replaces today's `board_busy`
  refusal for the *same card* (`board_protocol.py:1826-1861`).
- **Plan pressed during a Discuss** queues as a `plan` item: the mode rides on the queue item
  beside `readonly`, so the brief and the constraint are the queued turn's, not the running one's.
- **Two cards in one tab** run at once, as they do today: two sessions, two supervisors, no cap
  (#0Z13). **Two cards in two tabs** are two workers and were never related.
- **Stop is per card.** `board_cancel {card}` stays the verb (the phone's only one) and cancels
  that card's supervisor; the card console's Esc sends it, and stopping #A leaves #B running —
  `test_board_cancel_stops_one_card_and_leaves_the_other_running`
  (`tests/test_board_protocol.py:761`) must go on passing.
- **What stays refused.** A cleanup while anything runs and anything while a cleanup runs (it
  rewrites the cards being talked about), and a board **console** turn holds the board because it
  can write any card (§19.18, `board_protocol.py:1838-1846`). Neither is a card-turn rule and
  neither changes.
- The removed option "how many cards the agent works at once" (`board/max_card_turns`, added in
  `1c4f41a9`, removed in `7edbb09e` by #0Z13) is **not** reinstated. A stale key from an old GUI
  is still ignored (`tests/test_board_protocol.py:698`).

### 6. `board_ask` stays the wire verb — **this plan's own decision, said plainly**

The question was scoped as "the card's console sends the turn like any other", which reads as
`ask {surface: "card:<ID>", mode}`. This plan does **not** do that, and the reason is the phone:
`board_ask {id, text, mode}` is a device's only way to discuss a card
(`remote/board_state.py:87`), the GUI already sends exactly that from the console's own composer
through `CardContext::submit` (`src/BoardPane.cpp:3987`, `:4469-4474`), and `board_ask` is where
the thread write and the stage advance happen *before the model sees the words*. Changing the verb
would mean a compatibility shim for one release, a second path to the same two writes, and a
rewrite of remote §17.3/§17.4 — for no behaviour the owner asked for.

So the verb is unchanged and **what changes is behind it**: `board_ask` submits to that card's
`TurnSupervisor` instead of `CardTurns.start`'s bare thread. Everything the owner sees — bubbles,
tool rows, the queue strip, Esc — follows from the turn being an ordinary supervised turn, not
from the message it arrived in. `queue_remove`, `queue_move`, `queue_steer` and `queue_unsteer`
gain a `surface` so the strip on a card operates that card's queue; that is the whole protocol
delta.

*If the owner wants the verb changed anyway*, it is additive: `ask {surface, mode, card}` routed
to the same place, with `board_ask` kept as its alias for the phone. It changes step 2 only.

## Plan

### Goal

Delete the second turn runner. A Discuss or Plan on a card becomes an ordinary supervised turn —
the pane's queue, steers, request ledger, thinking bubbles, tool rows, Activity and Esc — running
on that card's own conversation, drawn by the card console's transcript, with the worker still
writing `issues/threads/<ID>.md` at both ends with its provenance and still advancing the stage.
`board_protocol._build_card_agent`'s special options and `Agent.tools()`'s `card` branch retire;
`board_turns.py` becomes the per-surface session registry it already half is.

### Principle

An agent is the prompt box (#AGNT). A context says what the agent is about and constrains a
*turn*; it does not own a runner. A card is the last surface in Relay that still owns one.

### Findings

**The card turn's own path, end to end.**

- `board_protocol._ask` — `backend/relay_core/board_protocol.py:1809-1864`. Validates `mode`
  against `CARD_MODES = ("discuss", "plan")` (`board_tools.py:847` — **Execute and Verify are not
  card turns**), refuses on busy, appends the owner's entry (`:1834`), advances the stage
  (`:1840`), decides seeding from the card's file hash (`:1847-1856`), builds the prompt from the
  mode's brief (`mode_prompt`, `:2121`), emits `board_thread_appended`, and calls
  `self.cards.start(...)`.
- `board_turns.CardTurns.start` — `backend/relay_core/board_turns.py:143-180`. Emits
  `agent_started {id, turn_id, card_id, mode, surface: "card:<ID>"}`, opens the scope with
  `tools.begin_card_turn(mode, card_id)`, and runs `agent.ask(...)` on a bare daemon thread
  (`_run`, `:213-241`). `_observe` (`:243-281`) collects the text, inserts the paragraph break a
  tool call needs, tags `CARD_TAGGED` events with `card_id` and `surface`, tags `MODE_TAGGED` with
  `mode`, strips tool fragments (#VN69) and calls `on_answer`. `agent_finished {id, outcome,
  card_id, mode, surface}` closes it.
- `board_protocol._build_card_agent` — `:643-691`. One `Agent` per card, from the pane agent's
  provider config, skills, roles, effort, app tools, policy and instructions; its own
  `BoardTools`, `cancel_event` and conversation; and, the part this card removes,
  `tool_scope="card"`, `track_requests=False`, `todo_tool=False`, `completion_check=False`.
- `board_protocol._card_answer` — `:709-733`. `append_thread(author="agent", kind="comment",
  mode=, model=, turn="<session>/<turn>")`, then `stage_advance(card, "plan-written")` for a Plan,
  then `board_thread_appended`.
- `_busy_error` — `:1826-1861`: same card refused; anything refused while a cleanup runs; anything
  refused while the **console's** turn runs.

**The ordinary console turn's path, for comparison.**

- `backend/worker.py:105` one `TurnSupervisor`; `:337` one `Agent`; `:524-530` `turns.submit(...,
  surface=, screen=, readonly=)`.
- `queue.py:129-215` submit and queue; `:344-374` queue rows; `:520-560` the run, where
  `set_readonly(True)` / `(False)` bracket `agent.ask` — **the two lines `set_card_turn` joins**.
- `queue.TurnSupervisor.agent_emit` (`:72-79`) tags every event of the running turn with
  `self._surface`, which is what makes `surface` provenance rather than a filter.

**The GUI.**

- `CardContext` — `src/BoardPane.cpp:3946-4012`. `spec()` already says `surface = "card:<ID>"`
  (`:3958`), `persistScope`/`persistKey` = `helper` / `card:<ID>` (`:3970`), `scope = "card"`
  (`:3965`), `shell = false`, `routing = "agent"`. `submit(route, _)` (`:3987`) is Enter /
  Ctrl+Enter / Ctrl+Shift+Enter → `CardDetail::submitFromConsole` (`:2111-2120`) → `board_ask`
  (`:4469-4474`) or `board_comment` for the third chord. `turnFinished` (`:4002`) re-reads the
  card and nothing else (`BoardView::cardTurnFinished`, `:4974-4984`).
- The streaming that has to go — `src/BoardPane.cpp:6473-6534`, gated on
  `m_cardTurns.contains(card_id)`: `thinking_delta` → `appendThinking`, `delta` → `appendDelta`,
  `status`/`tool_started`/`tool_result` → one elided `setProgress` line, terminal events → drop
  the turn and `setBusy(false)`. `struct CardTurn` is `src/BoardPane.h:569-581`.
- The busy strip — `src/BoardPane.cpp:1932-1954`, wording in `setModeTips()` (`:3013-3038`), Stop
  → `board_cancel {card}` (`:4481-4488`).
- `RelayWindow::deliverToConsoles` — `src/RelayWindow.h:7798-7809`, no `surface` filter.
  `TabConsoleContext::spec()` — `:7552-7568` — overwrites `persistScope`/`persistKey` with
  `helper` / `<tab id>` for every console.
- `createAgentConsole` `:7596-7625`, `attachConsoleToTab` `:7760-7776`, `sendFromConsole`
  `:7782-7794` (it drops a console's own `configure`; the window owns the tab worker's).
- **Execute and Verify are not card turns and are not touched.** `BoardView::executeCard`
  (`:7711-7761`) opens a terminal pane with the prompt `board::executeTask` builds
  (`src/BoardModel.cpp:361-418` — a task sentence and the board's conventions, *not* the plan text
  inlined), hands the card id as an `ask {cards:[id]}` attachment
  (`Pane::startBoardTask`, `src/Pane.h:11606-11613`), and claims the card with the pane token
  (#R9G7, `:7733-7735`). `verifyCard` (`:7769+`) is the same shape on the recommended verifier
  (#T71W). Its own comment says it: *"Nothing here is a model turn, so the Switchboard worker
  stays free"* (`:7709`).

**What a card turn needs that a plain turn does not** (#AGNT already itemised this; it is still
the right list): the card id and mode on the request · the owner's words on the thread before the
model sees them · the answer after, stripped, with provenance · a stage advance at both ends · a
re-seed when the card file's hash moves · a per-mode brief · a per-card cancel. Every one of those
is `board_protocol`'s and survives; what does not survive is the *runner* underneath them.

**The remote side, which constrains more than it looks.** `remote/board_state.py:298`
`EVENT_TYPES` drops `delta`, `thinking_*`, `tool_*`, `agent_started` and `agent_finished` on the
board channel, so a phone sees a card turn only as two `board_thread_appended` events plus
`board_cancelled`, `board_busy` and `error {card_id}` (`docs/REMOTE-PROTOCOL.md` §17.4). Nothing
in this plan changes what a phone sees: the verb, the thread writes and `board_cancelled` all
stay.

### Steps

One subagent per step, disjoint file sets. Order: **A** = 1 · **B** = 2, 3 (3 is additive and may
run first or in parallel) · **C** = 4, 5 in parallel · **D** = 6 · **E** = 7. Every step lands
through `python3 scripts/land.py begin <me> <its files>` … `commit`, builds through
`scripts/relay-build`, and runs only its own tests.

**No step opens `src/Pane.h`.** Only step 5 opens `src/RelayWindow.h`, and it is two edits.

---

**Step 1 — A `TurnSupervisor` can be asked for a card turn.**
*Files:* `backend/relay_core/queue.py`, `backend/relay_core/agent.py`, `tests/test_queue.py`.
*Do not touch:* `board_*.py`, `backend/worker.py`, any `src/` file.

1. `TurnSupervisor.submit` gains `mode: str = ""` and `card: str = ""` beside `readonly`,
   validated the way `surface` is (`queue.py:157-159`); they ride on the queue item, on `queued`,
   on the `queue_changed` rows and on the run (`:170`, `:197`, `:344`, `:373`).
2. In `_run` (`queue.py:529-549`), beside the `set_readonly` pair: `set_card_turn = getattr(agent,
   "set_card_turn", None)`; call it with `(mode, card)` before `agent.ask` and `(None, None)`
   after, in the same `try/finally`.
3. `Agent.set_card_turn(mode, card_id)` in `agent.py`, beside `set_readonly` (`:1065-1076`): sets
   `self.card_turn = (mode, card_id) or None`, and `self.board.begin_card_turn(mode, card_id)` /
   `self.board.end_card_turn()` when there is a board. A `CARD_BLOCKED` frozenset beside
   `READONLY_BLOCKED` (`:533`); `_prepare` refuses a blocked name with
   `board_tools.CardScope(mode, card_id).refusal(name)`, which already names Execute.
4. **`Agent.tools()` is not touched in this step**, so nothing changes for today's card agents.

*Tests:* `RELAY_KEYRING=off python3 -m unittest tests.test_queue` plus two new cases — a queued
item carries its mode and card on `queue_changed`; `set_card_turn` is called with the mode before
the ask and cleared after, including when the ask raises.

---

**Step 2 — A card turn runs on a supervisor of its own.**
*Files:* `backend/relay_core/board_turns.py`, `backend/relay_core/board_protocol.py`,
`tests/test_board_turns.py`, `tests/test_board_protocol.py`.
*Do not touch:* `agent.py`, `board_tools.py`, `queue.py`, any `src/` file.

1. `CardSession` gains a `TurnSupervisor` built on the worker's raw emit, and loses `thread`,
   `text`, `_run` and most of `_observe`. The supervisor's own `agent_emit` already tags
   `surface`; `CardTurns` keeps a thin observer that adds `card_id` and `mode` to `CARD_TAGGED` /
   `MODE_TAGGED` (a phone and the board list route by `card_id`, 19.10), keeps the paragraph break
   before a tool call, keeps `strip_tool_fragments` (#VN69) and keeps `on_answer`.
2. `CardTurns.start` becomes `CardTurns.submit(card_id, mode, prompt, request_id, seed_hash)`:
   it finds or builds the session, then `session.turns.submit(prompt, "now", request_id,
   mode=mode, card=card_id, surface=surface_of(card_id))`. The `agent_started` /
   `agent_finished` pair is now the supervisor's, with `card_id` and `mode` added by the observer
   — the same event names and the same fields, which is what
   `TurnBoundaryTests::test_a_turn_is_bracketed_and_every_event_of_it_names_its_surface`
   (`tests/test_board_turns.py:299`) already asserts.
3. `_await_unwinding` and the "a turn is already running" refusal go: a second prompt **queues**.
   `is_running` / `running_cards` read the supervisors' `busy`.
4. `stop(card_id)` cancels that card's supervisor; `stop_all` and `drop` walk them. `MAX_SESSIONS`
   and the LRU stay, and a session with a busy or non-empty queue is never evicted.
5. `board_protocol._ask`: keep the validation, the owner's thread write, the stage advance, the
   seed-hash reseed, the brief and `board_thread_appended` exactly as they are; the only change is
   `self.cards.start(...)` → `self.cards.submit(...)`, and `_busy_error` stops refusing a second
   turn on the *same* card (it goes on refusing everything during a cleanup and during a console
   turn).
6. `_build_card_agent` becomes `_build_card_agent(card_id, emit)` returning the same agent built
   with the **console's** options: drop `tool_scope="card"` (it defaults to `console` via
   `helper`), and let `track_requests`, `todo_tool` and `completion_check` take the ordinary
   values the pane agent has. Rename it `_build_card_console` in the same commit so the diff says
   what happened.
7. Persistence (Planning notes, question 1): the session adopts
   `agent_context.helper_file(workspace, "<tab>/card:<ID>")` when the worker knows its tab
   (`board.set_tab`, `worker.py:236`), so a card's conversation survives a restart.
8. `queue_remove`, `queue_move`, `queue_steer`, `queue_unsteer` and `cancel` gain an optional
   `surface`; when it names a card, the op goes to that card's supervisor. `board_cancel {card}`
   is kept and implemented as that.

*Tests:* `RELAY_KEYRING=off python3 -m unittest tests.test_board_turns tests.test_board_protocol`.
Adapt, naming the new behaviour in the test name rather than deleting the case:
`test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs`
(`test_board_turns.py:133`, `test_board_protocol.py:715`) → *queues*;
`test_the_scope_is_opened_for_the_turn_and_closed_after_it` (`:180`) → the supervisor brackets it;
`ModeTests` (`test_board_protocol.py:1110-1231`) → the brief and the mode ride the queue item.
**Must go on passing unchanged:** `test_the_question_is_recorded_before_the_agent_sees_it`
(`:601`), `test_the_answer_is_appended_to_the_thread_when_the_turn_finishes` (`:665`),
`test_a_failed_turn_writes_nothing` (`:679`), `test_two_cards_are_planned_at_the_same_time`
(`:730`), `test_the_fifth_card_runs_too…` (`:748`),
`test_board_cancel_stops_one_card_and_leaves_the_other_running` (`:761`),
`test_a_leaked_tool_call_never_reaches_the_card_thread` (`test_board_turns.py:157`).
Add: a second prompt on a busy card appears in `queue_changed` with its mode; a Plan queued behind
a Discuss runs with the Plan brief.

---

**Step 3 — The stage rule is a per-turn constraint, and the tool list stops moving.**
*Files:* `backend/relay_core/agent.py` (the `tools()` branch at `:1087-1100` and
`_deferred_groups`' `card_scope` line at `:906-908`), `backend/relay_core/board_tools.py`
(`CardScope.tool_specs` and `BoardTools.tool_specs`' `chat` test),
`backend/relay_core/agent_context.py` (`SCOPES`), `tests/test_board_tools.py`,
`tests/test_agent_context.py`.
*Do not touch:* `queue.py`, `board_turns.py`, `board_protocol.py`, any `src/` file.
*Runs after step 2, because until then agents are still built with `tool_scope="card"`.*

1. Delete the `card_scope` branch of `Agent.tools()` (`:1087-1100`). Every agent takes the one
   list. `_deferred_groups`' card line goes with it: a card turn is a console turn and defers
   nothing because a console defers nothing.
2. `"card"` leaves `agent_context.SCOPES`; a `configure` that still sends it is mapped to
   `console` rather than refused, for one release.
3. `CardScope.tool_specs` is deleted (nothing calls it). `CardScope.allows`, `CardScope.refusal`,
   `_check_card_scope`, `begin_card_turn` and `end_card_turn` are **unchanged**.
4. `BoardTools.tool_specs`' `getattr(self.card_scope, "chat", False)` test is inverted to ask the
   scope directly, so a `CardScope` open on a console's tools does not silently switch the board's
   list mid-conversation (this is the one place the per-turn scope could still move the prefix).

*Tests:* `RELAY_KEYRING=off python3 -m unittest tests.test_board_tools tests.test_agent_context`.
`CardTurnScopeTests` (`tests/test_board_tools.py:2255-2300`) passes **unchanged** — that is the
proof the stage rule did not move. Rewrite
`test_a_card_turn_offers_no_commands_no_writes_and_no_subagents`
(`tests/test_board_protocol.py:1233`, edited here by agreement with step 2's session or left to
step 2 — say which in the thread) as: the list is the console's, `run_command` is refused with
`board_mode_refused`, and the refusal names Execute. Add: the tool list of a Discuss turn, a Plan
turn and an ordinary console turn on one agent are byte-identical.

---

**Step 4 — The card page stops drawing the turn.**
*Files:* `src/BoardPane.h`, `src/BoardPane.cpp`, `tests/boardmodel_test.cpp`.
*Do not touch:* `src/RelayWindow.h`, `src/Pane.h`, any backend file.

1. Delete the streaming half of the card-turn block (`src/BoardPane.cpp:6485-6516`): the
   `thinking_delta`, `thinking_done`, `delta`, `status`, `tool_started` and `tool_result` arms.
   **Keep** the terminal arm (`:6517-6534`) and `m_cardTurns` itself, reduced to `{mode}` — the
   board list's working marker, the Execute/Verify refusal, `cardBusyChanged()` and `onTurnEnded`
   all read it, and `BoardRemote` follows the same events for its own set
   (`src/BoardRemote.cpp:567-580`).
2. Delete `CardDetail::appendDelta`, `appendThinking`, `finishThinking`, `sealThinking`
   (`:2545-2586`), `m_streaming`, `m_thinking*`, and `render()`'s live tail (`:3680-3705`). The
   thread view now renders `m_entries` only; `appendEntry` (`:2532`) is unchanged.
3. `setProgress` and `m_busyWhat` go (`:1932-1954`, `:2618-2630`, `:3013-3038`): the tool rows are
   in the transcript. The strip keeps its label and its ✕ and its #VZ69 wording.
4. `ensureCardConsole` drops `setTranscriptHiddenUntilUsed(true)` (`:4846-4847`).
5. Nothing about `submit`, `board_ask`, `board_cancel`, `cardActions`, Execute, Verify or
   `turnFinished` changes.

*Tests:* `ctest --test-dir build -R '^board$|^boardpane$|^boardexecute$'`. Adapt
`theThinkingTraceRunsInTheCardsThread` (`tests/boardmodel_test.cpp:1802`) into
`theThinkingTraceRunsInTheCardsConsole`; adapt
`theBoxDiscussesAndTheRowPlansOrLeavesTheBoard` (`:2622`) for the strip's lost progress line
(the `board_ask` and `board_cancel` assertions stay);
`aCardKeepsItsOwnTurnWhileAnotherCardIsOnScreen` (`:2729`) keeps its point — two cards running at
once, each with its own state — and loses only the per-card `streamed` text, which now lives in
each card console. `tests/boardexecute_test.cpp` must pass **untouched**.

---

**Step 5 — A card's events reach that card's console, and its conversation is its own.**
*Files:* `src/RelayWindow.h`, `tests/boardworkspace_test.cpp`.
*Do not touch:* `src/BoardPane.*`, `src/Pane.h`, any backend file.
*Two edits. This is the hot file; `begin` immediately before the change, not before the reading.*

1. `deliverToConsoles` (`:7798-7809`): an event whose `surface` begins with `card:` goes only to
   the console whose `contextSpec().surface` equals it; every other event broadcasts exactly as
   today. Four lines, and the comment at `:7795-7798` is rewritten to say why the one exception
   exists (a card is a different conversation — Planning notes, question 1) rather than that there
   is none.
2. `TabConsoleContext::spec()` (`:7557-7558`): keep overwriting `persistScope`/`persistKey` for
   every console **except** one whose host asked for a card key, which becomes
   `helper` / `<tab id>/<host key>`. Three lines.

*Tests:* `ctest --test-dir build -R '^boardworkspace$'`. Add two cases: an event with
`surface: "card:K7Q2"` reaches only that card's console; a card console's `configure` carries
`persist {scope: "helper", key: "<tab>/card:K7Q2"}` while the board console's is the tab's.
`theWindowsWrapperForwardsEveryContextVirtual` must go on passing.

---

**Step 6 — Retirement and the protocol.**
*Files:* `docs/AGENT-SESSIONS-PROTOCOL.md` (§19.10, §19.16, §33.1–33.3), `docs/SWITCHBOARD-DESIGN.md`
§4.9 and §4.13, `docs/ARCHITECTURE.md` (the agents-and-contexts section), `docs/REMOTE-PROTOCOL.md`
(§17.4 — a note that it is deliberately unchanged), `docs/SWITCHBOARD-FORMAT.md:370` (the stale
`O_APPEND` sentence, found on the way, #N5JJ made it a directory `flock`).
*Do not touch:* any code file.
*Runs last, when steps 1–5 have landed.*

§19.10 gains the queue and loses "one turn at a time, per card"; §19.16 keeps the per-card
conversation and says the runner is now a supervisor; §33.2 gains `mode` and `card` beside
`readonly` and repeats the sentence that matters — *the tool list is unchanged*; §33.3's table
drops the `card` row and its third constraint becomes "a card's Plan turn is refused the writers
at call time". `grep -rn "_build_card_agent\|tool_scope=\"card\"\|CardScope.tool_specs" backend
src docs tests` must come back empty but for §33.3's history note.

---

**Step 7 — The live drive.**
*Files:* `docs/qa_evidence/2026-09-2x-card-turns-are-console-turns/` only, plus the card's
`## Tests`, `## Execution Summary` and `## QA checklist`.
The checks are listed under Verify. Nothing else in the repository is edited.

### Risks

1. **The stage machine is the board's core and must not regress.** It is three events
   (`STAGE_MOVES`, `board_tools.py:3519`) called from three places
   (`board_protocol.py:1840`, `:726`, `board_tools.py:2532`), and this card moves none of them.
   The mitigation is that steps 2 and 3 are forbidden to edit `stage_advance` or `STAGE_MOVES`,
   and `test_the_question_is_recorded_before_the_agent_sees_it` is on the must-pass-unchanged
   list.
2. **Thread-format drift is silent.** `relay-board.py check` does not validate `author`, `mode`,
   `model` or `turn` (`board.py:1285`), so a dropped attribute would pass every check and be found
   months later on a card nobody could re-run. Mitigation: the live drive diffs a real thread file
   against the byte template in Planning notes, question 2, and the diff goes in the evidence.
3. **Prompt-cache churn.** The whole of step 3 is about not causing any; the failure mode is the
   opposite one — a `CardScope` left open across a `tools()` call moving the board's half of the
   list. Step 3 item 4 is that hole, and the added test (three turns, byte-identical lists) is the
   gate.
4. **Conversation growth.** A Plan turn reads the repository for minutes and its tool trace now
   lives in that card's conversation rather than being thrown away. Mitigation: it is per card
   (question 1), so it never reaches anyone else's; compaction applies as it does to a pane; the
   LRU still caps how many are live. Measure the size of a card's session file after a Plan in the
   live drive.
5. **A card turn running with no GUI attached.** Unchanged and deliberate: the worker writes the
   thread (question 2), the session outlives the page, and `board_thread_appended` is what tells
   whoever comes back. The new part is that a *queued* card prompt is worker-side state that no
   file records; say so in §19.10.
6. **N supervisors is N threads.** Six at the LRU ceiling. Counted in the live drive; if it is
   ever a problem the supervisor is what to make lazy, not the session.
7. **`deliverToConsoles` filtering could hide something.** Only `surface: "card:*"` is filtered,
   and only card turns carry it. A board console that needs a card's event gets it as
   `board_thread_appended`, which is not surface-tagged.
8. **`src/Pane.h` is untouched, and must stay that way.** Every temptation to add a card mode to
   the console's busy line ends there. The busy strip staying on the card page (question 4) is
   what buys that.

### Verify

**Per step**, the tests named in that step and nothing more (WARP.md: the suites are the owner's).

**End to end**, once steps 1–6 have landed:

- `ctest --test-dir build -R '^board$|^boardpane$|^boardexecute$|^boardworkspace$|^agentcontext$'`
- `RELAY_KEYRING=off python3 -m unittest tests.test_board_turns tests.test_board_protocol
  tests.test_board_tools tests.test_queue tests.test_agent_context`
- `python3 scripts/relay-board.py check`

**Live under Xvfb**, isolated `XDG_CONFIG_HOME` / `XDG_RUNTIME_DIR` / `TMPDIR` under a short path,
`RELAY_KEYRING=off`, against a **copy** of a fixture board, evidence in
`docs/qa_evidence/2026-09-2x-card-turns-are-console-turns/`:

1. **A Discuss with a thinking bubble and a tool row in the card console.** The answer streams in
   the transcript with a fold that opens and closes and at least one tool-call row with its OSC 8
   fold — the Issue, on a card. The thread view shows **no** live text while it runs and the
   settled entry after.
2. **A queued second prompt.** Enter twice on one card: the second appears in the §12 queue strip
   with its row, can be steered, withdrawn and reordered, and runs when the first finishes. Today
   it is refused with `board_busy`.
3. **Plan refused a file write, in a sentence.** A Plan turn that calls `write_file` is refused
   with the `CardScope.refusal` text naming Execute, and the turn carries on. Screenshot the row.
4. **The thread entry is byte-compatible.** `diff` the card's thread file against the template in
   Planning notes question 2: the owner's entry (`author kind mode`), the stage event
   (`author=owner kind=event pane=switchboard`, "Inbox → Discussing · the discussion started") and
   the agent's entry (`author=agent kind=comment mode= model= turn=<session>/<turn>`), attributes
   in that order, no heading line. The diff file goes in the evidence.
5. **Two cards in two tabs, and two cards in one tab.** Both pairs stream at once; Stop on one
   leaves the other running; each card console shows only its own turn (the `deliverToConsoles`
   predicate) and Options' console shows neither.
6. **Restart.** Quit with a card discussed, start again, open the card: the thread is complete, the
   transcript is empty, and a follow-up Discuss shows the model still remembers the earlier turn
   (question 1's persistence).
7. **Execute and Verify still leave the board.** `x` on a planned card opens a terminal pane with
   the task text, claims the card with its `pane_token`, and the claim chip reveals the pane
   (#R9G7). No card turn is started. `v` in a QA lane, the same.
8. **The numbers.** `relay.log`'s `pane_usage` and the worker's thread count for a tab with two
   card consoles and the board console open (Risks 4 and 6), and the size of one card's session
   file after a Plan.
9. **The phone is unchanged.** Options › Remote on: a device discusses a card, sees the question
   and the answer land as thread entries and nothing else, and its Stop puts the lamp out
   (`board_cancelled`, #SWPH's bug).

## Execution Summary

Seven steps, in the order the plan set: 1–3 the backend, 4–5 the GUI, 6 the docs, 7 the live
drive and what it found.

- **Step 1** (`8f5322a9`) — a `TurnSupervisor` can be asked for a card turn: `mode` and `card`
  ride beside `surface`, `screen` and `readonly`, every queue row and the boundary pair carry
  them, and `Agent.set_card_turn` opens the `CardScope` that already existed.
- **Step 2** (`1b9571ce`) — a supervisor per card. A second prompt on a busy card queues instead
  of being refused; the queue ops, `cancel` and `resume_queue` are addressed by `surface`; every
  event of a card's turn carries `surface: "card:<ID>"`. `ab0834e5` made the queue row show the
  owner's words rather than the model's prompt.
- **Step 3** (`cb866888`) — one tool list. `Agent.tools()`'s `card_scope` branch and
  `CardScope.tool_specs` are gone, `"card"` has left `SCOPES`, and a Discuss, a Plan and an
  ordinary console turn are offered byte-identical tools. `4951b5b1` fixed what that uncovered: a
  console kept a console's board tools when a project arrived mid-conversation.
- **Step 4** (`21660474`) — the card page stops drawing the turn: the thread view's live tail and
  its four `append*` methods are gone, and the busy strip keeps #VZ69's wording and its ✕ minus
  the progress line.
- **Step 5** (`d9577277`) — `deliverToConsoles` routes by `surface`, and a card console's
  conversation is keyed `<tab id>/card:<ID>`.
- **Step 6** (`cd75da79`) — the protocol and the architecture docs say a card turn is an ordinary
  console turn.
- **Step 7** — the live drives. `dc083b31` is what the first one found (the page was still
  refusing the second prompt; the card's console had no size); `7a5d7f27` and `acda4552` are what
  the second one needed, below.

**The final pass**, which is the one step allowed to open `src/Pane.h`:

- **The §12 queue strip reads the worker's queue** (`7a5d7f27`). One rule for every pane: the
  worker's list for this console's surface is the truth, and the pane's own pending items are the
  optimistic overlay in front of it. A queued card prompt has a row with the owner's words; ↑
  selects it, Ctrl+↑↓ moves it, Shift+Delete removes it and Enter on an edited one withdraws it
  and asks again — every op naming the card's own queue. **Esc** does too, which it did not: an
  untagged `cancel` from a card console stopped the *tab's* turn.
- **One console, several cards** (`7a5d7f27`). The card page still keeps one console — one vterm
  per open card is the cost Risk 4 named — and hands its transcript over with the card:
  `ConsoleHandle::clearTranscript` banks what is on screen under the surface it was printed for,
  resets the emulator, the scrollback and the fold ledger, and draws that card's back.
- **A use-after-free at quit** (`7a5d7f27`). `m_consoles` owns the console contexts and is an
  ordinary member, so it was destroyed before `~QWidget` deleted the console panes: on a SIGTERM
  quit `~Pane` wrote into a freed wrapper. This is the segfault the steps 4-5 drive saw once and
  could not reproduce.
- **What the second drive found** (`acda4552`): the "▸ running" line said nothing on a card, and
  the transcript coming back said "this shell is new" on a surface with no shell.

## Tests

- `tests/test_queue.py`, `test_board_turns.py`, `test_board_protocol.py`, `test_board_tools.py`,
  `test_agent_context.py`, `test_agent.py`, `test_board_chat.py`, `test_app_tools.py`,
  `test_roles.py` — **718 tests, green** on a clean `git archive` export (steps 1-3).
- `ctest -R '^consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit$'`
  — green. `consolemode` gained four cases for the strip (a `queue_changed` for this surface
  draws rows the console never submitted and skips another console's; a terminal pane draws none
  of them; remove and move carry the surface, and so does Esc; a line this console sent comes
  back as an editable row). `board` gained the card-to-card transcript hand-over.
- `boardexecute` fails 3 of 5 exactly as #48S3's thread records, unchanged by any commit here.

### Check 2026-09-23 19:23
- not-applicable · unittest:test_roles — test_roles.py is not in the project any more
- missing-evidence · ctest:consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit — no run of ctest -R consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit for this revision, from any host, and no attached result
- notice · unittest:test_roles — test_roles.py is not in the project any more
- notice · ctest:consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit — ctest -R consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit: 2 of 8 are slow (consolemode, board)
- notice · ctest:consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit — ctest -R consolemode$|^board$|^boardworkspace$|^boardpane$|^boardsections$|^boardfilter$|^queuenav$|^queuesubmit: 1 of 8 never ran here (boardfilter)
history: thread
## QA checklist

The evidence is three drives: `docs/qa_evidence/2026-09-21-card-turns-backend` (the wire, end to
end), `-console` (the GUI, steps 4-5) and `-final` (the queue strip, the transcript hand-over and
the restart). What a verifier should check by hand, because none of the three could:

1. **A real provider.** Every drive above runs against a loopback stub. Discuss and Plan a card
   on a real model: the reasoning fold, the tool rows and the answer in the card's console, the
   thread entry with `mode=`/`model=`/`turn=`, and a Plan's `write_file` refused in the sentence
   that names Execute.
2. **The phone's `board_ask`.** A paired device discusses a card while the card page is open
   here: its question and the answer land as thread entries, its prompt appears as a row in
   **this** console's §12 strip with the owner's words (the row is drawn from the worker's queue,
   whoever filled it), and the "▸ running" line names it.
3. **A long queue under load.** Five or six prompts queued on one card while another card works:
   the rows stay in delivery order, Ctrl+↑↓ reorders them through `queue_move`, Shift+Delete
   removes the right one, Clear empties that card's queue and not the tab's, and the turns run in
   the order the strip showed. ↑ is #QRC1's since `3ebf3673`: on the head row it takes the prompt
   back as an unsent draft rather than selecting it, and a row this console did not send keeps
   the selection instead, because its text is the worker's 120-character preview.
4. **Two tabs on one card.** Open the same card in two tabs and ask in both: two conversations
   (`<tab id>/card:<ID>`), two queues, and neither strip shows the other's rows.
5. **An open question for the owner, not a check.** A device's Stop sends `board_cancel`, which
   cancels the card's supervisor — and cancelling *pauses* that card's queue. A device has no
   `resume_queue` (§17.1), so anything queued behind a turn a phone stopped waits until somebody
   resumes it from the desk. Should a device's Stop clear the card's queue instead of pausing it?
   That is a product decision and nothing here assumes an answer.
