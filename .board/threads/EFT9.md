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

<!-- relay:entry 20260922T145510Z-ad author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/07e3de33b34a4b49af98b45413370b96 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["b8222865", "44a46ca1", "7d313776"], "evidence": ["doc… → {"plans": [], "commits": ["b8222865", "44a46ca1", "2ee01f0c", "d329d88a", "a0882…; replaced `## Execution Summary`

<!-- relay:entry 20260922T145527Z-th author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/07e3de33b34a4b49af98b45413370b96 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260922T145537Z-0x author=agent kind=decision model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/07e3de33b34a4b49af98b45413370b96 -->
A departure from what this card's `## Discussion points` guessed at, made by the implementing session and endorsed here because it is right.

That section suggested "gating `remoteState()`'s `in.efforts` on `effortFixed`", and stream A's earlier summary repeated it. Stream E did **not** do that, and said why: publishing no levels would meet the letter of the suggestion and break the card's own `## Done means`. `renderEffort` hides the chip whole when `levels.length === 0`, so a Relay Free pane would have drawn **no chip at all** where the card asks for "a chip that cannot be changed rather than a live picker". `Entry::effortFixedReason()` exists precisely because a fixed model's levels are still worth showing — Relay Free has two.

So the levels stay published and `effort_fixed` rides beside them. The `## Discussion points` were a guess written before the work, and the work found the better answer; the card's `## Done means` is what governs, and it is met.

Verified here rather than taken on trust — the real hub cleaner into the real view:

```
hub publishes  {"label":"Relay Free","effort":"high","efforts":["low","high"],
                "effort_fixed":true,"effort_fixed_reason":"Relay free sets the level for you."}
chip, fixed    {"hidden":false,"disabled":true,"shown":"high"}
chip, ordinary {"hidden":false,"disabled":false,"shown":"high"}
view device    {"label":"Relay Free"}          <- no level, no levels, no fixed flag
```

The seam this card's thread flagged when the client half landed — "the client reads `m.effort_fixed` and nothing else, so if E publishes that fact under another name the branch is dead code" — is therefore closed: the name matches and the chip really is disabled.

<!-- relay:entry 20260922T145539Z-8f author=agent kind=event model=claude-opus-5 pane=21ce0a13 turn=9ce3cc66aa5f4d38884b376901d01092/07e3de33b34a4b49af98b45413370b96 -->
- ✦ agent moved this card · Executing → Needs verification · All three faults landed, across both halves: 44a46ca1 for the chip, 2ee01f0c/d329d88a/a0882679 for the wire. The orchestrating session verified the seam itself — the real cleaner's output into the real view gives a visible, disabled chip for a fixed level, and a view device is told none of it. Suites 36/36, 45/45. · evidence docs/qa_evidence/2026-09-22-streamE-effort/ · implemented_by anthropic/claude-opus-5 via claude-code
<!-- relay:entry 20260925T225836Z-lb author=agent kind=note -->
Verification (rev 2db96643, clean worktree): PASSED — the chip tests (renamed by later cards) and tests.test_remote_pane_state green at HEAD; the three fixes are in the shipped app/pane.js; ctest -R panestate is a stale line (target is a static lib now); test_pane_view's OutboxTests error is #DX4A-family module drift, noted there. Evidence: docs/qa_evidence/2026-09-25-verify-EFT9/. Moved needs-verification → needs-qa-llm.

