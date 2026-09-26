---
id: EHBH
type: work
status: planned
labels: [bug, skills, tests]
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-25'
source: 'pane observing #9FX8 work, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Bundled disk-hygiene description is 153 chars, three over skills.MAX_DESCRIPTION, so test_skills fails at main

## Issue
Unrelated fault noticed while landing #9FX8's worker step: tests.test_skills is red at main. (Measured during #9FX8 work, filed per board rule 6 — not fixed, not a detour.)
Measured 2026-09-25, main at d138dc63:

```
PYTHONPATH=backend python3 -m unittest tests.test_skills
FAIL: test_disk_hygiene_is_bundled_and_names_the_tool_that_checks_before_it_deletes (tests.test_skills.BundledSkillTests)
AssertionError: 153 not less than or equal to 150
```

`backend/relay_core/skills_bundled/disk-hygiene/SKILL.md` (unmodified in the tree, so this is at HEAD) has a 153-character `description:`; `skills.MAX_DESCRIPTION` is 150. Introduced by 0da66145 "Relay owns agent scratch: a ledger for every temp dir (#DVV2)" — the card that added the skill — which landed without running `tests.test_skills`. The fix is either three characters off the description or a bound the #DVV2 owner picks deliberately; whoever owns #DVV2 should say which.

## Done means
`tests.test_skills` is green at main again — in particular `test_disk_hygiene_is_bundled_and_names_the_tool_that_checks_before_it_deletes` — and every bundled skill's `description:` is at most `skills.MAX_DESCRIPTION` (150). Only the disk-hygiene `description:` line changes: the skill's `short:` trigger line, body and commands stay byte-identical, and the description still says bounded scratch, Relay owning temp dirs, ask and release. Failure would be the same `AssertionError: 153 not less than or equal to 150` at main, or a description cut to the point of saying nothing.

## Plan
### Goal
Make `tests.test_skills` pass at main again by fitting the bundled disk-hygiene `description:` inside `skills.MAX_DESCRIPTION` (150), touching nothing else in the skill.

### Findings
- `backend/relay_core/skills_bundled/disk-hygiene/SKILL.md` line 3 has a 153-char `description:`; `backend/relay_core/skills.py:27` sets `MAX_DESCRIPTION = 150`.
- That bound is a prompt-budget guard enforced only by tests: `MAX_DESCRIPTION` is referenced nowhere at runtime except its own definition — `tests/test_skills.py:334` is the assertion that fails, and nothing truncates or rejects an over-long description at load time.
- The catalogue line the agent sees on every request is the frontmatter `short:` ("Relay owns scratch: …", 79 chars), not the description — so shortening the description changes no prompt text except inside the loaded skill.
- disk-hygiene (added by 0da66145, #DVV2) is the only bundled skill over the bound: deliver 140, guest-account-setup 135, local-model-setup 142. The description text exists in exactly one file (no packaging or docs copy).

### Steps
1. `python3 scripts/land.py begin <session> backend/relay_core/skills_bundled/disk-hygiene/SKILL.md` — the one path this card owns.
2. Replace only the `description:` line with a wording of at most 150 characters that keeps the three ideas (bounded scratch, Relay owns every temp dir, ask then release), for example this 144-char line:
   `Keep agent scratch (tree copies, build dirs, test workspaces) bounded; Relay owns every temp dir through the scratch ledger: ask, then release.`
   Leave `short:` and the body byte-identical.
3. `PYTHONPATH=backend python3 -m unittest tests.test_skills` — must be fully green; the same test also checks the body still names `relay-scratch check`, `relay-scratch gc --apply`, `live process` and `Never delete what you did not make`, which is the guard against trimming the wrong thing.
4. `python3 scripts/land.py commit <session> -m "Shorten bundled disk-hygiene description to fit skills.MAX_DESCRIPTION (#EHBH)"`. No C++ or build paths, so no verify build; do not run the full test suites (owner's rule — targeted tests only).
5. Record the passing test output in `## Tests`, then move the card to needs-verification citing this evidence.

### Risks
- The issue leaves the choice — trim the description, or raise `MAX_DESCRIPTION` — to #DVV2's owner. **Recommendation: trim.** The 150 bound is a deliberate budget constant, every other bundled skill fits under it, and raising it changes no runtime behaviour (nothing reads the constant at runtime) while weakening the guard for every future skill. If the owner would rather keep the exact 153-char sentence, Run instead raises `MAX_DESCRIPTION` in `backend/relay_core/skills.py` and the test passes unmodified — say so before Run.
- The replacement wording above is this plan's proposal, not the owner's words; any ≤150 phrasing that keeps the three ideas is acceptable.

### Verify
- `PYTHONPATH=backend python3 -m unittest tests.test_skills` green at main after the commit (its own assertion checks ≤ 150 — no extra tooling needed).
- `git show --stat HEAD` lists exactly one path: `backend/relay_core/skills_bundled/disk-hygiene/SKILL.md`.
