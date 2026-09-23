---
id: WMXN
type: work
status: planned
rank: zzzzzzzzzzzzzz
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

## Done means
Pressing Recap on a phone prints a recap in the pane, rendered like a manual one — since 192bf4c0 (#MVGR) that includes the `[end of message]` / `finished at` preamble. The worker no longer rejects the phone's request: the `reason must be "away", "resume" or "manual".` error never fires for it. Failure looks like what the issue reports: the phone's button produces an error (or silence) instead of a recap block in the pane.

## Plan
**Goal** — the phone's Recap button produces a recap. Take the GUI-side fix from the card's Fix shape: the pane sends `manual`, a reason the worker already accepts, and the request prints through the one recap handler like any manual recap.

**Findings**

- The chain, end to end: the phone's button emits `recap_request` (`app/pane.js:926`) → routed as an AGENT action (`remote/wire.py:82`) → `remote/host.py:2467` → `remote/gui_host.py:423` sends `{"t": "recap_request", "pane": …}` to the GUI — **no reason field crosses the wire** (asserted by `tests/test_pane_view.py:945`) → `src/RemoteShare.cpp:381-385` calls the pane's `hooks.recap()` → the pane's hook sends the worker `recap_request {reason: "remote"}` → `SessionCommands._recap_request` (`backend/relay_core/session_protocol.py:501-505`) rejects it against `suggestions.RECAP_REASONS = ("away", "resume", "manual")` (`backend/relay_core/suggestions.py:14`), raising `reason must be "away", "resume" or "manual".`
- The hook lives in `src/Pane.h` (the file is over 128 KiB, so the search/read tools skip it — find the line with `grep -n '"reason", "remote"' src/Pane.h`; the build-clean snapshot has it at line 3694).
- Why `manual` is the right reason, not just a valid one: `_start_recap` (`session_protocol.py:507`) dedupe-skips `away`/`resume` recaps when no new turns finished, but a `manual` recap always runs — which is what a pressed button owes. The pane's recap handler already prints every reason through one path, so the phone's recap gets the #MVGR `[end of message]` / `finished at` preamble for free.
- Because the wire carries no reason, this fix touches no protocol contract: the worker, the sidecar, the phone app and the protocol docs are all unchanged. (`docs/REMOTE-PROTOCOL.md:338` writes `recap_request {pane, reason}` loosely; tidying that line is optional, not required.)

**Steps**

1. `src/Pane.h`, `hooks.recap` lambda: change `"remote"` to `"manual"` — one word. Fix any adjacent comment that explains the old value.
2. Nothing else changes. Build with `scripts/relay-build`, run the targeted tests below, land with `python3 scripts/land.py begin <me> src/Pane.h` / `commit`, and move the card to done with the test output in the reply (small, one-word GUI change proved by tests).

**Risks**

- **Owner decision already implied, confirm at Execute**: this plan picks the GUI-side fix. The worker-side alternative (add `remote` to `RECAP_REASONS` and grow the protocol doc's recap list) is deliberately not taken: it widens the contract for one caller, and to keep the button's always-run behaviour `remote` would need the manual branch of the dedupe rule anyway — the same special case, at a higher price. Say so before Execute if you want the protocol changed instead.
- `src/Pane.h` defeats the file tools; the executing agent works at the terminal with `grep`/`sed -n` for context.

**Verify**

- `scripts/relay-build` — the one-word C++ change compiles.
- `pytest tests/test_session_protocol.py -k recap` — worker recap handling (validation, dedupe) is unchanged and green.
- `pytest tests/test_pane_view.py -k recap` and `pytest tests/test_remote_security.py -k recap` — the phone-side wire is untouched.
- Behaviour: grep proves no `recap_request` in `src/` still sends `\"remote\"`. If a paired phone or a sidecar harness is available, press Recap and see the recap block print in the pane with no error; otherwise code inspection plus the tests above stands as evidence — the only changed byte is the reason string, and the worker's acceptance of `manual` is already covered by `tests/test_session_protocol.py:339`.
