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
two fixes in one file collide. This card is stream A + E: app/pane.js and app/pane.css for the chip; src/Pane.h, src/PaneState.*, src/RemoteShare.*, remote/pane_state.py and docs/REMOTE-PROTOCOL.md for the fixed-effort flag and the refusal answer.
The board writes for every card in the workstream are made by this session, so the
implementing sessions never edit `issues/` and cannot collide there.

<!-- relay:entry 20260922T144219Z-hx author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["b8222865"], "evidence": ["docs/qa_evidence/2026-09-22… → {"plans": [], "commits": ["b8222865", "44a46ca1", "7d313776"], "evidence": ["doc…; replaced `## Execution Summary`

<!-- relay:entry 20260922T144228Z-44 author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T144237Z-6p author=agent kind=progress model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/381e26be698647499dd654bd35620d8b -->
Stays in Executing on purpose: faults 1 and 2 landed in `44a46ca1`, fault 3 is stream E's and is still running. The card moves to needs-verification when the wire half lands, so a verifier is never handed half a chip.

Verified the client half independently of the implementing session's own report, with the probe this card was filed from, unchanged, against the landed tree:

* level-only state — before `after effort=low {"shown":"✓ high"}` (dropped); after `start {"shown":"high"}` → `after effort=low {"shown":"low"}`.
* chevron geometry — chevron x 140.2–147.5 sits inside the model at 17–154.5, with the level at 158.5–230.6; it was 216.4–223.6, across a level at 198–289.
* `RELAY_KEYRING=off python3 -m unittest tests.test_pane_view` → 36 tests, OK.

The seam to check when stream E reports: the client reads `m.effort_fixed === true` from the model block and nothing else. If E publishes the fact under another name or shape, this branch is dead code and the chip is live for a level the pane will refuse — which is exactly fault 3 with an extra step.
