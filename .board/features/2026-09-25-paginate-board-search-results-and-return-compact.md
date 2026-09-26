---
id: ZS9B
type: work
status: planned
labels: [feature, switchboard]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: Relay conversation, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Paginate Board search results and return compact rows

## Issue
Keep the board_list per-page cap at 50, add a cursor or offset so callers can retrieve later matches, and reduce each search row to the fields needed to identify a card. Preserve the total match count and make broad searches usable without large tool responses.

> add a card with that
> — elliott · [session:3d9f0c2a853a40a180759676b456c60f](relay://session/3d9f0c2a853a40a180759676b456c60f) · 2026-09-25
