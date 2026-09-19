# Ctrl+Enter sends now — implementer evidence (#N8VK)

Run 2026-09-19, live under Xvfb, isolated `HOME`/`XDG_*`/`TMPDIR`, no provider account
(`local:stub` → `stub-provider.py`, which logs the epoch time of every ask). `drive.sh`
reproduces the card's three scenes; `implementer-notes.txt` is the OCR of each shot.

## The card's reproduction, after the fix (`queue` scene)

`sleep 8` running, `echo queued-behind` queued behind it, Ctrl+Enter on an agent prompt:

- `requests.log`: the ask left **within ~60 ms of the keypress** (`timeline.txt` 615.103,
  ask 615.052) — **4.8 s before the `sleep 8` ended** (611.85 + 8 s). Before the change this
  prompt sat in the queue until the echo ran, ~25 s in the card's reproduction.
- `implementer-queue-mid.png`: the turn already shows `» stub / Received. Done.` while the strip
  still holds `▸ running $ sleep 8` and `$ echo queued-behind ×` — the prompt went through, the
  queue kept its rows. No "Queued" toast for the prompt.
- `implementer-queue-after.png`: the sleep ended, `echo queued-behind` ran **in its original
  place** (`queued-behind` in the terminal), the agent line stands above — order untouched.

## Same rule for the other doors (`star`, `busy`)

- `star`: `sleep 6` running, `echo star-tail` queued, `*`-prefixed prompt + plain Enter — ask at
  the keypress (637.94 vs 637.93), turn visible mid-sleep, echo ran in place after.
- `busy`: while a turn streams, Ctrl+Enter still prints "Interrupting the current turn…" and the
  new ask goes out at the keypress (660.67 mark, 660.61 ask) — the interrupt branch unchanged.

## Suites

- `ctest -R queuesubmit`: 8/8.
- `ctest -j` full: 56/57, the only miss the `backend-and-bash` ctest default timeout; the same
  suite's content re-run directly: **3040/3040 OK** (one earlier run showed 3 `test_board_tools`
  failures while a killed parallel ctest job was still settling — different test count, clean
  re-run green, unrelated to this change; the queue/steer/interrupt worker tests pass throughout).
- `relay` target builds; the new `relay-queuesubmit` library is `-Wall -Wextra -Wpedantic` clean.
