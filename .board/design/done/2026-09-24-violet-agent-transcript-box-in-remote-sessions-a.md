---
id: EE11
type: work
status: done
labels: [feature, ui, remote]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: visual, primary: person, also: [script], human: optional, criteria: 'The live boxed transcript has a violet outline and violet `Agent - [model]` header in a remote session or program.', sign_off: none, effort: low, stakes: rework, blast: case}
links: {plans: [], commits: [b0532fb5b2a9eb9e1c8c52d581414f2077742048], evidence: [docs/qa_evidence/2026-09-24-ee11-agent-box/], related: [], github: null}
---
# Violet agent transcript box in remote sessions and programs

## Issue
in remote sessions or programs, where the agent is in a box. give the box a violet outline and make the header "Agent - [model]" in violet

## Done means
- The boxed agent transcript visible during an SSH session or foreground program has a violet outline from the active theme.
- Its header reads `Agent - [model]` in violet, with the current model shown; extra explanatory text is removed.
- Other terminal and program controls retain their existing styling.

## Tests
- `scripts/relay-build --fast --target relay`
- `git diff --check -- src/Pane.h src/Theme.cpp`
- `manual: docs/qa_evidence/2026-09-24-ee11-agent-box/agent-box-program.png`

## Execution Summary
Changed the boxed agent transcript style to use the theme's violet `@agent` for its outline and header. The header now reads `Agent - [model]` with the active model.

![Violet agent transcript box during a foreground program](docs/qa_evidence/2026-09-24-ee11-agent-box/agent-box-crop.png)
