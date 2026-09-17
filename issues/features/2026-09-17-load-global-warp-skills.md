# Let the agent use global Warp skills

- **Status**: open
- **Component**: agent
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: backend tests with a fixture skills directory, plus a live run where
  the agent loads `issue-tracking` for a triage request and follows it
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "access global .warp skills"

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
