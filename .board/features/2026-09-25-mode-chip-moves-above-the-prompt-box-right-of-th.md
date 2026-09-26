---
id: 6G12
type: work
status: needs-verification
labels: [feature, composer]
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
source: Claude guest pane, 2026-09-25
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Mode chip moves above the prompt box, right of the Relaying line

## Issue
Move the auto/agent/shell mode button out of the prompt box's top-right corner into the row above the box (the "Relaying · …" line), right-aligned, so the prompt text uses the box's full width. Landed in 0c51dc54: the corner layout (mode, `!`/`*` prefix and password chips) now sits at the right end of busyRow; consolemode_test checks the chip's parent is busyRow and not inside the composer. Offscreen grab of a terminal pane showed the chip right-aligned above a full-width prompt box.

> put the mode button (auto/agent/shell) right above the prompt box, in line with relaying... but at the right side. 
>
> then allow the full prompt box to include commands or prompts
> — elliott · [session:73aa766b91d547fa99045d88657fb156](relay://session/73aa766b91d547fa99045d88657fb156) · 2026-09-25
