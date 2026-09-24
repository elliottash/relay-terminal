---
id: B2XF
type: work
status: needs-qa-llm
labels: [change, bug]
component: [agent, worker]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Every indexed skill''s name reaches the system prompt even when the descriptions do not fit; skills under ~/.claude/skills/synced/<id>/<name>/SKILL.md are indexed; `tests/test_skills.py` passes'
source: 'owner, 2026-09-18: "relay didnt find my global warp skills by default", then "3 fix that" on the ~/.claude nesting'
links: {plans: [], commits: [cb010d8, 9fcc03f], evidence: ['docs/qa_evidence/2026-09-18-skills-every-name-in-the-prompt/'], related: [], github: null}
---
# Every skill reaches the prompt by name, and ~/.claude's nesting is indexed

## Report

The owner asked the agent to use one of his skills and was told it did not exist, so it read as
"Relay didn't find my global Warp skills". Relay had in fact indexed 45 of them — the worker logs
`skills=45` at every configure and the `/skills` dialog listed them — but two things hid skills
from the model:

1. The system prompt's list is capped at `MAX_PROMPT_BYTES` (6 KB). Twelve skills fell off the end
   as "(12 more skills not listed; ask the user for their names)", so a skill asked for by name
   was reported missing while the model went looking on disk.
2. Nothing under `~/.claude/skills` was indexed at all. Claude Code's synced skills sit at
   `skills/synced/<id>/<name>/SKILL.md`, so the folder named in the search order holds folders of
   skills rather than skills, and every one was skipped as "no SKILL.md".

## Change

- `SkillIndex.prompt_section()` reserves part of the budget (`MAX_NAMES_BYTES`, 1536) for a
  trailing line that names whatever the descriptions could not fit: "also loadable by name,
  descriptions omitted for length: …". A name costs a few bytes and is all `load_skill` needs, so
  a skill the user asks for by name is always loadable. Only when the names themselves overflow
  does the model fall back to asking.
- `default_directories()` runs the same walk over `~/.claude` that it already ran over `~/.warp`,
  so nested layouts are found wherever they sit (depth ≤ 6).

On this machine: 45 → 54 skills, 5.2 KB of the 6 KB budget, nothing unreachable. The nine that
were invisible are docs, docx, pdf, pptx, xlsx, skill-creator, morning, import-memory and
setup-writing-style.

## QA checklist

1. **Count.** Open `/skills`: the list holds the `~/.warp/skills` entries *and* the ones under
   `~/.claude/skills/synced/…`, each with its source. The worker log line for a new pane reports
   the same count.
2. **Overflow.** With more skills than fit, the prompt ends with the "also loadable by name" line
   and every indexed name appears in it. Ask the agent to load one of the named-only skills: it
   loads rather than reporting that it does not exist.
3. **Truly crowded.** With names long enough to overflow their own budget, the line ends
   "(and N more; ask the user for their names)" and the prompt still fits the cap.
4. **Excludes.** A skill unchecked in the dialog stays out of both lists.
5. **Tests.** `PYTHONPATH=backend python3 -m unittest tests.test_skills` (15) passes.

## Known gaps

- Folders in `~/.warp/skills` with no `SKILL.md` (on this machine `ethis-approvals` and
  `editor-support-dossier-workspace`) are still skipped, correctly, and say so in `skipped`.
- The dialog shows skipped entries only on hover; a skill the user expects and cannot find still
  takes some hunting.
