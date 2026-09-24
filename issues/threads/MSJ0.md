<!-- relay:entry 20260924T025137Z-0f author=agent kind=event model=claude-fable-5-1 pane=9b541d35 turn=cf99c92a9b1e48559ab6b383c65134c7/a328f0aa299c4326bff4c99a3b066dc2 -->
- ✦ agent created this card in Planned · issues/features/2026-09-23-skills-declare-a-task-profile-in-skill-md-frontm.md

<!-- relay:entry 20260924T025244Z-sz author=agent kind=event model=claude-fable-5-1 pane=9b541d35 turn=cf99c92a9b1e48559ab6b383c65134c7/a328f0aa299c4326bff4c99a3b066dc2 -->
- ✦ agent updated this card · parent: (unset) → 1QKM; component: (unset) → ["worker", "gui"]; replaced `## Done means`

<!-- relay:entry 20260924T025700Z-0a author=agent kind=progress model=claude-fable-5-1 pane=9b541d35 -->
### agent (subagent of pane 9b541d35) · 2026-09-24 02:57
claimed this card; starting on backend/relay_core/skills.py (parse_profile, Skill.profile), skill_manage.list_skills, the deliver profile, five example profiles under docs/skills-examples/, SkillsDialog's verify line, tests/test_skills.py and docs/ARCHITECTURE.md. The board_claim / board_update_card verify-default hook is left to a follow-up after the board_tools.py owner lands.

<!-- relay:entry 20260924T033200Z-0a author=agent kind=progress model=claude-fable-5-1 pane=9b541d35 -->
### agent (subagent of pane 9b541d35) · 2026-09-24 03:32
owner steer applied: no SkillsDialog line after all ("ideally, most of this is just in the agent's work and the user doesn't see it directly") — the src/SkillsDialog.* and consolemode-test changes are reverted; the profile is agent-facing data in skills_list / load_skill and the docs/skills-examples templates. Re-landing the backend + examples + tests + docs.
