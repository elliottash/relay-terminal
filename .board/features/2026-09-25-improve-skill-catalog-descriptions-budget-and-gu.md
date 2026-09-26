---
id: G8JN
type: work
status: planned
labels: [feature, skills, worker]
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [9FX8, GSK7], github: null}
---
# Improve skill catalog descriptions, budget, and guest supplements

## Issue
Use short or the first complete description sentence for each skill, raise the catalog budget to 8 KiB, and avoid repeating the guest harness's own skill tree in Relay's supplemental block. Test the prompt rendering and budget behavior.

## Done means
Catalog rows use `short:` or a complete first sentence within an 8 KiB total budget. Guest supplemental skills do not repeat the harness's own home-tree catalog. Tests cover truncation, over-budget behavior and guest-specific source filtering.
