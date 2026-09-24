---
id: 6W9X
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 6b0b0195-6ae5-4b28-8675-7ed6034dd95f
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Claude Code guest session in Relay, 2026-09-23
links: {plans: [], commits: [d77495b3, a532eafe], evidence: [docs/qa_evidence/2026-09-23-refine-6W9X/], related: [XS6Q, WC3E, CTRN, AGNT], github: null}
---
# Refine: a card action that checks the request before anyone plans it

## Issue
potential feature -- in the switchboard, add a feedback buttton along with plan, where relay will analyze the card or the plan and see if there are ways to improve it, eg for bugs, find similar bugs, for  features, try to imptove them. is this helpful or is it redundant with plan?

actually call it "refine" rather than "feedback"

great, lets plan that and put on a card

## Done means
A card's action row has a **Refine (f)** button beside Plan. Pressing it runs one card turn that searches the whole board (open *and* closed cards, and threads) and the code, then leaves on the card: `links.related` pointing at siblings and prior fixes, corrected labels, a `## Done means` if the card had none, and one thread comment that names the duplicates or the fixed-before card, states the ask more sharply, and asks at most three questions. It never touches `## Issue`, `## Plan`, the title, the status or another card, and the tools refuse it if it tries.

Failure looks like: a Refine that rewrites the issue or writes a plan; a Refine on a card that is a near-duplicate of a done card and does not say so; or the Plan button's behaviour changing at all.

## Plan
### Goal

A third card mode, **Refine**, beside Discuss and Plan: one `board_ask {mode: "refine"}` turn that checks the *request* rather than planning the work. It searches the whole board including closed cards, checks main for a fix that already landed, sharpens the ask, and writes only `links.related`, labels, `## Done means` and a thread comment. Plan answers "how do we do this"; Refine answers "is this the right card". It is the on-demand, per-card version of the intake chores BOARD-DESIGN already contemplates (duplicate, label and title suggestions).

### Findings

- **Modes are a closed tuple with one tool table.** `backend/relay_core/board_tools.py`: `CARD_MODES = ("discuss", "plan")`, `CARD_MODE_BOARD_TOOLS` lists each mode's board tools, `CardScope.allows`/`refusal` gate calls, and `_check_card_scope` (line ~1630) is where Plan is held to `## Plan` + `## Done means` on its own card. `card_brief(mode)` loads `board_{mode}_brief.md` from beside the policy.
- **The ask handler is mode-generic except for four spots.** `backend/relay_core/board_protocol.py` `_ask` (~2058): `mode not in CARD_MODES`; the wordless default (`text is None and mode == "plan"` → "Plan this card."); the stage move (`plan-started` vs `discussed`); and the seeded/brief_mode shortcut for Discuss. `mode_prompt` (~2407) builds `[Plan · #ID]` / `[Discuss · #ID]` heads. `_turn_phrase` (~247) names the running turn in refusals. The turn-end hook (~825) advances `plan-written` for Plan only.
- **Stage moves are a table.** `STAGE_MOVES` in board_tools.py (~3685): `discussed` moves inbox→discussing, `plan-started`→planning, `plan-written`→planned. Refine should *not* move the card: it is a check, not a stage.
- **`links` is a writable front-matter field** (`B.ALLOWED_FIELDS` includes `links`, board.py:218), but writing it replaces the whole object, so the scope must confirm that only `related` changed.
- **The GUI builds the row from `CardDetail::cardActions()`** in `src/BoardPane.cpp` (~3092): three `relay::agent::Action`s (Plan `p`, Execute `r`, Verify `v`, and Try it elsewhere), each with `key`, `letter`, `label`, `tooltip`, `run`. `plan()` (~2579) does `takeReply()` then `onReply(text, "plan")`; the submit lambda (~4905) sends `board_ask` with `mode` and records the running mode in `m_cardTurns` for the busy strip. `setModeTips()` (~3265) labels the strip from `m_busyMode == "plan"` only (planning… / discussing…). Keys: card view (~3013) and list (~8947) switch on `p`/`r`/`v` → `plan()` / `cardAction(...)`. The letter `f` is unused in BoardPane.cpp.
- **Mode labels are string switches.** `board::modeTitle` in `src/BoardModel.cpp` (~338) maps discuss/plan/execute for the thread's author line; `src/BoardRemote.cpp` (~549) labels a phone's `board_ask` as Plan or Discuss.
- **Protocol 19.10** (`docs/AGENT-SESSIONS-PROTOCOL.md` ~3250) holds the mode table; BOARD-DESIGN 4.9/4.12 describe the buttons and keys.
- **Tests that pin the current shape.** `tests/test_board_protocol.py` `ModeTests` (~1262: brief per mode, wordless Plan, scope opened/closed); `tests/test_board_tools.py` (~2290–2330 and ~3025: Plan refusals, brief content); `tests/boardremote_test.cpp` (~201: phone `board_ask` mapping).

### Steps

1. **The brief.** Add `backend/relay_core/board_refine_brief.md` (v1, this card), same header comment style as the Plan brief. It says: read the card and its thread; `board_list` with two or three queries from the request's own words, *including* `status: done` and `dropped` and the QA lanes; `board_read` the candidates; `search_files`/`read_file` the code the card names, and look for the fix already on main. Then write, in this order: `links.related` for real siblings (not same-area cards); a plainly wrong or missing `bug`/`feature`/area label; `## Done means` only if the card has none (revise nothing that exists); and one `board_comment` kind `note` with three short parts — *Same as / fixed before* (ids and why, or "none found"), *The ask, sharper* (a one-paragraph restatement the owner may paste into the issue themselves), *Questions* (up to three, or none). Bugs: is it already fixed, is the repro missing, is it a regression of a done card. Features: what already covers it, the smaller first version, what the ask leaves out. Explicit "never": `## Issue`, `## Plan`, title, status, other cards. Reply in two sentences.
2. **Backend mode.** `board_tools.py`: `CARD_MODES += "refine"`; `CARD_MODE_BOARD_TOOLS["refine"] = ("board_list", "board_read", "board_update_card", "board_comment")`. Extend `_check_card_scope` so `refine` (like `plan`) writes only its own card, and for `board_update_card` allows: `replace_section`/`append_section` with heading `Done means` only; `fields` with keys ⊆ {`labels`, `links`}, where a `links` value must equal the card's current `links` except `related`. Refusal text names Discuss for the issue and Plan for the plan. `CardScope.refusal` gets the mode name ("a Refine turn"). `card_brief("refine")` needs no change.
3. **Backend ask.** `board_protocol.py`: wordless default for `refine` ("Refine this card."); `mode_prompt` label `Refine`, head `[Refine · #ID]`, owner text appended as "The owner adds, verbatim:" like Plan; `_turn_phrase` → "a refinement"; `_busy_error` phrase; **no** `stage_advance` on start or end (a card in inbox stays in inbox — the point is that Refine runs before anything is decided). `board_turns.MODE_TAGGED` and the `mode` attribute on entries need nothing: they carry whatever string the ask had.
4. **Backend guest bridge.** Check `guest_board_bridge.py` / `guest_harness*.py` for any `mode in ("discuss", "plan")` literal and route through `CARD_MODES` instead, so a Codex/Claude guest driving the board gets the same mode.
5. **GUI action.** `src/BoardPane.cpp`: in `cardActions()` add a `refine` Action after Plan — `key` `boardRefine`, `letter` `f`, label `Refine`, tooltip "The agent checks the request: finds the same or a fixed-before card, sharpens the ask, fills related links and Done means; it changes no issue text, no plan and no code (f)". Offered while a turn runs, like Plan (it queues). Add `void refine()` beside `plan()` sending `onReply(text, "refine")`. Card-view key handler (~3013) and list handler (~8947): `f` → `refine()` / `cardAction("refine")`. `onModeHint` hint id `board.refine`, letter `f` (WARP.md hint rule). `setModeTips()`: strip reads `✦ Switchboarding · refining…` / `✕ Stop refining` for `m_busyMode == "refine"`. The `## Done means` and `links.related` the turn writes already reach the page through `board_written`, so nothing new is drawn.
6. **GUI labels.** `src/BoardModel.cpp` `modeTitle`: `refine` → `Refine`. `src/BoardRemote.cpp` (~549): label from the mode string via `modeTitle` rather than the two-way ternary, so a phone's Refine reads as Refine.
7. **Docs.** Protocol 19.10: add the Refine column to the mode table and a paragraph on what it writes and why it makes no stage move. BOARD-DESIGN: new 4.15 "Refine: check the request before the plan (#6W9X)" with the owner's words from this card, the write set, and the key; amend 4.12's list of row buttons and the empty-thread line that teaches the keys (`p`, `r`, `v` → add `f`).
8. **Evidence.** `docs/qa_evidence/2026-09-23-refine-6W9X/`: a run on a real intake bug card that is a near-duplicate of a done card, showing the thread comment and the `links.related` write; the refusal transcript for an attempted `## Issue` rewrite.

### Risks

- **Four buttons on the row.** Plan, Refine, Run, Verify (plus Try it in a QA lane). Refine sits *before* Plan in reading order since it comes first in the flow; if the row feels crowded the fallback is the cheaper version noted in the discussion: fold the whole-board search into Plan brief step 1 and skip the button. Owner to decide after seeing it.
- **`Done means` written by two modes.** Plan already writes it. Refine writes it only when absent, and Plan's rule ("revised, not replaced") already covers the overlap. If that reads as two cooks, Refine drops `Done means` and keeps the rest.
- **Owner decision: may Refine relabel?** Labels are a filter the pane draws (cleanup brief step 5). The plan says yes, within the board's existing vocabulary only, never a new word. Say if it should only suggest.
- **Key `f`.** Free in BoardPane.cpp today; if the console's own action letters ever claim it the list handler's "letters this page spends never reach the row" rule keeps the card's meaning.
- **`links` replacement.** The scope check in step 2 is what stops a model dropping `links.commits` by writing a partial object. Test it explicitly.

### Verify

- `tests/test_board_tools.py`: Refine may write `Done means`, `labels`, `links.related` on its own card; is refused for `Issue`, `Plan`, title, another card, `board_move_card`, `board_create_card`, and for a `links` object that changes anything but `related`; `card_brief("refine")` mentions `done`, `dropped`, `Done means` and `Issue` (as a never).
- `tests/test_board_protocol.py` `ModeTests`: a wordless Refine records "Refine this card.", prompt carries `[Refine · #ID]` and the brief, scope mode is `refine`, and the card's status is unchanged after start and after the turn ends (no stage move).
- `tests/boardremote_test.cpp`: `board_ask` with `mode: refine` passes through and is labelled Refine.
- `tests/agentcontext_test.cpp` or `boardpane_test.cpp`: the card row lists Refine with letter `f` between Plan and Run.
- Targeted runs only: `python -m unittest tests.test_board_tools tests.test_board_protocol`, `ctest --test-dir build -R 'boardremote|boardpane|agentcontext'`, and `scripts/relay-build --check "Stop refining"`.
- By hand: open a bug card that repeats a done one, press `f`, see the strip say refining…, then the comment naming the done card and `links.related` on the card; press `p` afterwards and confirm Plan behaves as before.

## Execution Summary
Landed in `d77495b` (code, tests, docs) and `a532eaf` (evidence).

- **Backend.** `board_refine_brief.md` v1. `board_tools.py`: `refine` in `CARD_MODES`, `CARD_MODE_TITLES`, and `_check_refine_update`, which allows only a missing `## Done means`, `labels` with words another card already carries, and a `links` object whose only change is `related`. A Refine turn's own comment skips the `discussed` stage move. `board_protocol.py`: wordless default "Refine this card.", `[Refine · #ID]` head, no stage move on start.
- **GUI.** `BoardPane.cpp`: Refine (f) after Plan on the card row, `f` on the card and list, hint `board.refine`, strip "refining…" / "Stop refining", the empty-thread line teaches `f`. `BoardModel.cpp` `modeTitle` knows `refine`. `BoardRemote.cpp` accepts `refine` from a phone and says it as Refine.
- **Docs.** Protocol 19.10 mode table gains a Refine column and paragraph; BOARD-DESIGN 4.15 is new and 4.9's key line has `f`.
- **Guest bridge.** Step 4 was a no-op: no hard-coded mode names there.
- **Placement.** Refine sits after Plan, as the plan's steps and Verify said. The Risks line that floated putting it before Plan is the owner's call when they see the row.

Not done here: a live Refine turn with a model in the app. That is the verifier's Try it.
