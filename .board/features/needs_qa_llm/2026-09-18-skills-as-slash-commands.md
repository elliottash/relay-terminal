---
id: JKW4
type: work
status: needs-qa-llm
labels: [feature]
component: [agent, worker, gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: '`/clean-commit <input>` in the prompt box runs the agent with that SKILL.md attached as its instructions; the popup lists skills; `tests/test_skills.py` passes; live run in docs/qa_evidence/2026-09-18-skills-as-slash-commands/'
source: '`issues/feature_intake.txt`'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-skills-as-slash-commands/'], related: [AK6B, B2XF], github: null}
---
# Skills run as `/name` in the prompt box

## Issue

add skills as / commands like warp, eg /clean-commit

(An earlier wording in the intake: "add skills as /skill clean-commit".)

## Change

- **Worker.** `configured` now carries `skill_commands: [{name, description}]`, the skills this
  pane's agent can load. `ask` accepts `skills: [name]` (at most 5). The worker attaches that
  skill's SKILL.md, plus a list of its supporting files, as a `kind: "skill"` block
  (`SkillIndex.invoked`). `attachments.format_block` frames the block as this request's
  instructions, with the text after `/name` as its input. An `@file` is still framed as "data,
  not instructions". An unknown name is an `error`.
- **Prompt box.** Typing `/` lists the skills after the built-ins and the aliases, as
  `/clean-commit [input]  Skill · <description>`. On Enter, `/clean-commit tidy the readme` goes to
  the agent as typed, with `skills: ["clean-commit"]`. This works from the queue, when steering and
  from a paired device's prompt too. When a built-in or an alias has the same name, it takes
  `/name`, and the skill is still reachable as `/skill clean-commit …`. `/skill` on its own opens
  the Skills dialog. `/skill <unknown>` names the closest skill. The route line reads `SKILL`. A
  skill name no longer gets the "Unknown command" answer.
- **The list stays current.** It is rebuilt from every `configured` event and from every `skills`
  listing the dialog fetches, so refines, imports and exclusions show up.
- **Hint.** A prompt that mentions a skill by name and says "skill" ("use the clean-commit skill
  on this") ends with the hint "Next time: /clean-commit runs that skill". It uses the usual hint
  gates, and fires at the end of the turn because the turn's own status holds the toast while it
  runs. It is listed in `docs/ARCHITECTURE.md`, "Shortcut hints".
- Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` section 11, "Skills as `/name`".

## Implementer check (not a QA verdict)

- `PYTHONPATH=backend python3 -m unittest tests.test_skills`: 20 tests pass, 5 of them new. They
  cover the command list, the attachment and its file list, refusals, the framing reaching the
  model message, and the worker's `configured` and unknown-skill `error`.
- A build of HEAD plus this change in a scratch copy: `ctest` passes apart from
  `backend-and-bash`, where three tests fail. The same three fail on plain HEAD:
  `test_tools…test_absolute_and_parent_paths_allowed_inside_workspace` and two
  `test_remote_gui_host.VoiceTests`. The shared `build/` could not build at the time because other
  sessions had edits in progress there.
- Live run under Xvfb with an isolated profile. The model was a fake local endpoint that logs each
  request (`drive.sh`, `fake-provider.py`, `requests.jsonl`). `/clean-commit tidy the readme` and
  `/skill clean-commit again` both reached the model with the SKILL.md marker. The prose request
  did not, and it showed the hint. Screenshots a–f are in the evidence folder. Screenshot b does
  not show the `SKILL` route word, because the strip only fills in once routing preview has run.

## QA checklist

1. Type `/` in the prompt box. Your skills (`~/.warp/skills`, `~/.claude/skills/…`) are listed
   after the commands, each marked "Skill ·". Typing more letters filters them.
2. `/clean-commit` with Enter, and again with some input after it. The agent follows that skill
   without first calling `load_skill`. Check the worker log or the turn pane: the user message
   contains the SKILL.md inside a "[Skill clean-commit, invoked by the user…]" block.
3. Exclude a skill in `/skills`. It leaves the `/` list, and `/name` then gets "Unknown command".
4. A skill with the same name as a built-in (make a `plan` skill): `/plan` still toggles plan
   mode, and `/skill plan` runs the skill.
5. `/skill nosuch` gives a status line, and nothing reaches the agent.
6. `use the clean-commit skill on this` as prose: at the end of the turn, the hint
   `Next time: /clean-commit runs that skill` shows (subject to the 20 s gap between hints).
7. Queue `/clean-commit` behind a running turn, and steer with it: the skill travels in both cases.
8. `PYTHONPATH=backend python3 -m unittest tests.test_skills` passes.

## Notes

- Before a provider is configured, the pane has no skill list, so `/clean-commit` is answered as
  an unknown command. Without a provider no agent can run the skill anyway. Configuring a
  provider fixes both.
- The intake line in `issues/feature_intake.txt` is left for the owner to clear. That file is the
  owner's inbox.
