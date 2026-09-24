# #PRM2 — the user's path, step by step

1. Settled startup spends exactly two pairing rooms (one `pair`, one `pair_code`) — **check**:
   driver asserts the sidecar sink holds exactly one of each.
2. Repeated `started`/`remote_state` announcements spend none — **check**: driver asserts the
   sink drains empty after four more announcements.
3. Remote control off clears the code and QR; on mints fresh ones — **check**: driver asserts
   the cleared fields and the two fresh requests.
4. A 429 from the rendezvous stays visible with New code — **check** (asserts the note text and
   the button) plus **agent** capture `02-after-429.png`.
5. The working dialog shows typed code and QR together — **agent** capture `01-pairing-ok.png`.
6. Judge whether the refused state is understandable to a person who did not build it —
   **person**, because wording comprehension is a human judgement, not an assertion.

Counts: 3 check, 2 agent, 1 person.
