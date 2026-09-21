---
id: QRC1
type: work
status: needs-verification
labels: [feature, queue]
assignee: codex
rank: mqrc1
created: '2026-09-21'
source: 'Owner in Relay, 2026-09-21'
links: {plans: [], commits: [3ebf3673bcfa68cb92cbf1a492ab32dc28bd0adb], evidence: [docs/qa_evidence/2026-09-21-queue-recall/], related: [9V1F], github: null}
---
# Up recalls a queued command as an unsent draft

## Issue
if you have a command queued, i think pressing up should de-queue it and bring it back into the prompt, rather than keeping it there and saying queue paused. what do you think about taht?

## Decisions
"ok, you agree then. make that change"

## Plan
Goal: Up from an empty composer takes the first pending item back for editing, without holding the queue.
Findings: `Pane::handleComposerKey` currently selects the head; `queueHeldBySelection` then holds execution. `QueueNav` already protects an occupied composer.
Steps: remove recalled local entries before restoring their draft, preserve submission routing, reuse withdrawal for steers and known worker prompts, and update the shortcut hint. Build and exercise the real composer under isolated Xvfb.
Risks: worker rows with only truncated previews cannot safely become drafts; retain their existing selection behavior. Relay-authored entries remain noneditable.
Verify: targeted queue tests and isolated GUI recall/resubmit checks, including two queued commands and a busy terminal.


## Execution Summary
Up removes the first editable queued item and restores its text as a normal draft. Local shell/agent routing is preserved, the selection hold is cleared, and remaining items keep running in order. Updated the queue hint and tooltip. Evidence: `docs/qa_evidence/2026-09-21-queue-recall/notes.md`.

## Tests
`ctest -R queuenav`
`ctest -R queuesubmit`
manual: docs/qa_evidence/2026-09-21-queue-recall/notes.md

## QA checklist
- [ ] Queue two commands behind a running command. Up recalls the first and shows no selection-induced pause; the second runs normally.
- [ ] Edit the recalled draft and press Enter; it submits exactly once with its original shell/agent destination.
- [ ] Recall the sole queued item; the queue row disappears and the draft remains after the active command finishes.
- [ ] An occupied composer keeps ordinary cursor/history behavior on Up.
- [ ] Check agent prompt and pending-steer recall with a live provider; noneditable worker previews still offer move/remove controls.
