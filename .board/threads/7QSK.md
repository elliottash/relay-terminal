<!-- relay:entry 20260924T131201Z-q7 author=relay kind=event -->
### Relay · 2026-09-24 13:12
card filed from the user's report: Sessions pane in ~/repos/modalities listed relay-terminal sessions.

<!-- relay:entry 20260924T131202Z-k3 author=relay kind=progress -->
### Relay · 2026-09-24 13:12
claimed this card. Cause found: `Pane::bindSessionManager` sends the pane's frozen launch
workspace as the `conversations` request's `workspace`, which the worker uses as the
`scope=project` filter; the live terminal cwd (OSC 7) is ignored — the same disease #JN7X
documented and fixed for the Board's `candidateProject()`. Fixing the request to carry the live
cwd, the scope label in `linkSessionsPane` to match, plus anchor tests in
tests/conversations_test.cpp.

<!-- relay:entry 20260924T131603Z-e9 author=relay kind=evidence -->
### Relay · 2026-09-24 13:16
fix in: `Pane::bindSessionManager` sends the live terminal cwd as the conversations request's
`workspace` (was the frozen launch workspace); `linkSessionsPane` labels the scope from
`owner->cwd()`; new anchor test `theScopeFilterFollowsTheLiveTerminalDirectory`.
Evidence: `scripts/relay-build --target relay-conversations-tests` builds;
`QT_QPA_PLATFORM=offscreen ./build/relay-conversations-tests theScopeFilterFollowsTheLiveTerminalDirectory`
PASS; conversations suite 52/53 (`sessionsDropdownsRespondToMouseChoices` fails pre-existing at
HEAD, unrelated — no uncommitted diff outside this card's addition); `PYTHONPATH=backend
python3 -m unittest tests.test_conv_index.ProtocolTests.test_scope_project_uses_the_requested_workspace
tests.test_conv_index.IndexTests.test_scope_and_filters` OK; boardworkspace-tests failures
(6) pre-existing at HEAD, anchors absent from HEAD's headers as well.

<!-- relay:entry 20260924T131804Z-f2 author=relay kind=landed -->
### Relay · 2026-09-24 13:18
landed 74ec27ceab7b36a41bac55d430602134d9eb2091 on main (card #7QSK, needs-verification). The
verify tree built clean; two unrelated RelayWindow.h hunks from session e728 (#E728) were left
uncommitted in the working tree, untouched.
