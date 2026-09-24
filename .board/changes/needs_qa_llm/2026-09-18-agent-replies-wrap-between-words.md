---
id: TW84
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Agent replies and Relay notes printed into the terminal break between words at the pane width, with list items hanging under their text; `relay-wordwrap-tests` passes; a QA session confirms it live at two pane widths'
source: 'owner in chat, 2026-09-18, with a screenshot of a reply broken "ses / sion": "relay is line wrapping mid word … can we fix that so it works more like warp terminal?"'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-agent-replies-wrap-between-words/'], related: [], github: null}
---
# Agent replies wrap between words, not mid-word

## What was wrong

Agent prose is rendered by `MarkdownAnsi` and written straight into the terminal grid
(`printInline`). Nothing knew the pane's width, so the terminal broke each paragraph wherever the
last column ran out — mid-word ("ses / sion").

## The change

- `src/WordWrap.{h,cpp}` (in `relay-markdown`): a streaming wrapper between the renderer and the
  terminal. It tracks the cursor column (escape sequences zero-width, wide characters two cells),
  holds only the word being streamed, and starts a new row before a word that would cross the
  edge. A continuation row takes the hanging indent of its line: past a list/quote marker, or the
  leading spaces. The indent is cursor-forward, not spaces, so copies and underlines skip it. A word
  wider than a row is left to the terminal to break.
- `src/main.cpp`: every ink in `printInline` goes through it at `m_backend->columns()`;
  `closeInline()` and the two hyperlink lines flush the held word first and restart at column 0.

## Limits

The breaks are real newlines, as in any CLI that wraps its own output: resizing the pane later
does not re-flow a reply already printed. Output from programs (the shell, Claude Code, …) is
untouched; they wrap their own text.

## QA checklist

- [ ] Ask the agent for a long paragraph and a bulleted list; no row ends mid-word, bullets hang.
- [ ] Repeat in a narrow split pane.
- [ ] Inline code, bold and links still render; Ctrl+click on "thought for" still works.
