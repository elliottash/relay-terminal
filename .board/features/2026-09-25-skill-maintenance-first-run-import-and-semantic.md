---
id: M91Y
type: work
status: planned
labels: [feature, skills, onboarding]
parent: SZ1H
blocked_by: [K26R]
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

## Plan
Refreshed 2026-09-26 against `053f4458`. **Blocked by #K26R** (content identity, aliases, requirement health) and **three owner questions** in the thread; the first-run hook also depends on #6VMF.

**Findings.**
- Canonical homes (#HS7V, needs-verification): project `.relay/skills`, personal `~/.config/relay/skills`; Claude, Codex and Warp trees stay discoverable as labelled sources (`skills.py:513`).
- Import machinery exists for Git sources: `import_skills_preview` / `confirm` and update checks in `skill_manage.py` copy selected skills at a pinned revision. #H7NF (executing, Codex pane) is building the Discover/import UI on it in `src/SkillRegistryView.*` and expects this card to supply semantic-review suggestions.
- First run today: `WindowManager::newWindowAt` opens terminal + Models pane; #6VMF (planned) replaces that with a Start pane whose "Import my settings" row lists what it found from Claude Code, Codex and Warp, boxes unchecked. On a fresh install there may be no model configured yet.
- Staleness and pass rates already exist per skill from the case ledger (#95VZ: `last_served`, `pass_rate_30`, `stale`).

**Steps (assuming the recommended answers).**
1. `skill_inventory` in the worker, no model: walk the supported sources, group by #K26R content hash, and return new skills, exact duplicates (with aliases), same-name different versions, requirement health and stale flags. Pure function, fixture-tested.
2. Semantic candidates: a cheap prefilter (name and description token overlap) proposes pairs; the maintenance skill has the model read each pair's instructions and say same / overlapping / different with a one-line reason. Candidates only; nothing merges.
3. `skill-maintenance`, a bundled skill: runs the inventory, presents a reviewable plan (add, alias, keep both, consolidate), applies only the rows the user ticks, never edits or deletes a source file, and writes a `cases.jsonl` row for the run. Consolidation writes a new Relay-owned skill that names its sources; the originals are then disabled in Relay, not touched.
4. First run: the model-free inventory feeds a "N skills" entry in #6VMF's import row; the semantic review is offered on the first agent turn once a model is configured. Until #6VMF lands, `/skill-maintenance` and a Globals › Skills entry run it.
5. Repeat runs report what changed since the last run (new sources, broken requirements, stale skills, new candidates); a Relay reminder can schedule them.

**Verify.** `tests/test_skill_inventory.py` over a four-source fixture (Relay, Claude, Codex, Warp) with exact and near duplicates and a broken requirement; the skill's own Try-it case (per #4EMF) drives one review in a fixture and asserts no source file changed; a manual first-run pass once #6VMF exists.

## Decisions
2026-09-26, owner ("I agree with the recs. proceed"): register external skills in place, copying only to edit or consolidate; model-free inventory at first launch, semantic review after a provider exists; first-run entry rides #6VMF's import row, with `/skill-maintenance` and Globals › Skills until then. The Plan's steps now stand as written; remaining gate is #K26R.
