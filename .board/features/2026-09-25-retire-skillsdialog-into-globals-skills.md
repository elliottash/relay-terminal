---
id: JVEJ
type: work
status: needs-verification
labels: [feature, skills, gui]
component: [gui, skills]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 436b9d1c-6289-445d-9f67-441056f34000
rank: zzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: visual, primary: script, also: [person], human: optional, criteria: Globals › Skills shows overridden/refined/skipped and the empty state; /skills and the palette land on it; no SkillsDialog in src/, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'owner decision 3 on #9FX8 (2026-09-25), filed by the subagent that built Globals › Skills'
links: {plans: [], commits: [6eb4f77731ac, c92cdc3f46a1], evidence: [], related: [9FX8, 1QKM, SZ1H], github: null}
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

## Execution Summary
Landed in `c92cdc3f46a1` (build gate green on the exact landed tree).

- **Parity** (`src/SkillRegistryView.*`): the Source column says "refined" (`refined_from`, source path in the tooltip) and "overridden" (`shadowed_by`, whole row greyed, "Not used: <path> has the same name"); Globals' count line adds "· N skipped" with the index's reasons in its tooltip; an empty scope shows the dialog's empty-state sentence (Board: "No project skills. Add folders with SKILL.md to .relay/skills in this workspace."). The list is extended-selection and **Refine takes every selected skill** (the open question on the card, settled by keeping the dialog's behaviour). A refined copy opens in an editable pane when the refine answers, and Open file uses the same opener (`openDocument`), as the dialog did.
- **Entry points**: `/skills [name]`, the palette's Skills… and Options' Skills row call `Pane::openSkills` → `onOpenSkills` → `RelayWindow::openSkills`: Globals › Skills (`GlobalsPane::showSkill`, which selects the named skill even before the registry arrives), or, for a name in the workspace's own `.relay`/`.agents`/`.claude`/`.codex`/`.warp` skills folders, the tab's Board on its Skills tab (`BoardView::showSkill`). The palette and Options paths show the hint "Next time: type /skills".
- **Deleted**: `src/SkillsDialog.{h,cpp}`, `m_skillsDialog`, the include in `main.cpp`/`Pane.h`, both CMake source-list entries. `docs/ARCHITECTURE.md` § Skills and the file table now describe the registry view; `docs/PROJECT-BOARD-DESIGN.md`'s skill row too.

## Tests
- `relay-globalspane-tests`: 16/16, new `skillsSectionHasTheDialogsParity` (empty state, refined/overridden/greyed, 2 skipped with tooltip, showSkill before the rows arrive, Refine over two selected, refined copy and Open file go to the pane opener).
- `relay-boardpane-tests`: 23/23.
- `relay-consolemode-tests`: new `skillsOpensTheRegistryNotADialog` passes (`/skills`, `/skills <project>` → project, `/skills <other>` → global, `/skill`, palette `openSkills()`, no QDialog). The suite's one failure, `ctrlClickEditsTheActualFile` (lines 769/781), follows main (#6BY7 thread).
- Run through `land.py try jvej --tests '^(consolemode|globalspane|boardpane)$'` on the tip plus this card's hunks only.

## Human QA
1. Type `/skills` in an agent pane: the Sessions pane opens on Globals › Skills (not a dialog), with refined / overridden rows marked and the skipped count on the top line when the index skipped folders. Does it read right?
2. From the palette (Skills…) or Options › Skills › Open…, the same section opens and a "Next time: type /skills" hint shows. OK?
