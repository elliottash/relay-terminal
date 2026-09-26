<!-- relay:entry 20260925T200739Z-h6 author=agent kind=event model=claude-fable-5-1 pane=987d2a1a turn=d90a68ebefd44e559d4f9c1c2e23dc95/099484242f074d54a3753df2ced917ae -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-tests-check-reports-an-existing-test-file-as-ret.md

<!-- relay:entry 20260925T211535Z-36 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T211535Z-43 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T211855Z-we author=agent kind=event model=k3 pane=switchboard turn=e8b7afbb87db04dd95ffd825fc82ee27/65c20040e65b4cf8a6411e092d8c49b9 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T212020Z-84 author=agent kind=event model=k3 pane=switchboard turn=e8b7afbb87db04dd95ffd825fc82ee27/65c20040e65b4cf8a6411e092d8c49b9 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T212031Z-pr author=agent kind=comment mode=plan model=k3 turn=e8b7afbb87db04dd95ffd825fc82ee27/65c20040e65b4cf8a6411e092d8c49b9 -->
I'll read the card and the code it names.

Reading the part of the file the card names.





Now I need `resolve()`, `parse_test_line()`, and who calls `check_card`.













One more check — where `check_card` is tested.



Writing `## Done means` first.

Now the plan itself.

**Done:** #R7HD's `## Done means` and `## Plan` are written. The plan fixes both false warnings in `backend/relay_core/test_history.py`: thread the project root from `tests_protocol.py` into `H.check_card` so manual evidence paths resolve against the repo instead of the worker's cwd, and downgrade the retired verdict to never-run whenever the entry's file verifiably exists on disk (which also covers the discovery-cap false retirement TC5K reported). Nothing needs a decision from you — press **Run** when ready; the executor will also leave a note on TC5K that this fix addresses its symptom.

<!-- relay:entry 20260925T212031Z-ps author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
