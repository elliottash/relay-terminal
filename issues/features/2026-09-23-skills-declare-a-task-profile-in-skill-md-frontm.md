---
id: MSJ0
type: work
status: needs-verification
assignee: agent
labels: [feature, switchboard, skills, qa]
component: [worker]
parent: 1QKM
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
source: owner, Relay conversation, 2026-09-23
implemented_by: anthropic/claude-fable-5-1
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: 'the six example profiles parse clean with no warnings, and an unknown key or bad value warns without dropping the skill', sign_off: none, effort: medium}
links: {plans: [], commits: [52e22a8e04d5395fea06bf5608f533c5198219d5, 333091b16026ad04bc0d267c057d41193993494a, 1701a68f13e6bdb43474c7b3f66bbc1d5d586cf7], evidence: [], related: [1QKM, BX7B, WFRA], github: null}
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
- ~~`SkillsDialog` shows the profile's primary mode and human flag as a second line under the description.~~ Dropped by owner steer 2026-09-23 (no dialog line on this card); see Execution Summary.
- Tests: `tests/test_skills.py` (parse, unknown key, list, load), `tests/test_board_tools.py` (default-from-skill, explicit wins). Failure shows as a card claimed under a profiled skill with an empty `verify`.

## Plan
**Goal.** A `SKILL.md` can declare its task profile; the worker carries it in `skills_list` / `load_skill` without touching the catalogue line.
**Findings.** `skills.parse_frontmatter` is flat and already handles `|` blocks, so `profile: |` is the format; `skill_manage.list_skills` builds the `skills_list` items and `SkillIndex.load_skill` the `load_skill` result; `skills_bundled/` is indexed as live skills, so templates cannot live in a subfolder there.
**Steps.** (1) `parse_profile` with the vocabularies, `Skill.profile` and `profile_warnings`, warnings into `skipped`; (2) `list_skills` items and `load_skill` results carry `profile`; (3) the `deliver` profile and five template skills under `docs/skills-examples/`; (4) tests in `tests/test_skills.py`; (5) three sentences in `docs/ARCHITECTURE.md`.
**Risks.** The `board_claim`/`board_update_card` verify-default hook needs `board_tools.py`, which another session owns; it is deferred to a follow-up card after that lands.
**Verify.** `tests/test_skills.py` ProfileTests.

## Tasks
- [x] `parse_profile`, `Skill.profile`, `profile_warnings`, warnings beside `skipped` <!-- t:p1 -->
- [x] `skills_list` items and `load_skill` results carry `profile` <!-- t:p2 -->
- [x] `deliver` profile and five template skills in `docs/skills-examples/` <!-- t:p3 -->
- [x] tests: `tests/test_skills.py` ProfileTests <!-- t:p5 -->
- [x] `docs/ARCHITECTURE.md` Skills section <!-- t:p6 -->
- [x] `board_claim`/`board_update_card` fill `verify` from a loaded skill's profile <!-- t:p7 -->

## Execution Summary
- `backend/relay_core/skills.py`: `PROFILE_VOCAB`, `PROFILE_TEXT`, `PROFILE_VERIFY_KEYS` and `parse_profile(text, warnings)`; `Skill.profile` and `Skill.profile_warnings`; `SkillIndex.load` parses the block and appends each warning to `skipped` as `<name>: profile … (skill still loads)`, so it reaches `configured.skills_skipped` and the dialog's status tooltip with no change to `worker.py`; `load_skill` returns `profile`. The catalogue line (`trigger`) and `prompt_section` are untouched.
- `backend/relay_core/skill_manage.py`: `list_skills` items carry `profile` (when non-empty) and `profile_warnings`.
- `backend/relay_core/skills_bundled/deliver/SKILL.md`: the profile block (script primary, human none, effort medium, stakes rework, blast capability, routine, executable yes, rot low, confidential no, money no).
- `docs/skills-examples/{referee-report,domain-purchase,slide-deck,analysis-run,server-health-check}/SKILL.md`: five template skills, each a short procedure with a full profile matching #1QKM §7's worked profiles. They are under `docs/` rather than `skills_bundled/templates/` because `skills_bundled/` is indexed as live skills and a `templates/` folder there would show up as a skipped "no SKILL.md" entry in every session; the card's `.relay/skills` wording was read the same way.
- `docs/ARCHITECTURE.md` "### Skills": the profile block, where it rides, how warnings surface.
- **Owner steer 2026-09-23: no dialog line.** The Done-means line about a second line in `src/SkillsDialog` was dropped after the code was written and reverted: "ideally, most of this is just in the agent's work and the user doesn't see it directly." The profile is agent-facing data — `skills_list` items, `load_skill`'s result, and the examples — with no C++ change on this card.
- **The verify-default hook (t:p7, landed after #WFRA's `board_tools.py`):** `ToolContext.skills` is the per-turn record of the skills in context — `Agent` fills it from every `load_skill` result and from the `kind: skill` attachments of a `/name` invocation, right after `BoardTools.begin_turn` clears it. `BoardTools._verify_from_skills` reads it on `board_claim` and on `board_update_card` when the work card has no `verify` and no explicit `fields.verify` was passed: exactly one loaded skill with verify keys writes the card's `verify` from `PROFILE_VERIFY_KEYS` (only the keys present, through `validate_verify`), with `verify_defaulted_from: <id>` and the note `verify defaulted from skill <id>` in the result, the claim summary and the update's changes; two or more profiled skills default nothing and the note names them; a profile that fails validation (say `human: required` without `criteria`) is reported in the note, not written. An explicit `fields.verify` always wins and gets no note. Agent-facing only: the note is in the tool result and the thread event, nowhere a user reads. Documented in protocol 19.21. Commit 333091b1.
- **Found on the way (commit 1701a68f):** this card's own `verify.criteria` held a comma, `board.yaml_value` wrote it unquoted inside the flow map, and `Board.cards()` raised on re-read for the whole board. `_flow_item` now quotes a scalar holding a comma or a bracket inside `[...]`/`{...}` (a bare colon stays plain, so URLs and `run:unit` signals and every other card in the repo are unchanged byte for byte); this card's front matter is quoted by hand in the same commit as this note.

## Tests
`PYTHONPATH=backend python3 -m unittest tests.test_skills tests.test_board_tools` — `ProfileTests` and `VerifyDefaultTests` (default on claim, on update, explicit wins, two profiled skills, no verify keys, invalid profile, turn reset, the bundled `deliver` profile).
