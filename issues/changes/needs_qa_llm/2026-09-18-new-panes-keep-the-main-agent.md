---
id: K9SR
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: c
created: '2026-09-18'
acceptance: 'A new pane starts on the Main agent; Settings › Agent › "New panes use the Flash agent" restores the old behaviour; Alt+F still switches one pane either way'
source: 'owner, 2026-09-18: "it also keeps changing from glm 5.3 to glm 5.3 flash"'
links: {plans: [], commits: [cb010d8], evidence: [], related: [], github: null}
---
# New panes keep the Main agent

## Report

Every pane after a window's first one started on the fast agent, so the model chip in a new pane
read `glm-5.3-flash` while the window said `glm-5.3`. The owner read it as the model changing
under him, which is what it looks like: nothing announces the switch, and the pane answers on a
smaller model than the one he chose.

## Change

`Pane::newPanesUseFlashAgent()` defaults to **false**. The setting stays — Settings › Agent › "New
panes use the Flash agent" turns the old behaviour back on, and its description now says which way
round it is — and Alt+F (`agent.flashAgent`) still switches a single pane either way without
losing the conversation. A pane that restored a saved role keeps it.

Note the role was renamed "fast" → "flash" later the same day (`#C6YX`,
`issues/changes/needs_qa_llm/2026-09-18-model-tier-commands-and-flash-naming.md`); this change is
about the default, not the name. The names below were updated to match: the method is
`newPanesUseFlashAgent()` and the setting is now `agent/panes_flash`, with `agent/panes_fast`
migrated to it once at startup.

## QA checklist

1. **Default.** With no `agent/panes_flash` key in settings, open a second and third pane: both
   report the Main agent and the main model in the chip and in the worker log line.
2. **Opt in.** Turn the setting on: panes after the first start on Flash again, as before.
3. **Per pane.** Alt+F still flips the focused pane and the chip follows; the conversation is kept.
4. **Restore.** A window layout saved with a pane on Flash still comes back on Flash.
5. **Existing users.** Someone who had explicitly set the toggle keeps their setting; only the
   unset default moved. This survives the later rename too: `migrateFastRoleSettings()` moves
   `agent/panes_fast` to `agent/panes_flash` on the first start, so an explicit `true` is still
   `true` afterwards (checked as part of `#C6YX` item 6).

## Known gaps

- Nothing announces a pane's role at creation; the chip is the only tell. If the default is ever
  turned back on, a first-run hint would be worth having.
