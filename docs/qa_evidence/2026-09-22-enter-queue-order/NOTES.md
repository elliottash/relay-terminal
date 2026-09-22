# #QFF1 implementation evidence

The empty-Enter handler selected `m_entries.last()`, allowing the latest queued
prompt to overtake older prompts. It now selects the head. The handler checks an
existing steer's escalation first so another Enter targets that same prompt.
Explicit remote steering still selects the newly submitted entry by id.

`repeatedEnterKeepsTheFirstQueuedPrompt` in `tests/consolemode_test.cpp` sends real
composer key events: submit first and second while busy, then press empty Enter
twice. It checks that the first outgoing steer contains the first prompt, the
next outgoing `queue_unsteer` names that steer's id, and one prompt remains queued.

Validation:
- `scripts/relay-build --target relay-consolemode-tests`: passed.
- `ctest --test-dir build -R '^consolemode$' --output-on-failure`: passed (1/1).
- `XDG_CONFIG_HOME=/tmp/relay-queue-fifo-config QT_QPA_PLATFORM=xcb xvfb-run -a build/relay-consolemode-tests`: passed; isolated settings and a real X11 Qt event loop.
- Board check: existing unrelated errors/warnings; none refer to this card/thread.

The live tests use a stub worker and inspect outgoing messages; they do not run a
live model provider. Separate verification remains pending.
