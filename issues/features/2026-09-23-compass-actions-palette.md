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
links: {plans: [], commits: [b1f05a32b08c5a50feebdc14127cbfc0d36dec6c], evidence: [docs/qa_evidence/2026-09-23-compass-actions/, docs/qa_evidence/2026-09-23-compass-refinement/], related: [MAGP], github: null}
---
# Compass layout and directional navigation for Actions

## Issue
are you ready to plan and deliver the compass?

## Decisions
Owner chose the Compass proposal delivered in the conversation: Actions remains the name; the search box is home, Down reaches results, and Up, Left and Right reach fixed groups around it.

Owner, 2026-09-23 (after seeing the build): "i think for the top row, it might be better if they are horizontal. you click up to get the middle option, and then left or right to get one of the other top top options" — the Up arm becomes a horizontal row of its three destinations above the search box; Up reaches the middle one, Left and Right move along the row to the other two. The Left and Right arms are unchanged.

Owner, 2026-09-23: "can you amke those buttons a little sharper as well? they dont read as more important than the list of actions in the long list" — the destination buttons need more visual weight: full text ink instead of placeholder ink, a stronger border (highlight accent when focused), so they read as primary controls above the results list.

## Done means
- Actions opens with the search box at the centre of visible Up, Left and Right group arms; each group opens a dropdown containing the relevant live actions.
- From the search box, Down enters results, Up selects Models in the top row, and Left and Right enter their side arms. Left/Right traverse the top row; repeating a side-arm direction moves outward, the opposite direction moves home, and typing from a group returns to search.
- Left and Right still move the caret while editing a query, except at the respective text edge; Tab remains a full keyboard fallback.
- Small windows retain readable access to every group. Existing search, execution, shortcuts and close behavior remain usable.
- Agent, Models and Sessions form a horizontal row above search; Up from search selects Models, and Left/Right traverse the top row without entering a side arm.
- Compass buttons have a visibly stronger, sharper hierarchy than ordinary action rows in both dark and light themes.

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
`scripts/relay-build --target relay-actionpalette-tests relay` — passed after the top-row and styling refinement.
`QT_QPA_PLATFORM=offscreen build/relay-actionpalette-tests -silent` — 24 passed, 0 failed.
manual: `docs/qa_evidence/2026-09-23-compass-refinement/drive.sh` and `drive-light.sh` — isolated Xvfb captures in dark and light themes, including a 520 px window.

## Execution Summary
The Actions palette now places nine stable dropdown destinations around a wider central search field. Catalog actions route by key and section, including Board and Sessions in a fresh project; unfamiliar sections remain in Options. Directional keys, caret-edge behavior, Tab, menus and the compact layout are covered by focused tests. Dropdown placement stays within the available screen.

![Compass with nine destinations and results](docs/qa_evidence/2026-09-23-compass-actions/01-compass.png)

![Remote dropdown reached by Right, Right, Down](docs/qa_evidence/2026-09-23-compass-actions/02-remote-menu.png)

![Compact layout at 520 px window width](docs/qa_evidence/2026-09-23-compass-actions/03-narrow.png)

The Up arm now sits in one row. Up from search selects Models; Left and Right stay in the row and reach Agent and Sessions. The group buttons use full text ink, a stronger tinted fill and border, bolder labels, and tighter corners. Wide, narrow, dark, and light captures show the revised hierarchy without clipped button labels.

![Horizontal top row and stronger buttons in dark theme](docs/qa_evidence/2026-09-23-compass-refinement/01-compass.png)

![The same controls in a 520 px window](docs/qa_evidence/2026-09-23-compass-refinement/03-narrow.png)

![Button hierarchy in light theme](docs/qa_evidence/2026-09-23-compass-refinement/04-light.png)
