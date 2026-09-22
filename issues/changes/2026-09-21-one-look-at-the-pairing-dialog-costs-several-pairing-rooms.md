---
id: PRM2
type: work
status: executing
labels: [bug, remote]
component: [gui, remote]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Claude Code session on #SWPH, 2026-09-21: found by the hosted drive `docs/qa_evidence/2026-09-21-swph-hosted-drive/`'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-swph-hosted-drive/notes.txt], related: [FR1C, SWPH], github: null}
---
# One look at the pairing dialog costs two to four pairing rooms, and a code shown across an off/on is dead

## Issue

Two faults in #FR1C's pairing dialog, measured by the #SWPH hosted drive on 2026-09-21:

- The hosted rendezvous allows 20 pairing rooms an hour per desktop. Opening "Pair a phone…" once
  costs 2–4 of them (the QR's room, the typed code's room, and re-mints as the dialog settles); the
  drive used 17 in one run and its second attempt was refused with `429 too many pairing rooms this
  hour`. An owner pairing an iPhone and an iPad, reopening the dialog a few times, can lock himself
  out for an hour. Fix shape: one room per dialog opening until it is used or expires (the QR and
  the code can share nothing, by #FR1C's design, so that is two), no re-mint on refresh, and a
  sentence in the dialog when the rendezvous answers 429 instead of a dead code.
- With the dialog open, switching remote control off and on from the plug menu leaves the dialog
  showing a code that no longer works; reopening mints a good one. The dialog should mint again, or
  say the code ended, when `remote_state` goes off and on under it.

## Done means
Opening the pairing dialog does not remint rooms on repeated state announcements. Turning remote control off invalidates the displayed credentials; turning it on creates usable credentials. Rate-limit errors are visible and no stale code remains.

## Execution Summary
Pairing waits for the configured service destination to be online, then requests one QR and one typed-code room. Repeated started/remote_state announcements reuse those credentials. Stop clears both displays; restart requests fresh ones. A failed request leaves the error visible and offers New code. Xvfb driver exercises the real dialog and its outgoing sidecar requests.

## Tests
- `ctest --test-dir build -R '^remotesettings$'`
- `manual: docs/qa_evidence/2026-09-22-remote-delivery/` (run.py drives the actual dialog under Xvfb)
