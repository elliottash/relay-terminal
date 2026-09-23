---
id: RWND
type: work
status: needs-verification
labels: [bug, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-22'
source: User request in Relay, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-rewind/], related: [0TJ9], github: null}
---
# Collapse rewound terminal output into a saved-output link

## Issue
there is an issue with rewind that it doesnt move back and delete the rewound output from the terminal. 

i would replace it with a gray italic (with line breaks before and after) 

x lines rewound -- click to view

and the link takes you to a pane showing the rewound output, which we save i think.

## Done means
- Chat rewind removes the discarded output and preserves earlier terminal text.
- A gray italic notice, separated by blank lines, gives the removed line count and opens saved output in a pane.
- Code-only rewind preserves the conversation; failure to save output never silently discards it.

## Plan
**Goal:** replace discarded chat output with a link to its saved text.
**Findings:** `src/Pane.h` saves a rewind sidecar but does not remove terminal rows. The ordinary scrollback writer caps text at 5,000 lines; the ordinary ANSI serializer drops hyperlinks.
**Steps:** save the complete branch without the scrollback cap, snapshot retained rows with SGR and OSC 8 links using `engine/backend/VTermBackend.cpp` and `engine/core/AnsiSerializer.cpp`, rebuild the retained display, and print a file-pane link.
**Risks:** missing anchors and write failures preserve output; repeated rewinds must retain earlier links. Existing saved-session restart behavior still strips OSC 8 links.
**Verify:** real Pane regression for collapse, count, style, link activation, long branches, missing anchors and save failures; serializer tests; Xvfb screenshot and application build.

## Execution Summary
Chat rewind now saves the complete discarded branch as plain text and replaces it with a gray italic, blank-line-separated count/link. Clicking opens the existing file preview pane. Retained output keeps SGR and OSC 8 links, including earlier rewind notices. Code-only rewind keeps chat; missing anchors or save failures preserve the display. Live Xvfb screenshots and reproducible driver: docs/qa_evidence/2026-09-22-rewind/. Across restart, the existing SGR-only scrollback replay still does not restore hyperlink labels.

## Tests
- `ctest -R consolemode` — tests/consolemode_test.cpp
- `RELAY_ENGINE_TEST=CoreTest QT_QPA_PLATFORM=offscreen build/engine/relay-engine-tests ansiSerializerPreservesAttributesAndColours ansiSerializerHandlesWideCharactersAndClusters ansiSerializerRetainsLinksOnlyForLiveReplay`
- manual: docs/qa_evidence/2026-09-22-rewind/tests.log
- manual: docs/qa_evidence/2026-09-22-rewind/rewind.png
- manual: docs/qa_evidence/2026-09-22-rewind/viewer.png
