---
id: K26R
type: work
status: planned
labels: [feature, skills, worker]
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [95VZ, 9FX8, HS7V], github: null}
---
# Skill requirements and tool-aware availability in the catalog

## Issue
Add structured requires to skills, check tools and local prerequisites when indexing, and show why a skill is unavailable. Replace the Warp product-bundle discovery and fixed exclude list with source- and tool-aware rules; retain provenance and exact-content identity.

## Done means
Skill manifests accept declared program, environment, secret and tool requirements. The index distinguishes runnable, hidden-for-missing-tool, and visible-with-action-needed. Exact content duplicates have one identity with every source retained; same-name differing versions remain visible in provenance. Tests cover missing tools, missing local prerequisites and source discovery.
