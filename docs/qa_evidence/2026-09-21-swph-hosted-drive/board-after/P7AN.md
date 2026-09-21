---
id: P7AN
type: work
status: executing
labels: [feature]
assignee: agent
session: 4831bb72-b88c-420c-9d45-7033230576d0
rank: 6j
created: '2026-09-20'
acceptance: a second request within the hour does not call the feed
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Cache the tide feed for an hour

## Issue
the tide feed is slow, cache it

## Plan
**Goal.** One feed call an hour per port, and a stale answer rather than none.

1. Wrap `next_high_tide` in a cache keyed by port.
2. Expire entries after sixty minutes, on a clock the test can set.
3. Serve the stale entry when the feed is down, and say so on the page.

Rewritten by the fake planner (PLANNED-BY-FAKE).

## Tasks

- [ ] 1 The cache and its expiry <!-- t:c1 -->
- [ ] 2 A test with a fake clock <!-- t:c2 -->
