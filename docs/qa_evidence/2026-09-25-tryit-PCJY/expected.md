# Sealed expected result — #PCJY

Running `stage.sh` prints two blocks.

The BEFORE block (backend at `01affa57^`, i.e. `2fd471ce`) prints:

    state_loaded names the guest: (nothing)
    harness start: resume=None fork=False  ->  claude --session-id <a fresh uuid> (empty memory)

The AFTER block (backend at `59635ce2`) prints:

    state_loaded names the guest: {'guest': 'claude', 'guest_session': '6f3c8a24-2c8d-4d61-9856-50ddc861b242'}
    harness start: resume=6f3c8a24-2c8d-4d61-9856-50ddc861b242 fork=False  ->  claude --resume 6f3c8a24-2c8d-4d61-9856-50ddc861b242

The conversation-id line differs between runs (a fresh temporary store each time);
that is expected and not part of the comparison. The three `guest` lines and the
two `harness start` lines are the claim.
