---
id: ZYRB
type: work
status: needs-qa-llm
labels: [bug, gui, agent]
implemented_by: Claude Opus 5 (Relay agent), 2026-09-18
rank: zzzzzt
created: '2026-09-18'
source: pane 1, 2026-09-18
links: {plans: [], commits: [450567e], evidence: [], related: [MH58], github: null}
---
# Onboarding: instructions dialog still shows when no instruction files exist — silently init default relay.md instead

## Issue
the introductory agent instructions file was still showing when i didnt have any. it shouldnt show any in that case. just init the default RELAY.MD

## Resolution (found already implemented by the 2026-09-19 board sweep)

`450567e`. `Pane::initDefaultRelayMd()` writes the starter `relay.md` and points the agent at it
when a first launch finds no instruction files at all, instead of opening the chooser on an empty
list. The comment above it quotes the owner's report verbatim. The quiet path is only first-run:
`/instructions` and Options › Agent › Instructions still open the dialog on an empty list, because
that is where a file is created on purpose.

The card already carried `implemented_by` and had simply never left `in-progress`.

## QA checklist
- [ ] First launch with an isolated `XDG_CONFIG_HOME` and no instruction file anywhere: no dialog, a starter `relay.md` exists, and one line says so.
- [ ] The starter file's text is the short one the code writes, and the agent reads it.
- [ ] `/instructions` on that same profile still opens the dialog.
- [ ] A profile that already has an instruction file is unchanged (no overwrite).
