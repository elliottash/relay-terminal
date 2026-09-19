---
id: TASK-2
title: 'Harden test lifecycle, timeout, and cleanup handling'
status: In Progress
assignee:
  - '@test-hygiene'
created_date: '2026-07-11 09:20'
updated_date: '2026-07-11 11:06'
labels: []
dependencies: [TASK-1]
parent_task_id: TASK-1
priority: medium
milestone: beta-2
ordinal: 173000
type: chore
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Tests leak processes when a timeout fires.
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes
<!-- SECTION:NOTES:BEGIN -->
The leak is in the fixture, not the runner.
<!-- SECTION:NOTES:END -->
