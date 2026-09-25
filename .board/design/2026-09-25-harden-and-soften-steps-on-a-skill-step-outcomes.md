---
id: 4YKJ
type: work
status: discussing
labels: [feature, design, skills, qa, worker]
component: [worker, skills]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'on a ledger with ten cases of one skill, the harden hint names the step that was routine every time and nothing else', sign_off: none, effort: medium, stakes: nuisance, blast: case}
source: 'owner, Relay conversation, 2026-09-23 and 2026-09-25; #1QKM §2 the codification axis'
links: {plans: [], commits: [], evidence: [], related: [1QKM, 95VZ, MSJ0, MEPR, SZ1H], github: null}
---
# Harden and soften: steps on a skill, step outcomes on a case, and the hint that a routine step should become code (or a rigid step needs judgement)

## Issue
i dont want to do anything formal here. i just want to use that to think about the factors that characteritze knowledge tasks, that we can then encode in AI skills or relay programs [...] analyze these issues and add comprehensive plans to cards. clarify anything with me with questions.

## Plan
**Goal.** Give the codification axis of #1QKM §2 something to compute from. A skill may declare its **steps**; a case row may say, per step, whether it was routine or needed judgement and whether a program step fell back; from those the ledger names **harden candidates** (a step routine in every one of the last N cases: make it code) and **soften candidates** (a program step that keeps falling back: insert an intelligent joint). Nothing formal (owner, 2026-09-23): counts, not costs. The hint reaches the agent in `skills_list` and the person only on the skill page (#9FX8) as one line with an action.

**Findings.**
- `skills.PROFILE_VOCAB` has `regularity: routine | mixed | novel` for the whole skill; nothing at step granularity. `cases.FIELDS` has `escalated {yes, to}` for the whole case, no steps.
- The program end of the axis exists: task plugins (#MEPR) declare tools and runners; `scripts/` under a skill folder are programs a skill wraps (`jle-editing`'s Playwright scripts, `land.py` under `deliver`). #SZ1H recommends a Relay `skill-creator` that writes a skill with `profile`, `requires` and a `Try it` case — the natural home for "write the program step".
- The ledger has no volume yet: on this machine, rows come from card verdicts, Try it and turn-end `pending` rows. Step data only pays off after tens of cases of one skill, which is why this card follows #9FX8 and #G9ZD rather than leading them.

**Steps.**
1. **`steps:` in SKILL.md front matter** (optional): an ordered list, each `name`, `kind: program | skill | person`, optional `program` (the script or tool it calls). Parsed beside `profile` in `skills.py` (`parse_steps`, warnings not fatal), carried on `skills_list` / `load_skill`. Tests in `tests/test_skills.py`.
2. **`steps` on a case row** (optional, capped at 20 entries and the row's 4 KiB): `[{name, kind, routine: bool, fallback: bool}]`. `board_case` and `record_case` accept it; `record_turn_cases` fills it from the loaded skill's declared steps when the agent reported them through a new optional `steps` argument on the card write that ends the turn (else the row carries none). `routine: true` is evidence only when the step's `kind` is `program` (a tool call happened) or the agent says so explicitly; the hint is advisory either way.
3. **`cases.harden_candidates(records, skill, n=10)`**: steps of `kind: skill` with `routine: true` in each of the last `n` rows that carry steps → list of `{step, cases}`. **`cases.soften_candidates(records, skill, n=10, k=3)`**: steps of `kind: program` with `fallback: true` in ≥ `k` of the last `n` → `{step, fallbacks, cases}`. Pure, tested with synthetic rows; a skill with fewer than `n` stepped rows returns nothing.
4. **Where it shows.** `skills_list` items gain `harden` / `soften` lists (agent-facing). The skill page (#9FX8) draws one line per candidate — "step 'find the deadline' was routine in the last 12 cases" — with **Harden** / **Soften** actions that open a console with the `skill-creator` (or `deliver`) prompt: write the program step under the skill's folder (or a task-plugin tool), set `server:` on the card, add the step's `program` to `steps:`. No automatic rewriting of a skill.
5. **Docs.** Protocol 19.22 (row shape) and the skills section (§11) gain `steps`; `docs/TASK-PLUGINS.md` gets a paragraph on a skill step graduating to a plugin tool.

**Risks.**
- Self-reported `routine` is the weak link: a model that calls every step routine produces a harden hint for everything. Mitigation in step 2 (program-kind evidence) and in the hint being advisory; the calibration of #GW74 does not cover it.
- Row size: 20 steps × ~60 bytes fits the 4 KiB cap; longer skills declare fewer, coarser steps.
- Sequencing: worthless before the ledger has volume; recommendation below is to hold until #9FX8 and #G9ZD land and one skill has 30 cases.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_cases.StepTests tests.test_skills.StepTests`; a staged ledger of 12 rows for one skill with one routine step and one falling-back program step, and `skills_list` naming exactly those two.

## Done means
- A SKILL.md may declare `steps:`; a case row may carry `steps` with routine/fallback per step; both parse with warnings, never fatally.
- `skills_list` names harden candidates (a skill step routine in each of the last N stepped cases) and soften candidates (a program step falling back in k of N); the skill page shows one line per candidate with Harden / Soften actions that open a console prompt, and no skill is rewritten automatically.
- Failure looks like: a candidate named from fewer than N cases, a routine flag counted from a step with no evidence, or a row over 4 KiB.
