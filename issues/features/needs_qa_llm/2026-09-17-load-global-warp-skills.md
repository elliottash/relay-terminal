---
id: AK6B
type: work
status: needs-qa-llm
component: [agent]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code session, skills subagent), 2026-09-17
rank: lh
created: '2026-09-17'
acceptance: backend tests with a fixture skills directory, plus a live run where the agent loads `issue-tracking` for a triage request and follows it
source: '`issues/feature_intake.txt`, "access global .warp skills"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Let the agent use global Warp skills

## Context

The user keeps reusable instructions in `~/.warp/skills/<name>/SKILL.md`; 30 exist today.
Each file has YAML frontmatter with `name` and `description`, then Markdown instructions.
Relay's agent does not read them.

## Desired behavior

- At configure time, index skills from `~/.warp/skills` and optionally a project `.warp/skills`.
- Put only each skill's name and description in the system prompt, with a size cap.
- Add a read-only `load_skill(name)` tool that returns the full `SKILL.md` and lists
  supporting files in that skill's folder.
- Skill text is lower-priority instruction content, labelled with its path.
- A settings toggle and a skills directory setting, defaulting to `~/.warp/skills`.

Security: tools run without approval, so a skill's instructions can cause commands to run.
Only load skills from directories the user configured, never from arbitrary workspace paths.

## Acceptance criteria

1. Names and descriptions of all valid skills appear in the prompt; malformed ones are skipped and reported.
2. `load_skill` returns the full text and refuses names not in the index or paths outside the skill folder.
3. Prompt size stays under a documented cap with 30 skills.
4. Disabling the toggle removes skills from the prompt and the tool list.

## Resolution (2026-09-17): backend implemented

Behavior:

- `backend/relay_core/skills.py` indexes `<dir>/<name>/SKILL.md` (minimal YAML frontmatter: `key: value`,
  quoted values, `>`/`|` blocks), keyed by folder name. Up to 200 skills; skipped folders are reported.
- The system prompt gains an "Available skills" list (150 characters per description, 6 KiB total cap,
  with an omission note) and says skills are lower-priority guidance that must be loaded first.
- Tools: `load_skill(name)` returns SKILL.md (64 KiB cap) plus supporting file paths; `read_skill_file(name, path)`
  reads a text file inside that skill folder (64 KiB cap). Unknown names, absolute paths, `..`, symlinks
  anywhere on the path, and binary files are refused. Tools are offered only when at least one skill is indexed.
- Worker protocol: `configure` accepts optional `"skills": {"enabled": bool, "dirs": [absolute paths], "project": bool}`.
  Absent means enabled with `~/.warp/skills`. `project: true` adds `<workspace>/.warp/skills`. The `configured`
  event includes `"skills": <count>` and, when relevant, `"skills_skipped": [reasons]`.
- Not done here: the GUI toggle and directory setting (the GUI sends no `skills` field yet, so defaults apply).

Implementer check (not a QA verdict):

- Real `~/.warp/skills`: 30 skills indexed; skipped `editor-support-dossier-workspace` and `ethis-approvals`
  (no SKILL.md); prompt section 5,499 bytes with all 30 listed.
- `tests/test_skills.py`: 12 tests (block descriptions, skip reasons, prompt cap, tool refusals incl. symlink
  and `..` escapes, project directory opt-in, worker configure count).
- Live run with Kimi K3 in a scratch workspace, prompt "Which of my skills would you use to file an issue?
  Load it and tell me its status values.": one tool call, `load_skill("issue-tracking")`; the reply named
  issue-tracking and listed open, in-progress, needs-qa, needs-labels, needs-review, needs-ab, done.

QA checklist:

1. Ask the agent in Relay which skill fits a task; it loads that skill before answering.
2. A skill that references `scripts/...` can be read with `read_skill_file`; `../` and absolute paths are refused.
3. A skill folder symlinked outside `~/.warp/skills` is skipped and reported.
4. Removing all skills removes the list and the skill tools from the next configured agent.
5. The prompt stays under the cap with many long descriptions.
