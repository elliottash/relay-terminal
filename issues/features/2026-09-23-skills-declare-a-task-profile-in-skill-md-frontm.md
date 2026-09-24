---
id: MSJ0
type: work
status: planned
labels: [feature, switchboard, skills, qa]
component: [worker, gui]
parent: 1QKM
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [1QKM, BX7B], github: null}
---
# Skills declare a task profile in SKILL.md frontmatter; a card worked under a skill inherits its verify defaults and effort

## Issue
so skill development and maintenance could be an important new component we need in the switchboard. [...] agent should make this suggestion, and also the effort level, per task.

[...] yes, document it, and lets build all the functionality, and we can experiment with how to phase in complexity without overwhelming the user

## Done means
- A `SKILL.md` may carry a `profile:` frontmatter block (a `|` block of `key: value` lines, since `skills.parse_frontmatter` is a flat parser) with the card `verify` keys (#WFRA: `artifact`, `primary`, `also`, `human`, `criteria`, `sample`, `sign_off`, `effort`, `stakes`, `blast`) plus the server-only factors `regularity` (routine|mixed|novel), `executable` (yes|no), `rot` (low|medium|high, with an optional `rot_reason` such as "drives Editorial Manager UI"), `confidential` (yes|no), `money` (yes|no). Unknown keys are reported in `skills_skipped`-style warnings, not fatal.
- `Skill` gains `profile: dict`; `skills_list` returns it; `load_skill`'s result carries it as a `profile` object so the agent sees it when it loads the skill; the catalogue line is unchanged (no prompt cost).
- When a pane's turn has loaded a skill with a profile and then calls `board_claim` or `board_update_card` on a card with no `verify` block, the worker fills the card's `verify` from the profile's verify keys and says so in the tool result ("verify defaulted from skill <id>"); an explicit `fields.verify` always wins.
- Six of the owner's bundled skills get a profile as the worked examples: `deliver` (script primary, human none, effort medium), plus in `.relay/skills` templates for a referee report, a domain purchase, a slide deck, a data-analysis run and a server health check, each one a five-to-ten-line profile matching the two worked profiles on #1QKM §7.
- `SkillsDialog` shows the profile's primary mode and human flag as a second line under the description.
- Tests: `tests/test_skills.py` (parse, unknown key, list, load), `tests/test_board_tools.py` (default-from-skill, explicit wins). Failure shows as a card claimed under a profiled skill with an empty `verify`.
