---
id: 7QSK
type: work
status: needs_qa_llm
labels: [bug, sessions, gui]
assignee: relay
rank: m
created: '2026-09-24'
source: 'user in Relay, 2026-09-24'
links: {plans: [], commits: ['74ec27ceab7b36a41bac55d430602134d9eb2091'], evidence: [7QSK thread], related: ['JN7X', G4VB], github: null}
---
# Sessions pane in another project lists the launching project's sessions

## Issue
relay-terminal bug: i just opened the sessions pane in another project (~/repos/modalities) and it is showing my sessions for anotehr project (relay-terminal)

## Done means
The Sessions pane's "This project" scope follows the directory the owner pane's terminal is
actually in (its live cwd, tracked by OSC 7) — the same rule the Board adopted in #JN7X — and
the scope label names that project. Opening Sessions from a pane sitting in ~/repos/modalities,
in a window launched in ~/repos/relay-terminal, lists modalities sessions, not relay-terminal's.
Failure is either the query carrying the pane's frozen launch workspace as the scope filter, or
the scope label naming the launch project.

## Findings
- `Pane::bindSessionManager` (src/Pane.h) stamps every `conversations` request with
  `workspace = self->m_workspace` — the pane's workspace is frozen at creation and inherited from
  the launch directory (or the active pane's workspace, `paneNode()`), so a pane cd'd into
  ~/repos/modalities still reports relay-terminal.
- The worker (backend/relay_core/conv_index.py `_filters`) narrows `scope=project` with
  `c.workspace = <that workspace>` — exactly the frozen-workspace disease #JN7X fixed for boards:
  its comment on `candidateProject()` says the workspace "is frozen when the pane is made and
  inherited from the directory Relay was launched in — the thing that made one project's board
  appear in every pane of every window".
- `linkSessionsPane` (src/RelayWindow.h) also labels the scope from `owner->workspace()` before
  `owner->cwd()`, so the label agrees with the wrong filter.

## Plan
1. `bindSessionManager` sends the owner pane's live terminal directory (`m_cwd`, falling back to
   `m_workspace` only if it is empty) as the request's `workspace`.
2. `linkSessionsPane` labels the scope from `owner->cwd()` first.
3. Anchor tests in tests/conversations_test.cpp pin both rules; build and targeted tests; land.

## Tests
- `scripts/relay-build --target relay-conversations-tests`
- `QT_QPA_PLATFORM=offscreen ./build/relay-conversations-tests theScopeFilterFollowsTheLiveTerminalDirectory`
- `PYTHONPATH=backend python3 -m unittest tests.test_conv_index.ProtocolTests.test_scope_project_uses_the_requested_workspace tests.test_conv_index.IndexTests.test_scope_and_filters` (worker side, unchanged behaviour)

## Execution Summary
- `Pane::bindSessionManager` (src/Pane.h) now stamps the `conversations` request with the pane's
  live terminal directory (`m_cwd`, OSC 7-tracked; `m_workspace` only if that is empty) instead of
  the frozen launch workspace, so the worker's `scope=project` filter matches the project the
  pane is actually in. Same rule the Board took in #JN7X.
- `linkSessionsPane` (src/RelayWindow.h) labels the scope from `owner->cwd()` first, so
  "This project" names the project the list is narrowed to.
- `relay-conversations-tests` gained `theScopeFilterFollowsTheLiveTerminalDirectory`, a
  source-anchor test pinning both rules (the request is built inside Pane, which needs a live
  worker); CMakeLists.txt gives that target `RELAY_SOURCE_DIR` for the anchors.
- Tests run: the new test passes; full conversations suite 52/53 with
  `sessionsDropdownsRespondToMouseChoices` failing **pre-existing** (the only uncommitted diff in
  tests/conversations_test.cpp is this card's addition, and src/Conversations.cpp is clean — the
  failure is at HEAD, #C7NQ/#GR7P territory). `tests.test_conv_index` scope tests pass.
  boardworkspace-tests has 6 pre-existing failures at HEAD (stale anchors, none near this
  change; verified the anchor strings are absent from HEAD's Pane.h/RelayWindow.h too).
- Not changed, noted for a card of its own: `paneNode()` stamps a *new* pane's workspace from the
  active pane, so a pane born in a project tab (`openProjectTab`) still runs its agent scoped to
  the launch project — the sessions list now narrows correctly regardless, but conversations the
  agent in such a tab writes are still indexed under the launch project.
