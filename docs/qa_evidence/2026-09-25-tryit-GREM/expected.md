# Expected — #GREM (sealed until you answer)

Running `stage.sh` prints, in order:

1. `A terminal pane agent is offered board_merge_cards: True` — the pane has the tool at an
   ordinary turn, with no Board cleanup running.
2. `... and still NOT offered board_split_card: True` — the wholesale-restructure tools stay
   cleanup-only; only merge moved.
3. A merge result with `merged 1 card(s) in: #N1BC Model dropdown search` — the call was
   accepted, not refused with the old `board_refused` cleanup-only error.
4. The survivor keeps its status (`in-progress`) and gains a `## Merged in` section carrying the
   duplicate's title and text.
5. The folded card is `dropped`, its file still exists, and its tail says `Merged into [#E0Y7]`
   with "Nothing was thrown away … this card stays here so `#N1BC` keeps resolving".

If `board_merge_cards` were still cleanup-only, step 1 would print False and step 3 would return
`{"code": "board_refused", ...}` — which is what the #BCJF delivery actually hit on #P4XN.
