<!-- relay:entry 20260922T131607Z-pd author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 13:16
Filed from a review and a live drive of the phone app at 390×844 in headless Chrome with touch
emulation, against the real rendezvous, host and Noise link for the whole-client half
(`tests/test_remote_browser.py`'s Harness) and the pane-state fixtures for the pane view. The
drive, its scripts, its numbers and its screenshots are in
`docs/qa_evidence/2026-09-22-phone-ux-drive/`; the README there maps each finding to the file and
line it is in and to the picture of it. Nothing was changed in the app: this card is the record of
the fault, not a fix.

<!-- relay:entry 20260922T142431Z-pf author=claude-code kind=progress -->
### Claude Code · 2026-09-22 14:24
Claimed as part of one workstream over the eight phone cards, organised by which files
a fix has to touch rather than by card, because several sessions share this checkout and
two fixes in one file collide. This card is stream A + D: app/app.js for the reconnect, Back and the notification hand-off; app/pane.js for the ask-choice and queue-row rebuilds.
The board writes for every card in the workstream are made by this session, so the
implementing sessions never edit `issues/` and cannot collide there.

<!-- relay:entry 20260922T144348Z-h9 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-phone-ux-… → {"plans": [], "commits": ["dc2c00ab", "7d313776"], "evidence": ["docs/qa_evidenc…; replaced `## Execution Summary`

<!-- relay:entry 20260922T144353Z-ws author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T144930Z-15 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["dc2c00ab", "7d313776"], "evidence": ["docs/qa_evidenc… → {"plans": [], "commits": ["dc2c00ab", "4777602f", "8aa0eb2c", "7d313776", "5f743…; replaced `## Execution Summary`

<!-- relay:entry 20260922T144941Z-g9 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T144952Z-0q author=agent kind=evidence model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
Stream D reported one gap it could not close, and the orchestrating session closed it rather than passing it on. Recording both halves, because the gap is the more interesting one:

**What D found.** `closeOneLayer()` was written to ask the mounted pane view and the board whether they had a layer to close, through `typeof … === 'function'` guards. Neither `mountPane` nor `mountBoard` returned such a handle, so both guards were false and Back fell straight through to closing the pane. That is worse than the fault this card was filed on: before, Back closed the app; after D's commit alone, Back over an open Conversations sheet would have closed the pane *and* the sheet. D could not fix it — `app/pane.js` and `app/board.js` belonged to streams A and C while it was running — and said so with the exact handles it needed.

**What was done.** Both streams finished, so the files were free. `8aa0eb2c` adds `closeSheet()` to `mountPane` and `closeSheet()` / `closeCard()` to `mountBoard`, each returning whether there was anything there. The closing itself already existed inside both files; only the answer was missing, which is why the guards were written that way in the first place.

**Proof it was a real gap, not a theoretical one:** the new test run against a clean `git archive` export of HEAD-without-the-handles fails at exactly the sentence it was written for — `AssertionError: False is not true : Back took the pane away with the sheet` — and passes on the landed tree.

Also re-run here, independently of the implementing sessions: `tests.test_remote_browser` 24/24 and `tests.test_pane_view` 36/36.

<!-- relay:entry 20260922T144954Z-g2 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
- ✦ agent moved this card · Executing → Needs verification · All four items landed: dc2c00ab (stream A, item 4), 4777602f (stream D, items 1-3) and 8aa0eb2c, which closed the gap D reported and could not reach. Both suites re-run by the orchestrating session; every test also fails against the code before its fix. · evidence docs/qa_evidence/2026-09-22-streamD-shell/ · implemented_by anthropic/claude-opus-5 via claude-code

<!-- relay:entry 20260926T012602Z-ky author=agent kind=event mention=JDN4 model=claude-opus-5-5 pane=switchboard turn=7bfd06306124d8cff9b6c80c5934362a/06ee51f0029e4641937c70d5dffa4788 -->
mentioned in #JDN4 · 2026-09-26 · agent
