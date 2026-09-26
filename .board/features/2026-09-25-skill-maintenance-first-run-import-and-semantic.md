---
id: M91Y
type: work
status: planned
labels: [feature, skills, onboarding]
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [HS7V, GSK7, 9FX8], github: null}
---
# Skill maintenance: first-run import and semantic catalog review

## Issue
Create a bundled skill maintenance skill invoked in first-install onboarding to discover skills from other agents and offer import into Relay's canonical catalog. Maintain provenance, versions, exact duplicate collapsing, semantic duplicate suggestions, requirement health, and staleness over time. Show comparisons and obtain a user choice before consolidating semantically similar skills; never silently overwrite source instructions.

> but for 2, we should de-dupe semantically as well right? we should have a skill maintenance skill, that runs first when you install relay, it brings in your skills from other agents. but it will also maintain your skill catalog.
> — elliott · [session:fc4b907796bb418994c523cffca15623](relay://session/fc4b907796bb418994c523cffca15623) · 2026-09-25

## Done means
First-install onboarding invokes the maintenance skill, inventories supported agent skill sources and offers a reviewable import into Relay's canonical catalog. Existing files are preserved. Exact duplicates collapse with source aliases; semantic candidates show side-by-side instructions and provenance and require a choice before consolidation. Repeat runs report stale skills, broken requirements and new candidates without changing source skills silently.
