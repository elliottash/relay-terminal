---
id: TVE1
type: work
status: ready
labels: [feature]
component: [gui]
milestone: beta
workstream: switchboard
rank: '3'
created: '2026-09-19'
acceptance: the tab bar and the Sessions list can each be grouped by `relay::projects::keyFor`, with an ungrouped view still available and nothing changing for a user who never attaches a tab
source: 'owner, 2026-09-18, decision 6 of #916B ("The tab list and the conversation list group by project from the start", Warp''s most-asked missing feature, #9875); split out on 2026-09-19 when #916B shipped the picker, the chip, the Options list and the Sessions filter without it'
links: {plans: [], commits: [], evidence: [], related: [916B, JN7X], github: null}
---
# Group tabs and the conversation list by project

## Issue

Decision 6 of #916B, deferred there so the rest of that card could ship: "the tab list and the
conversation list group by project from the start (Warp's most-asked missing feature, #9875)".
The Sessions pane already has a "By project" grouping (by the row's `project` *name*) and, since
#916B, a Project chooser that filters by the project's folder; the tab bar has nothing of the kind.
What is wanted is one grouping key for both — `relay::projects::keyFor(path)`, which is also the
backend's `workspace_digest` — so a project renamed on disk, or two projects with the same
directory name, group the way the registry sees them.

## Tasks

- [ ] the tab bar: attached tabs grouped by project (a separator or a label per group; unattached tabs stay where they are, ungrouped), with the grouping off by default or behind one Options row — a product decision for the owner <!-- t:tb -->
- [ ] the Sessions pane: "By project" groups by `keyFor` of the row's workspace, with the known project's name as the group title, instead of by the bare `project` name <!-- t:sp -->
- [ ] one place both read the key from, so the two views never disagree <!-- t:kk -->
- [ ] shortcut hints for whatever gains a key (WARP.md standing rule) <!-- t:hh -->
