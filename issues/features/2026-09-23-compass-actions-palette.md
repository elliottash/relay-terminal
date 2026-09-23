---
id: CMPA
type: work
status: needs-verification
labels: [feature, actions, keyboard]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: n
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23
links: {plans: [], commits: [b1f05a32b08c5a50feebdc14127cbfc0d36dec6c], evidence: [docs/qa_evidence/2026-09-23-compass-actions/], related: [MAGP], github: null}
---
# Compass layout and directional navigation for Actions

## Issue
are you ready to plan and deliver the compass?

## Decisions
Owner chose the Compass proposal delivered in the conversation: Actions remains the name; the search box is home, Down reaches results, and Up, Left and Right reach fixed groups around it.

## Done means
- Actions opens with the search box at the centre of visible Up, Left and Right group arms; each group opens a dropdown containing the relevant live actions.
- From the search box, Down enters results and Up, Left and Right enter their respective arms; repeating an arm direction moves outward, the opposite direction moves home, and typing from a group returns to search.
- Left and Right still move the caret while editing a query, except at the respective text edge; Tab remains a full keyboard fallback.
- Small windows retain readable access to every group. Existing search, execution, shortcuts and close behavior remain usable.

## Plan
**Goal.** Turn the current group-button row into the Compass around the Actions search box.

**Findings.** `src/ActionPalette.cpp` builds the row, keyboard filter and dropdowns. `src/RelayWindow.h` supplies sectioned catalog items. `tests/actionpalette_test.cpp` drives the widget.

**Steps.**
1. Group the current catalog sections into stable Compass destinations, preserving all actions and current shortcut text.
2. Place the Up, Left and Right destinations around search, with a compact layout for narrow windows and a focus cue.
3. Implement home, results and arm navigation, including caret edges, menus, typing and Tab.
4. Add focused widget tests and capture the live layout under Xvfb.

**Risks.** Catalog sections may grow; a fallback group must catch unfamiliar sections. Qt menu focus and window resize need live checks.

**Verify.** `scripts/relay-build --target actionpalette-tests`, the focused `actionpalette` ctest, and an isolated Xvfb screenshot with a populated catalog.

## Tests
`scripts/relay-build --target relay-actionpalette-tests relay` — passed.
`QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^actionpalette$' --output-on-failure` — passed.
manual: `docs/qa_evidence/2026-09-23-compass-actions/drive.sh` — isolated Xvfb open, menu navigation, and narrow resize.

## Execution Summary
The Actions palette now places nine stable dropdown destinations around a wider central search field. Catalog actions route by key and section, including Board and Sessions in a fresh project; unfamiliar sections remain in Options. Directional keys, caret-edge behavior, Tab, menus and the compact layout are covered by focused tests. Dropdown placement stays within the available screen.

![Compass with nine destinations and results](docs/qa_evidence/2026-09-23-compass-actions/01-compass.png)

![Remote dropdown reached by Right, Right, Down](docs/qa_evidence/2026-09-23-compass-actions/02-remote-menu.png)

![Compact layout at 520 px window width](docs/qa_evidence/2026-09-23-compass-actions/03-narrow.png)
