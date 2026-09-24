# W92V verification

The exact-tree fast build passed with `scripts/relay-build --fast --target relay`.
`git diff --check` passed for the three changed source files.

Visual verification still needed: start a background agent, then open it from Sessions,
a header count, a notification, and a card session link. In each case check that the
same live conversation appears beside the current pane in the visible window, with
no new visible window, and that the background count clears. Check with two visible
windows that the one containing the clicked Open control receives the pane.
