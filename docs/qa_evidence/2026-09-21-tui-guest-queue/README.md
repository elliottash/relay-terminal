# TUI guest queue (#TG7Q)

The C++ guest delivery reservation is acquired before typing into the PTY. Repeated poll ticks,
idle snapshots and duplicate completion signals cannot release it. A busy report transfers
ownership to the existing busy gate; a subsequent idle report releases the next prompt.
Guest exit/change resets the reservation. A stopped/missing backend rejects the write without
reserving the guest; the entry remains queued. Guest queue heads can proceed while the queued
shell launch remains active, but still respect pause/selection, guest identity and leaving state.

Validation:
- `scripts/relay-build --target relay-queuesubmit-tests relay`
- `ctest --test-dir build -R '^queuesubmit$' --output-on-failure`
- Regression cases cover delayed busy across 25 idle polls, duplicate completion after the next
  delivery, guest departure/reset, and resource ownership while a queued launch remains active.
- Board-wide format check has pre-existing errors/warnings outside this card; own card/thread
  checked separately in the output. Existing shared Pane changes excluded during landing.

Owner live test (no paid guest turns were run by the implementer):
1. Restart Relay using the rebuilt `build/relay`; an already running process retains the old C++.
2. In a terminal pane run Claude or Codex as a TUI (not a guest harness model preset).
3. Submit three distinct prompts quickly. Confirm only the first is sent, later rows stay queued,
   and each runs after the previous turn completes without overwriting any prompt.
4. Repeat with the guest launch itself queued behind a shell command. Once the guest starts,
   its queued prompts must run without waiting for the guest process to exit.
5. Pause/select the queue head and confirm delivery waits. Exit/change the guest during a pending
   delivery; confirm remaining prompts stay queued for their named guest, never sent to the shell.

The guest's busy hooks/rollout tail must be operational. The frontend does not infer acceptance
from terminal output or a timeout: if no progress signal arrives it retains the reservation to
avoid overwriting input. Commands handled entirely inside the guest (including local slash
menus) may emit no turn signal; verify those interactions in the live acceptance pass as well.
