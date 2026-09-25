---
id: JVEJ
type: work
status: planned
labels: [feature, skills, gui]
component: [gui, skills]
rank: zzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'owner decision 3 on #9FX8 (2026-09-25), filed by the subagent that built Globals › Skills'
links: {plans: [], commits: [], evidence: [], related: [9FX8, 1QKM, SZ1H], github: null}
---
# Retire SkillsDialog into Globals › Skills

## Issue
#9FX8 gave Globals a Skills section, the global half of the skills registry, with SkillsDialog's
import-from-repository and Check-updates actions in its toolbar. The owner decided the dialog
retires into that section once it has those actions. This card closes the remaining parity gaps,
points every way into the dialog at the section, and deletes the dialog.
> SkillsDialog retires into Globals › Skills once the section has its import and update actions? … "yes"
> — owner, answer 3/4 on #9FX8, 2026-09-25

## Plan
**Parity first.** Before anything is removed, the section must do what the dialog does:
- Show `shadowed_by` ("overridden", greyed, with the shadowing path in the tooltip) and
  `refined_from` ("refined") on the row. The `skills_registry` rows already carry both fields;
  `skills::SkillRegistryView::refill` does not draw them yet.
- Show the index's `skipped` folders (count, with the reasons in a tooltip), as the dialog's
  status line does. `skills_registry` already returns `skipped`.
- Refine several skills at once. The dialog multi-selects; the view refines the one on its page.
  Either allow extended selection, or record that one at a time is enough (owner call, if it
  matters).
- The empty state ("No skills found. Import some from a repository, or add folders with
  SKILL.md to ~/.config/relay/skills.").

**Then move the entry points.** `/skills`, the palette's Skills action and Agent options call
`Pane::openSkills()` (`src/Pane.h`). Each should open the Sessions pane on its Globals tab with
the Skills section selected (`globals.open` then the section). A project skill named from a
pane's `/skills` goes to that project's Board › Skills instead. Add a shortcut hint for the slow
path (RELAY.md, standing rule).

**Then delete** `src/SkillsDialog.{h,cpp}`, `Pane::openSkills`'s dialog body and
`m_skillsDialog`, and the two CMake source-list entries. The import and update dialogs already
live in `src/SkillRegistryView.cpp` (`skills::askImport`, `reviewImport`, `updatesText`,
`importUrl`), so nothing is lost with them. Update `docs/ARCHITECTURE.md` § Skills, whose "GUI:
`src/SkillsDialog.*`" paragraph becomes the registry view's.

**Tests.** `tests/globalspane_test.cpp` gains the parity cases (overridden, refined, skipped,
empty state). A palette/slash test checks `/skills` lands on Globals › Skills.

## Done means
- Every SkillsDialog capability is in Globals › Skills (or the Board's Skills tab for project
  skills), and `/skills` and the palette open the section.
- `SkillsDialog` no longer exists; nothing in `src/` names it.
