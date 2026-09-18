---
id: ANX9
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: keyboard
assignee: agent
implemented_by: Claude Opus 5 (GUI F1 worker), 2026-09-17
rank: n8
created: '2026-09-17'
acceptance: a non-Claude model QA session on a real desktop runs the checklist and records it under `docs/qa_evidence/`
source: owner decisions (intake batch 2) relayed by the coordinator; `docs/AGENT-SESSIONS-PROTOCOL.md` section 11
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Palette search aliases

## Behavior as implemented

- Palette items match hidden alias words at half weight (`paletteAliases()` in `src/main.cpp`), keyed by label/key/section substrings: model (llm provider switch), effort (reasoning think thinking budget), compaction (summarize context tokens), plans folder, automatic turns, instructions (claude.md agents.md warp.md rules), skills, copy on select, shortcut preset (keymap vscode konsole warp), program keys, suggestions, recap, shortcut hints, thinking, agents, rewind (undo checkpoint restore revert), fork, resume, new chat, stop, provider (byok api key), split, tabs, windows, move (detach drag), explorer, open file, native, interrupt, restore closed, keyboard shortcuts, routing.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-palette-aliases/`: "undo" lists Rewind chat… and Rewind code… first (`implementer-01`); "reasoning" finds effort items and Show thinking, "detach" finds the move actions (`implementer-02`).

## QA checklist

1. Search: undo, reasoning, byok, keymap, detach, summarize, rules: the expected items are in the top five.
2. Exact label searches still rank the labelled item first.
