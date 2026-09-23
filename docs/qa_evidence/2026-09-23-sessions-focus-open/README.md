# Sessions Shift+Enter focus and open badge (#E7FP, #J8QP)

`drive.sh` runs Relay under Xvfb with two synthetic saved sessions in an isolated XDG profile.
It opens Sessions with Ctrl+Shift+S, searches for alpha, presses Shift+Enter, replaces the search
with bravo, and presses Shift+Enter again. The [second screenshot](02-after-bravo.png) shows two
new terminal panes beside Sessions with the bravo search still in its box. `result.txt` counts
three terminal composers and confirms the Sessions search is visible.

The synthetic sessions did not complete provider configuration in this run: the screenshot does
not establish that either session's `open` badge appeared. The badge's source-specific matching
is established by `openBadgeDistinguishesGuestSources` in `tests/conversations_test.cpp`.
