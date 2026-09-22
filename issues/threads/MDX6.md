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
two fixes in one file collide. This card is stream C: app/boardmd.js.
The board writes for every card in the workstream are made by this session, so the
implementing sessions never edit `issues/` and cannot collide there.

<!-- relay:entry 20260922T144529Z-1b author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-22-phone-ux-… → {"plans": [], "commits": ["25067cb8"], "evidence": ["docs/qa_evidence/2026-09-22…; replaced `## Execution Summary`

<!-- relay:entry 20260922T144537Z-e9 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T144543Z-sj author=agent kind=note model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
One behaviour the implementing session found, looked at, and deliberately did not change — recorded here so a verifier does not read it as a miss.

A nested bullet indented **six or more columns** (`- a` then `      - b`, or `  \t- b`) renders as the literal text `- b` rather than a nested list. That is the renderer's existing rule — an item four or more columns into a paragraph is continuation text — and it behaves identically for plain spaces at HEAD, so it is not a tab bug and no characters are lost either way. Moving that threshold is a Markdown-semantics decision, not a fix, and it is not what this card asked for.

No card filed for it: it predates this card, matches the space behaviour exactly, and loses nothing. If the owner wants the phone to follow CommonMark's nesting rule instead, that is worth its own card with a decision on it.

<!-- relay:entry 20260922T144544Z-t0 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/1624e7a1961d4be7b788e7848e67efb6 -->
- ✦ agent moved this card · Executing → Needs verification · Landed 25067cb8 with its tests. The orchestrating session re-ran the probe this card was filed from against the landed tree: the tab-indented line keeps its first character, the nested bullet is a real nested list, __init__ is literal, and the table under a sentence is a table. Suite 32/32. · evidence docs/qa_evidence/2026-09-22-streamC-board/ · implemented_by anthropic/claude-opus-5 via claude-code
