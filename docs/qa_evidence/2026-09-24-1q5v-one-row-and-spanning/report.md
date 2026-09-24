# #1Q5V — one-row tops, spanning session titles with id + copy

Captured 2026-09-24 from the working tree of this change (offscreen test harness).

- `sessions-list.png`, `sessions-tokens.png` — `relay-conversations-tests listScreenshot`
  (`RELAY_SHOT_DIR`): the Sessions & Projects top as one row — search, Project, Model,
  Sort ▾, More ▾, the subagent checkbox — and the list below it, where each session row
  spans the width: bold title, then the session id with its ⧉ copy glyph and the relative
  time on the same line; tags and summary on the second line; no "ID:" line.
- `by-date.png`, `by-project.png` — `sessionsDropdownsRespondToMouseChoices` with
  `RELAY_SHOT_DIR`: grouping chosen through the Sort ▾ menu, one row of chrome above.
- `board-top.png` — `relay-boardpane-tests navigationSurvivesReload` with `RELAY_SHOT_DIR`:
  the Board list page top — section checkboxes and the engraved header, no label chips row
  (the #VKFV chips are gone; `label:` in the filter field still filters).

Suites: `ctest -R 'conversations|boardpane|boardmodel|boardfilter|boardsections|closedlist'`
100% passed. `boardworkspace` and `boardexecute` are not runnable in this tree: another
session's in-flight edits (RelayWindow refactor, actionButton work) break them before and
independently of this change.
