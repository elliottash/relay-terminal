---
id: TRC1
type: work
status: inbox
labels: []
created: '2026-09-19'
---
# show the thinking trace in the card

## Issue

In switchboard cards — when discussing with the agent, and especially in plan mode, show
the thinking traces same as in the terminals. The thinking should be in the thread in the
card, so that the agent can ask questions and they read after the reasoning.

## Plan
1. Render the reasoning tail in the card's thread, under a `thinking…` header that settles to `thought for N s`.
2. Seal the block above any thread entry that lands under it, so a question reads after the thinking it came from.
3. Never write the trace to the card file.
