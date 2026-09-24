---
id: ZW95
type: work
status: needs-qa-llm
labels: [bug]
assignee: agent
rank: zzzz105
created: '2026-09-18'
acceptance: completing a folder inserts the path and nothing else
source: 'issues/bug_intake.txt, 2026-09-18: "when i did tab autocompelte and selected a folder, it added a "-" for some reason: "cd 2026-09-18-EG/-" and then the command failed."'
links: {plans: [], commits: [828223c], evidence: [], related: [], github: null}
---
# Tab completion adds a stray "-" after a folder

## Request
when i did tab autocompelte and selected a folder, it added a "-" for some reason: "cd 2026-09-18-EG/-" and then the command failed.

## Resolution (found already fixed by the 2026-09-19 board sweep)

`828223c`, "Tab completion replaces the word that is there now". The popup replaced the range the
completion had been computed on, but Tab had since filled in the candidates' common prefix, so the
difference was left behind: `cd 2026-09-18-EG/G`, and where the folder names diverge at a dash, the
`cd 2026-09-18-EG/-` of the report. `Pane::acceptTabSelection()` now recomputes the token under the
cursor *at the moment of the click* (`relay::completeAt` on the live line) and replaces that. The
commit names the owner's exact string in its comment.

Nothing was needed here; the card had simply never been moved out of the inbox.

## QA checklist
- [ ] Type `cd 2026-09-` in a folder holding several `2026-09-18-*` directories, press Tab, pick one from the popup: the line holds that path and nothing else.
- [ ] The same with folders that diverge at a dash (the owner's case), and with the keyboard (Enter) as well as the mouse.
- [ ] Typing more characters after Tab opened the popup, then accepting, still replaces only the word under the cursor.
