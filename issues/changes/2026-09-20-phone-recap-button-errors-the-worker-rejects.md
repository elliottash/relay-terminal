---
id: WMXN
type: work
status: inbox
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [MVGR], github: null}
---
# the phone's recap button errors: it sends reason "remote", which the worker rejects

## Issue
Found while planning #MVGR. The phone's Recap button sends
`recap_request {reason: "remote"}` (`src/Pane.h`, `hooks.recap`, ~5704), but the worker accepts
only `away`, `resume` and `manual` (`suggestions.RECAP_REASONS`; `_recap_request` in
`backend/relay_core/session_protocol.py` raises `reason must be "away", "resume" or "manual".`).
Pressing the button on the phone errors instead of producing a recap.

## Fix shape
Either the GUI sends a valid reason (`manual` — a phone-initiated recap is a manual one), or the
worker accepts `remote` as a reason (and the protocol doc's recap list grows). The GUI-side fix is
one word; the worker-side fix changes the protocol contract. Whichever is chosen, the phone's
recap should print in the pane like a manual one does — and since 192bf4c0 (#MVGR) it would carry
the `[end of message]` / `finished at` preamble too.
