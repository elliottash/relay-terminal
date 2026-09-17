# Skills dialog: list, exclude, refine, import, check updates

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Acceptance evidence**: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (GUI F1 worker), 2026-09-17
- **Source**: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
- **Workstream**: agent

## Behavior as implemented

- `/skills`, Actions › Skills… and Actions › Agent options › Skills… open a non-modal dialog (`src/SkillsDialog.*`) listing `skills_list` items (name, source, description; "refined", "overridden" for `shadowed_by`; skipped count with a tooltip). Unchecking excludes a skill (QSettings `skills/exclude`, used for new agent sessions; the existing "Excluded skills…" text option stays in sync).
- **Refine selected** sends `refine_skills`; the first refined SKILL.md opens in an editable pane; the list reloads with the result kept in the status line.
- **Import from repository…** asks for a URL and optional branch/tag/commit, sends `import_skills_preview`, shows a review with a checkbox per skill and its files, and sends `import_skills_confirm` with the checked names.
- **Check for updates** (enabled for imported skills) reads the URL from `.relay-import.json` and shows up to date / pinned vs latest. Worker errors for dialog requests show in the dialog.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-skills-ui/`: list of 52 skills (`implementer-01`); refining "documentation" wrote a copy under the isolated config and opened it; the original shows "overridden" (`implementer-02`); importing https://github.com/anthropics/skills showed 17+ skills for review (`implementer-03`) and imported them (`implementer-04`, 73 skills); Check for updates: up to date (`implementer-05`); unchecking xlsx wrote it to `skills/exclude` (`implementer-06`).

## QA checklist

1. /skills; uncheck a skill; start a new chat; ask the agent to use it: it is not available.
2. Refine a skill; edit and save the opened copy; the agent uses the refined copy.
3. Import from a repository with a ref; uncheck some skills in the review; only checked ones import.
4. Import from an invalid URL: a readable error in the dialog.
5. Check for updates on an imported skill after the repository has new commits.
