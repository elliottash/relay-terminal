---
id: SPSG
type: work
status: needs-verification
labels: [feature, keyboard, sessions, projects]
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-22'
source: Owner in a Codex Relay pane, 2026-09-22; keyboard-system survey
links: {plans: [], commits: [e914d65c, cd7dcfcb, 3857b5fc, 878b0ca452ea7a4c4caa876da1c8d58c39ac8006, 134186308f20d143d070b086a40236ee7ab82b13, 5d7791c83076df7376e93a53e9639718fdbfe5a2], evidence: [docs/qa_evidence/2026-09-23-sessions-projects-keys/], related: [P7SJ, DKEW, KYPR, QWAS], github: null}
---
# Give Sessions and Projects one entry point and consistent toggling

## Issue
sessions and projects are a single function (if there are separate pathts right now, thats redundant)

this is great. we shoudl definitely fix the identified gaps so card those first.

## Planning notes
`src/RelayWindow.h` already hosts Projects, Sessions and Globals in one SessionManager pane, but the public actions remain separate: `projects.open` opens the Projects tab, while `agent.resume`/`conversations.open` toggle Sessions. `src/Keymap.h` assigns Ctrl+Shift+P and Ctrl+Shift+Y respectively. A focused Projects or Globals entry does not share Sessions' close-on-repeat behavior.

The owner considers Sessions and Projects one function. Consolidate public navigation to one Sessions & Projects entry and shortcut; internal filters or views may remain useful without separate top-level shortcuts. Preserve explicit project attachment, saved-session search, active-session navigation, Recently closed, and access to Globals. The exact key and treatment of Globals are part of the broader shortcut discussion, not settled by this card.

## Decisions
Owner: "sessions and projects are a single function (if there are separate pathts right now, thats redundant)".
Owner: "nobody has used relay yet so we can think fresh." No legacy-key migration is required merely to preserve shipped defaults.
Owner: "ctrl shift s on sessions" and "ctrl shift p on projects". The shared pane now has separate direct keys for these tabs. Owner: "in the sessions & projects pane, put sessions the first tab. put \"background\" as a button under sessions like recently closed".

## Done means
Sessions is the first tab in the shared pane. Ctrl+Shift+S selects Sessions and Ctrl+Shift+P selects Projects; each repeats to close when its tab is focused. Background and Recently closed appear as buttons within Sessions, each opening a nested page with a way back. Existing background sessions remain reachable and reopenable.

## Tests
`scripts/relay-build --target relay-conversations-tests relay-keymap-tests`
`QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(conversations|keymap)$' --output-on-failure`
`PYTHONPATH=backend python3 -m unittest tests.test_keybindings tests.test_action_catalog`
`XDG_CONFIG_HOME=$(mktemp -d) RELAY_SHOT_DIR=docs/qa_evidence/2026-09-23-sessions-projects-keys xvfb-run -a ./build/relay-conversations-tests sessionsActivationRefreshesAndClosedIsNested`
manual: `docs/qa_evidence/2026-09-23-sessions-projects-keys/`

## Execution Summary
One action, sessions.open on Ctrl+Shift+S, opens, focuses or closes the one pane on the tab last used. The pane is titled "Sessions & Projects" and holds Projects, Sessions, Globals and Background. projects.open, globals.open, agent.resume and conversations.open stay registered for slash commands and keybindings.json with no keys, and toggle the same way on their tab. The catalog shows one row, with tab children. Commits: e914d65c, cd7dcfcb, 3857b5fc (title).
2026-09-23 revision: Sessions is first in the shared pane. Ctrl+Shift+S selects Sessions; Ctrl+Shift+P selects Projects. Background joins Recently closed as a button within Sessions, with a nested page and Back button. Added a key and layout regression test. Commit `878b0ca4`; preset assertions `13418630`; Xvfb screenshots `5d7791c8`.

![Sessions first, with Recently closed and Background buttons](docs/qa_evidence/2026-09-23-sessions-projects-keys/sessions-first.png)
![Background nested within Sessions](docs/qa_evidence/2026-09-23-sessions-projects-keys/sessions-background.png)
