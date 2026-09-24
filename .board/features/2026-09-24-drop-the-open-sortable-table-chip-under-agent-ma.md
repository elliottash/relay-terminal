---
id: 15G5
type: work
status: needs-verification
labels: [feature, terminal, agent-ui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 31017c4c-cec4-46d5-b2b1-af761877de17
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [person], human: none, criteria: 'An agent reply containing a Markdown table shows only the drawn table, with no chip row under it', sign_off: none, effort: low}
source: Relay pane, 2026-09-24 (screenshot relay-paste-20260924-141826.png)
links: {plans: [], commits: [], evidence: [tests/markdownansi_test.cpp], related: [MDA7], github: null}
---
# Drop the "open sortable table" chip under agent Markdown tables

## Issue
check out this button for a markdown table 

i actually dont like the button would want to remove that. where else do those buttons show up, because i want to review that. 

i would like nice MD tables to be rendered directly in the terminal if tahts possible. but the separate button isnt helpful.

## Execution Summary
Landed in 93ca678. `MarkdownAnsi::renderTable` no longer writes a CSV + `kind: table` manifest into `~/.cache/relay/media` or appends the `relay-media:` chip row ("▦ Markdown table · N rows × M columns · open sortable table"). The box-drawn table is unchanged. `relay show file.csv` still produces a sortable-table row; the view's `table` painting/click path is untouched for that.

Tests: `ctest --test-dir build -R '^markdown$'` passes (33/33); `inlineTableKeepsTextAndAddsSortableRow` became `inlineTableIsOnlyTheDrawnTable` (with inline media on, output equals the plain render and has no media escape). land.py built `relay` from the exact committed tree. The shared `build/relay` currently fails on another session's uncommitted `src/SharingPane.cpp:685` (`onPairPhone`), so the local binary was not refreshed.

Other inline chips still in place, for the owner's review: automatic in agent replies — local audio links (play/waveform row) and LaTeX `$…$` / `$$…$$` (rendered image with a "Math" label bar); from `relay show` only — chart, video, PDF, SVG (each with an "open …" label bar) and CSV tables.
