# Staging notes — #PRM2 Try it

`stage.sh` runs `try_run.py`, which compiles `try-driver.cpp` against the current
`build/` objects (the real `src/RemoteShare.cpp`) and runs it under `xvfb-run`.
The driver's "sidecar" is a `cat` process: every message the dialog would send to
the real Python sidecar goes into a sink the driver reads, and sidecar replies
(`started`, `remote_state`, `error 429`) are injected by hand.

Difference from real use, honestly:

- The rendezvous is never contacted; the 429 is injected as the exact error string
  the hosted rendezvous sends, so the dialog's rendering of it is real but its
  arrival is scripted.
- The QR is a 2x2 placeholder matrix, not a scannable code; the typed code "ABCD 1234"
  is fixed, not minted.
- You are looking at captured images, not a live dialog, so the New code button
  cannot be clicked here; its visibility and enabled state are asserted by the
  driver before each capture.
- Behaviour against the real hosted rendezvous (the 20-rooms/hour lockout on a
  real iPhone/iPad) is not staged here and stays human QA outside this Try it.
