# Staging notes — #PCJY

- `stage.sh` is the one command. It takes no arguments, needs no model and no
  network: the guest CLI is `tests/guest_harness_fake.py`'s FakeHarness, and the
  model provider is the scripted test provider.
- The BEFORE side is materialised with `git archive 01affa57^` — the pristine
  backend before the fix, never this working tree (other sessions have uncommitted
  work in it).
- What this shows: the whole worker seam of the fix — the `state_loaded` event the
  pane receives after `resume`, and the harness `start()` the worker performs for
  the guest configure that follows (`--resume` vs a fresh `--session-id`).
- What this does not show: the C++ side (`Pane::takeGuestRequest` staging the
  resume, and `serializeNode` saving `guest_session` in the layout) cannot run
  headless; it is covered by the build and by the unittests named on the card. The
  driver plays the pane's staging rule — "guest.resume = whatever state_loaded
  named" — on both sides, which is why BEFORE also goes through the same script
  and finds nothing to stage.
- The hard-kill case (conversation never resumes) is the same rule fed from the
  layout's `guest_session` instead of the event; it lands in the same `start()`.
- Re-running is safe: each run builds a fresh temporary store.
