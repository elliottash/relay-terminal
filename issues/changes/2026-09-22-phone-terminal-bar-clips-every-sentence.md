---
id: TBR2
type: work
status: executing
labels: [bug, remote]
assignee: claude-code
rank: ztbr2
created: '2026-09-22'
source: 'Measured by Claude Code driving the phone app at 390x844, 2026-09-22'
links: {plans: [], commits: [f919f14b], evidence: ['docs/qa_evidence/2026-09-22-phone-ux-drive/'], related: [PH0N], github: null}
---
# The phone terminal bar clips every sentence it says, and an `agent` device is told it may only watch

## Issue
Two faults in the terminal bar, both visible in
`docs/qa_evidence/2026-09-22-phone-ux-drive/C-full-after-send.png` (findings 7 and 8).

**1. The sentence is cut.** `#term-note` shares its row with the "Watching" chip, the A−/A+ buttons
`f919f14b` added and "Take over" (`app/index.html:183-190`). Measured at 390×844:

| capability | what it says | `scrollWidth` | `clientWidth` |
|---|---|---|---|
| `full`  | "Agent running…" → **"Agent runnin…"** | 178 | 91 |
| `agent` | "This device is paired for viewing only." → **"This device is paired for viewi…"** | 226 | 195 |

That label is also where `sendPrompt`'s `fail()` puts the *failure* of a send
(`app/app.js:1803`), so a refused or not-permitted prompt is reported into 91 px and the reader
never learns why their line did not go.

**2. An `agent` device is told it may only watch.** `updateDriveUi()` sets
`allowed = capability === 'full'` and then, for anything below it, writes "This device is paired
for viewing only." (`app/app.js:1113`). But `canCompose` is `agent || full` on the very next
lines, so an `agent` device has a working composer on screen — in this drive it typed a prompt and
the desktop ran it — under a sentence saying it may only watch. `agent` needs its own words: it
may ask the agent, it may not take the keyboard.

## Done means
The terminal bar's sentence is readable on a 390 px phone — it is not cut mid-word, which
means measuring `scrollWidth` against `clientWidth` on the note and finding them equal. A failed
send says why somewhere the reader can read the whole of. A device paired `agent` is told what it
can do — ask the agent — and never that it may only watch while its composer is on screen. It fails
if `#term-note` still overflows at either capability.
