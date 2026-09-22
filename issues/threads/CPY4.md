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
two fixes in one file collide. This card is stream A: app/pane.js and app/pane.css.
The board writes for every card in the workstream are made by this session, so the
implementing sessions never edit `issues/` and cannot collide there.

<!-- relay:entry 20260922T144313Z-ey author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["7f945e42"], "evidence": ["docs/qa_evidence/2026-09-22… → {"plans": [], "commits": ["7f945e42", "782808f9", "7d313776"], "evidence": ["doc…; replaced `## Execution Summary`

<!-- relay:entry 20260922T144319Z-j3 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260922T144328Z-tp author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T144334Z-kx author=agent kind=decision model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
The `## Done means` on this card was self-contradictory and has been corrected by the session that wrote it. The original read:

> "The fallback text, when there is one, is attached to the sheet that asked and not to whatever sheet happens to be in the DOM."

in the same paragraph as "the sheet closes on the tap". Both cannot hold — with the sheet closed there is no sheet to attach to. The implementing session flagged it rather than quietly picking one, which is the right call.

The resolution: the refused id goes into the **toast**, which after fix 1 is `z-index: 20` and the topmost thing on the screen, and is selectable so it can still be copied by hand. The clause's purpose — the reader can get the id when the clipboard refuses — is met; its letter is not, and the letter was wrong. If a verifier would rather have the line inside a sheet, that means not closing the sheet on the tap, and the whole of fault 1 comes back.

<!-- relay:entry 20260922T144336Z-82 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent moved this card · Executing → Needs verification · Landed 782808f9 with its tests. The orchestrating session re-ran both probes this card was filed from: the toast is now topmost at its own centre (was a sheet row behind it) and the list holds scrollTop 1975 across a clock tick (was 2010 to 0), while a real title change still rebuilds. Suite 36/36. The card's last paragraph is app/app.js and stays with stream D. · evidence docs/qa_evidence/2026-09-22-streamA-pane/ · implemented_by anthropic/claude-opus-5 via claude-code
