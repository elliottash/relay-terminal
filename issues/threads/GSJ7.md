<!-- relay:entry 20260920T230044Z-nm author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=c938d100 turn=cf83c12eabc74498a84f09ebafd799f8/f5251f711fe64ff380f4c8425e20b0cc -->
- ✦ agent created this card in Inbox · issues/features/2026-09-20-teach-the-equalize-panes-key-alt-0-when-a-divide.md

<!-- relay:entry 20260920T230052Z-3x author=agent kind=event model=deepseek/deepseek-v4.1-flash pane=c938d100 turn=cf83c12eabc74498a84f09ebafd799f8/f5251f711fe64ff380f4c8425e20b0cc -->
- ✦ agent claimed this card · assignee agent, Inbox → Executing, implemented_by deepseek/deepseek-v4.1-flash, session c938d100

<!-- relay:entry 20260920T230052Z-nv author=agent kind=progress model=deepseek/deepseek-v4.1-flash pane=c938d100 turn=cf83c12eabc74498a84f09ebafd799f8/f5251f711fe64ff380f4c8425e20b0cc pane_token=c938d100-5bad-4eb8-9b60-0e53c81be565 -->
Claimed (c938d100) · working on it from a terminal pane

Add a shortcut hint on divider drag (pane.equalize), rebind pane.equalize Ctrl+Alt+0 → Alt+0 (no conflicts), and make sure the "?" list shows it.

<!-- relay:entry 20260920T231315Z-zr author=owner kind=event pane=c938d100 -->
Released (c938d100) · the pane closed

<!-- relay:entry 20260921T195119Z-g1 author=codex kind=progress -->
### Codex · 2026-09-21 19:51
Owner authorized recovery: “can you duoble check those again, and if they arent here merge them int o the active build, delete other branches and worktrees”. Claimed this follow-up, confirmed the implementation is still outside main, and reviewed the hunks for landing and rebuilding. No extra branches/worktrees exist.

<!-- relay:entry 20260921T195847Z-rv author=codex kind=evidence -->
### Codex · 2026-09-21 19:58
Recovered the owner-authorized inactive changes. Targeted panes/hints/settings tests and engine zoom checks passed; isolated GUI verified the drag hint, Alt+0 equalization, searchable Actions entry, and saved font size changing 18→22 pt in open terminals. Evidence: docs/qa_evidence/2026-09-21-recover-inactive/README.md. Moving to needs-verification with a QA checklist; landing uses the exact-tree build gate. No extra branches or worktrees exist to delete.
