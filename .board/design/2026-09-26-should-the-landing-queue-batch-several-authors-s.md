---
id: 55HG
type: work
status: discussing
labels: [feature, design, land, workflow]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
source: Claude guest pane, 2026-09-26
links: {plans: [], commits: [], evidence: [], related: [3MH4, BP15], github: null}
---
# Should the landing queue batch several authors' submissions into one gated candidate to shrink the queue?

## Issue
When several code jobs are queued, the publisher gates and publishes them one at a time. Should an agent (or the publisher) merge the queued work of several contributors into one combined candidate, gate it once, and publish all of them together, bisecting or falling back to one-by-one when the combined gate fails?

> whether, when build jobs are queued, should an agent try to merge the work of mutliple contributors to shrink the queue.
> — elliott · [session:9e7d96426aef4f42b8f052f757109540](relay://session/9e7d96426aef4f42b8f052f757109540) · 2026-09-26
