<!-- relay:entry 20260925T014228Z-jn author=agent kind=event model=glm-5.3 pane=55c67355 turn=ac598e144c144e34a442ba28dd6ed4c3/cf97cb837f124a6ea024b5c15109cb92 -->
- ✦ agent created this card in Planned · .board/changes/2026-09-24-board-update-card-an-apostrophe-in-verify-criter.md

<!-- relay:entry 20260925T014303Z-y1 author=agent kind=event model=glm-5.3 pane=55c67355 turn=ac598e144c144e34a442ba28dd6ed4c3/cf97cb837f124a6ea024b5c15109cb92 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"related": ["SZHQ"]}; verify: (unset) → {"artifact": "code", "primary": "script", "also": [], "human": "none", "criteria…; replaced `## ## Issue`

<!-- relay:entry 20260925T014306Z-ve author=agent kind=event model=glm-5.3 pane=55c67355 turn=ac598e144c144e34a442ba28dd6ed4c3/cf97cb837f124a6ea024b5c15109cb92 -->
- ✦ agent updated this card · replaced `## ## Done means`

<!-- relay:entry 20260925T040137Z-wh author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040138Z-g8 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Planning · a Plan turn started

<!-- relay:entry 20260925T040452Z-df author=agent kind=event model=glm-5.3 pane=switchboard turn=242826c05dd92241740c72f1a56d442c/2cd837aab038435e85ef310dbb35e430 -->
- ✦ agent updated this card · replaced `## ## Done means`

<!-- relay:entry 20260925T040452Z-fx author=agent kind=rewrite model=glm-5.3 pane=switchboard turn=242826c05dd92241740c72f1a56d442c/2cd837aab038435e85ef310dbb35e430 -->
- ✦ rewrote ## ## Done means

<details><summary>before</summary>

```
- A card write with apostrophes, colons and braces in every string field produces a file that re-parses with exactly the keys and values that were sent.
- The board's writer emits front matter through the YAML library (block style), not hand-built flow mappings.
- The write path re-reads what it wrote and refuses to save a card whose front matter does not round-trip.
```

</details>

<details><summary>after</summary>

```
- A card write whose string fields contain `'`, `:`, `,`, `{` and `}` produces a card file that re-parses, with the board's own parser, to exactly the keys and values that were sent — sibling keys (`sign_off`, `source`, `links`) survive the next read *and* the next write.
- The save path re-parses what it is about to write and refuses to save a card whose front matter does not round-trip (writes nothing on refusal).
- Failure would look like: criteria holding an apostrophe still swallows `sign_off`/`source`/`links` on the following write (the #SZHQ symptom) — or the new guard rejects a value a standard YAML reader accepts.
```

</details>

<!-- relay:entry 20260925T040503Z-er author=agent kind=event model=glm-5.3 pane=switchboard turn=242826c05dd92241740c72f1a56d442c/2cd837aab038435e85ef310dbb35e430 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040506Z-ex author=agent kind=comment mode=plan model=glm-5.3 turn=242826c05dd92241740c72f1a56d442c/2cd837aab038435e85ef310dbb35e430 -->
I'll start by reading the card and its related card, and locating the board writer code.

Now let me find the front-matter writer in the board code.

This is one focused ask (write the plan), so I'll keep going without a todo list. Let me read the writer code.

The file is over the read limit; I'll try pulling the exact lines with a quick command.`read_file` is capped by size, so I'll extract the writer through targeted searches.

The writer is a hand-rolled YAML subset. Let me check whether PyYAML is already a dependency, where `FIELD_ORDER` and the save path live, and which tests cover this.

No PyYAML in the backend — the YAML subset is fully hand-rolled. Let me check the test layout and how the package manages Python deps before writing the plan.

The test home is `tests/test_board.py`, which already has round-trip cases. Two last lookups: the save path in `board_tools.py` and the flow-splitter internals.



One more check: whether the generated `scripts/relay-board.py` reuses `board.py`'s parser or carries its own copy.

`relay-board.py` imports `board.py` — one parser. But the C++ Board model also reports `bad_front_matter`; let me check whether it shares the flaw.



I have what I need on the code side. Re-reading the card to get its current hash and exact section headings before writing.

The card's existing sections carry doubled `##` headings from the earlier write; I'll revise `## Done means` in place (matching its stored heading), then write the plan.

Done means is in. Now the plan.

**Plan written to #EBGX.** The plan fixes the real fault the plan-turn reading found: the writer's `''` escaping is YAML-correct, but the parser's four quote scanners (`_split_flow`, `_split_key`, `_strip_comment`, the balanced scanner in `backend/relay_core/board.py`) never treat `''` as an escape, and the next write persists the swallowed dict — so the fix is to repair those scanners, add a round-trip refusal guard at the save in `board_tools.py:893`, and add the #SZHQ-reproduction tests in `tests/test_board.py`. One decision is left to you, recorded in Risks: the card's Issue proposed switching to PyYAML, but the backend has no PyYAML dependency and this plan needs none — say the word if you'd rather vendor it instead.

<!-- relay:entry 20260925T040506Z-yh author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
