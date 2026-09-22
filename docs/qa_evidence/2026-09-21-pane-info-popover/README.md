# Pane info popover — implementer evidence

Card: #P1CP  
Date: 2026-09-21

## What was exercised

The focused `ConversationsTest::paneInfoPopoverCopiesAndKeepsTheInfoClick` widget test opens the
popover through a circle-i enter event, crosses from the button to the popover and waits beyond the
close delay, copies the exact displayed eight-character pane ID, dispatches Dim, refreshes its
manual state, confirms circle-i still emits its original click, and reopens the surface by keyboard
focus.

## Commands and results

- `RELAY_SESSION=codex-pane-info scripts/relay-build --target relay` — passed; `src/main.cpp`
  recompiled and the Relay executable linked.
- `RELAY_SESSION=codex-pane-info scripts/relay-build --target relay-conversations-tests` — passed.
- `ctest --test-dir build -R '^(conversations|themeswitch|panedimming)$' --output-on-failure` —
  3/3 passed (`conversations`, `themeswitch`, `panedimming`).
- `scripts/relay-build --check 'Pane ID'` — passed; the integrated Relay executable contains the
  new popover UI.

## Independent verification target

In a running Relay window, hover from a terminal pane's circle-i into the popover, copy the shown
ID and compare it with that pane's log ID, toggle Dim twice, then click circle-i and confirm the
Conversation info pane still opens. Repeat the open path by keyboard focus. Check that ◐ no longer
occupies permanent chrome space, including on a narrow pane.
