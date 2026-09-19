---
id: TASK-1
title: Show due dates on the surfaces that omit them
status: To Do
assignee: []
created_date: '2026-09-02 20:31'
labels: [ui, due-dates]
dependencies: []
priority: high
ordinal: 313000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A due date set through any surface is silently absent from several read surfaces: board
export and non-TTY board render a task row with no date.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Board export and non-TTY board output show a task due date when one is set
- [ ] #2 Surfaces without a due date are unchanged
<!-- AC:END -->

## Implementation Plan
- [x] Find every surface that renders a task row
- [ ] Add the column
