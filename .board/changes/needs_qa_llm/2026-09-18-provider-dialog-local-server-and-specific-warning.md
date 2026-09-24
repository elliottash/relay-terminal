---
id: M109
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code), 2026-09-18
rank: zzzz13
created: '2026-09-18'
acceptance: with http://127.0.0.1:8080/v1 in the provider dialog, Save works with no consent box; with a hosted URL and the box unticked, the warning names the box
source: 'owner in chat, 2026-09-18, with a screenshot of the dialog holding http://127.0.0.1:8080/v1 and the warning "Choose an existing workspace, enter valid JSON, and confirm provider data sharing.": "what do i do here?" and then "fix the issue with the checkbox / dialogue box"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-provider-dialog-local/], related: [24XJ], github: null}
---
# The provider dialog: a local server needs no consent box, and the warning says what is wrong

## Issue
what do i do here? … fix the issue with the checkbox / dialogue box

## What was wrong
The owner typed Bonsai's address into Settings › Models › Advanced provider settings and pressed
Save. The dialog answered with one sentence listing three conditions, two of which were already
met. The third was an unticked box, "Send my submitted agent prompts and tool results to this
provider", which makes no sense for a server on the same machine: nothing is sent anywhere.

## What changed (`Pane::configure()`, `src/Pane.h`)
- Plain HTTP to `localhost`, `127.0.0.1` or `::1` (the rule of `relay_core.provider.loopback_http`)
  hides the consent box and shows "This is a model server on this machine: prompts and tool results
  stay here, and no API key is needed." The key field is disabled with a matching placeholder, and
  no key is sent. It follows the Base URL as it is typed.
- The warning is one sentence about the condition that failed, and the keyboard lands on that
  field: the JSON, the workspace folder, or the box.
- A hosted URL still requires the box.

## For QA
- [ ] `http://127.0.0.1:8080/v1` and a model id: no box, Save configures the pane, a prompt is answered
- [ ] A hosted https URL with the box unticked: the warning names the box, and focus moves to it
- [ ] Invalid JSON, and a workspace that does not exist, each get their own sentence
- [ ] Changing the Base URL from hosted to loopback and back shows and hides the box as typed
