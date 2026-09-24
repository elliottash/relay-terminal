---
id: WMXN
type: work
status: needs-verification
assignee: agent
implemented_by: glm/glm-5.3
session: cbe2c0ba-c9c2-45a0-b49a-ca215c9a7f3b
rank: zzzzzzzzzzzzzz
created: '2026-09-20'
links: {commits: [8fc8d60b, 9be5a16, 72f6e48], evidence: [docs/qa_evidence/2026-09-24-recap-phone-manual/], github: null, plans: [], related: [MVGR]}
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

## Execution Summary
Executed 2026-09-24. The plan's one-word change was **already on `main`**: commit `8fc8d60b` (2026-09-20 18:25, "recap: the phone's Recap button sends a reason the worker accepts (#WMXN, #PH0N)") changed `hooks.recap` in `src/Pane.h` to `{"reason", "manual"}` and added the comment explaining why. So no code change was made this turn — executing meant verifying the landed fix against the plan's Verify section:

- Grep: no `recap_request` anywhere in `src/`/`backend/`/`remote/`/`app/` still sends `"remote"` — the reasons sent to the worker are `manual` (phone hook, `src/Pane.h:7322`; desktop actions menu, :4472) and `away` (:8921, :8938). The phone↔GUI wire message (`src/RemoteShare.cpp:186`) still carries no reason.
- Worker acceptance of `manual`, and its always-runs (no dedupe-skip) behaviour, asserted by `test_away_recap_not_repeated` (`tests/test_session_protocol.py:369-373`) — passes.
- Phone-side tests green (`test_pane_view` recap test, `test_remote_security` guest-reach test containing the recap wire entry).
- Build: the one-word change has compiled on `main` since Sep 20; nothing new to build this turn (the working tree also carries another session's uncommitted #R5TC edits in `src/Pane.h`, which are untouched).

One unrelated failure was met while testing: `test_compact_resume_recap_and_plan_execute` fails at its **plan_execute** tail — reproduced on a clean `git archive main` export, so pre-existing and independent of this card (which changed no Python). Already filed as #P4XN.

No paired phone was available, so per the plan's fallback the evidence is code inspection plus the targeted tests: `docs/qa_evidence/2026-09-24-recap-phone-manual/`.

## Tests
Run 2026-09-24 via `PYTHONPATH=backend python3 -m unittest` (the suite is unittest; the plan's `pytest … -k recap` maps to `unittest -k`):

- `tests.test_session_protocol.ProtocolHandlerTests.test_away_recap_not_repeated` — **ok** (worker: `manual` accepted and always runs, `away` deduped).
- `tests.test_pane_view -k recap` → `test_recap_is_under_the_pane_menu_and_sends_recap_request` — **ok** (phone-side UI sends `recap_request`).
- `tests.test_remote_security.GuestReachTests.test_no_message_at_all_reaches_the_pane_a_guest_was_not_invited_to` — **ok** (`-k recap` matches no test *name* in that file; this is the test whose `EVERY_PANE_MESSAGE` table holds the `recap_request` wire entry, `tests/test_remote_security.py:530`).
- `tests.test_session_protocol.ProtocolHandlerTests.test_compact_resume_recap_and_plan_execute` — **fails at its plan_execute tail** (line 347): pre-existing on clean `main` (reproduced on a `git archive main` export), unrelated to this card, filed as #P4XN. Its recap assertions before that point pass.

Full outputs in `docs/qa_evidence/2026-09-24-recap-phone-manual/evidence.md`.
