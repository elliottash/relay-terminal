<!-- relay:entry 20260924T132605Z-n1 author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/64473ac1f3874f0d975bfe614b7295fc -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-terminal-pane-drops-board-cards-batches-so-its-c.md

<!-- relay:entry 20260924T132619Z-8g author=agent kind=evidence model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/64473ac1f3874f0d975bfe614b7295fc -->
Diagnosis (glm session f1476, pane b0371114, 2026-09-24 13:1xZ).

**Where the owner saw it.** This pane's earlier session (claude-fable, 479dadd1, turn 2a7ab073 ended 13:07:34Z) wrote a summary naming #Q5QJ, #SAW4, #PH0N, #H0P3; the owner reports #SAW4 and #PH0N unlinked in the terminal.

**Mechanism (proven).** A `#ID` in terminal prose only becomes a card link when the pane's card index resolves it (`Pane::lookupOutputCard` → `m_cardIndex`, src/Pane.h ~13644-13790). The index is fed in `Pane::relayMessage`: it handles `board` (full reset) and `board_changed` (upserts/removes) — but has **no case for `board_cards`**, which is how the backend delivers snapshot rows beyond the first batch (`_row_batches`, MAX_ROWS_PER_MESSAGE=400; the follow-up batches are documented as "the pane patches in the same way", and BoardPane.cpp:6997 and ReviewPane.h:105 do — Pane does not).

This board has 658 cards → `board_open` answers with a 400-row `board` event + one 258-row `board_cards` (GUI log: pane b0371114 got exactly `board` at 13:06:13.906 then `board_cards` at 13:06:13.911). Recomputing the walk order with the backend's own `Board.card_paths()`: **batch 2 (dropped) holds PH0N, FR1C, H0P3**; Q5QJ/SAW4 are in batch 1. H0P3 and SAW4 still reached the index later via `board_changed` upserts when the same session created them (13:06:15.909, 13:06:18.627). PH0N has not been written since, so it is simply absent → **#PH0N can never linkify in any terminal pane**. Same for #FR1C and every card past row 400 (~40% of the board).

**The #SAW4 half (not fully explained).** By the same trace, #SAW4 resolves (batch 1 + upsert), and the assistant-prose "filed as #SAW4" in this pane should link. The unlinked #SAW4 the owner saw is most plausibly the one in their own just-typed message / the card-attachment echo, which are not card-linkified. Needs one look at the screen to confirm which occurrence stayed plain.

**Fix.** Add the `board_cards` case to `Pane::relayMessage`'s index path (`m_cardIndex.upsert(ev["cards"].toArray())` — mirror BoardPane.cpp:6997), and on the `board` event compare `cards_total` with what arrived so a pane that missed a batch re-asks instead of holding a 400-card index forever. Note also `lookupOutputCard`'s ask-once: the first hover before the index lands returns false ("links from the next hover on"), which can strand a reference until the next paint.

<!-- relay:entry 20260924T132619Z-9y author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/64473ac1f3874f0d975bfe614b7295fc -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260924T181102Z-sd author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/d8f3cb319732495eb42634865094341b -->
- ✦ agent claimed this card · assignee agent, Discussing → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session cf600dfa

<!-- relay:entry 20260924T181102Z-y9 author=agent kind=progress model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/d8f3cb319732495eb42634865094341b pane_token=cf600dfa-e32c-4893-88f2-a0c65ba42ef6 -->
Claimed (cf600dfa) · working on it from a terminal pane

Implement the fix: feed board_cards batches into the pane's card index and re-ask when a snapshot arrives incomplete; test; land.

<!-- relay:entry 20260924T181111Z-zs author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/d8f3cb319732495eb42634865094341b -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T182234Z-6m author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/d8f3cb319732495eb42634865094341b -->
- ✦ agent updated this card · appended to `## Execution Summary`

<!-- relay:entry 20260924T182240Z-6n author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/d8f3cb319732495eb42634865094341b -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T182248Z-nc author=agent kind=event model=glm-5.3 pane=cf600dfa turn=f1476b121bd447608cd13fa9b7e4f84a/d8f3cb319732495eb42634865094341b -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in 6fd0c6b4 with two passing unit tests; per this board's verify policy the card waits in needs-verification for the user to close. · evidence issues/changes/2026-09-24-terminal-pane-drops-board-cards-batches-so-its-c.md § Tests (unit tests PASS; commit 6fd0c6b4 verified on a clean export) · implemented_by glm/glm-5.3
