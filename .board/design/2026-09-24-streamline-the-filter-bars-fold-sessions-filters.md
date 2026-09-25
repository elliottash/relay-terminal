---
id: 1Q5V
type: work
status: needs-verification
labels: [feature, design, sessions, switchboard, ui]
assignee: agent
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [ai-visual], human: none, criteria: 'Sessions'' top is one row + chips, title/id/copy on one spanning line, no ID line; Board has no label chips row; suites green.', sign_off: none, effort: medium, stakes: nuisance}
source: Relay pane, 2026-09-24
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-1q5v-one-row-and-spanning/], related: [MXMG, P7SJ], github: null}
---
# Streamline the filter bars: fold Sessions' filters into search, drop the Board's label chips row

## Issue
5 yes, can you file a card for that you observed there. add your opinion about how to streamline the top of sessions and also probably remove the list of labels from the top of the board.

## Discussion points
What is there today (2026-09-24, code reading):

- **Sessions & Projects** (`src/Conversations.cpp`): a search field plus a filter row of eight combo
  boxes — Scope, Kind, Project, Model, Date, Branch, Group, Sort — and a "More ▾" menu of three-state
  filters. The search already speaks `"a phrase"`, `file:`, `model:`, `branch:`, `is:pinned`, `-not`.
- **Board list page** (`src/BoardPane.cpp`): a filter line ("any word in the card, label:bug, …"), a
  wrapping row of section checkboxes, and a row of label chips (#VKFV).

**My opinion.**

1. **Sessions: one row, two combos, everything else typed.** Keep the search field (it grows with
   the pane) and keep **Project** and **Model** as combo boxes — those two browse a list you may not
   know by heart, which is what a combo is for. Move the rest into the field and the More menu:
   Date → a `when:` token (or stays in More), Branch → the `branch:` token it already has, Group and
   Sort → a small "Sort ▾" next to More, Scope/Kind → they duplicate what the pane's own tabs and
   the Projects/Globals tabs already choose. Active filters should show as one removable chip each
   under the field (click the × to clear), so what the field is hiding stays visible. That takes the
   top of Sessions from two-plus rows to one, and makes it a *search bar*, which is the opposite
   shape from the Board's *control strip*.
2. **Board: drop the label chips row.** The chips duplicate the filter field (`label:bug` already
   works) and a click on any row's label badge already copies that filter term (#S53Z). Removing
   the row reclaims a line on every board pane and the one people scan is the cards. If label
   discovery is the worry, offering the known labels as completions when the field holds `label:`
   covers it without a permanent row.
3. Keep the Board's **section checkboxes** — they choose what the list shows at all (the stage
   sections), so they are view controls, not filters; they can share the filter line's row when
   there is room and wrap only when the pane is narrow.

## Done means
- The top of Sessions & Projects is one row: the search field, the **Project** and **Model** combos, a small **Sort ▾** menu (grouping + sort), and the existing **More ▾**. The Scope, Kind, Date, Branch, Group and Sort combos are gone from the row; Branch is typed (`branch:`), Date/scope/kind live in More where they are still reachable, grouping/sort in Sort ▾.
- Active non-token filters show as removable chips under the field (click the × to clear).
- The session title line spans the other columns; the session id and a copy button follow on that same line; the separate "ID: xxx" line is gone.
- The Board's label chips row is gone; the section checkboxes stay; `label:` in the filter field still filters (that behaviour is unchanged).
- `ctest -R conversations` and the board pane suites pass; screenshots of both tops in the evidence folder.

## Execution Summary
Implemented 2026-09-24, all three of the owner's agreements plus the title/id ask.

- **One-row top** (`src/Conversations.cpp`): search, Project, Model, **Sort ▾**, **More ▾** and the subagent checkbox on one row. The six combos that left the row (Scope, Kind, Date, Branch, Group, Sort) stay as hidden state holders the menus drive — `sweepComboIntoMenu()` mirrors a combo as checkable actions, so a menu choice fires the combo's own `currentIndexChanged` → requery. Scope/Source/Time are submenus in More; Branch is its own submenu, refilled with the facets and hidden while only one branch exists; the `branch:` token types the same thing. Deviation from the written opinion: Scope and Kind are in More rather than deleted — nothing becomes unreachable.
- **Chips**: `rebuildChips` now adds one removable chip per non-default combo (scope/source/project/model/time/branch), next to the existing operator chips; defaults with data of their own ("This project", "Any time") do not chip.
- **Rows**: session rows `spanFirstColumn`, the header is hidden (the columns' cells are no longer painted), and the delegate draws the title across the full width with the session id, a ⧉ that copies the id to the clipboard (click + tooltip), and the relative time after it on the same line. The "ID: …" line is gone. The updated/turns/requests/model/tokens/recap column *values* ride the tags/sub as before via the roles; turns/tokens/requests are no longer painted as columns.
- **Board** (`src/BoardPane.{h,cpp}`): the #VKFV label chips row is gone (creation, fill, restore and members); section checkboxes stay; `m_labelPicked`/`setLabelFilter` stay so typed `label:` and restored sessions still narrow the list.
- Tests updated to the new surfaces: `sessionsDropdownsRespondToMouseChoices` drives Project/Model by mouse and the menus by action; `headerClickSortsByThatColumn` drives the `sectionClicked` signal (the header is hidden; wiring unchanged); the branch assertions check the More submenu; `navigationSurvivesReload` writes a `board-top.png` when `RELAY_SHOT_DIR` is set (the conversations suite's own pattern).
- Known unrelated breakage in the tree: `boardworkspace` and `boardexecute` tests fail on another session's in-flight RelayWindow/`actionButton` refactor — not this change, not touched.

## Tests
- `ctest -R 'conversations|boardpane|boardmodel|boardfilter|boardsections|closedlist'` — 100% passed, 0 failed (includes the rewritten `sessionsDropdownsRespondToMouseChoices`, `headerClickSortsByThatColumn`, `filtersSendTheirOwnFieldsAndClear`, `chipsFollowTheParsedQueryAndTakeItBack`).
- `scripts/relay-build --target relay` — green.
- Evidence shots in `docs/qa_evidence/2026-09-24-1q5v-one-row-and-spanning/` (sessions-list, sessions-tokens, by-date, by-project, board-top + report).
- Not runnable here: `boardworkspace`, `boardexecute` — another session's in-flight refactor breaks them independently of this change (see Execution Summary).
