---
id: 3W58
type: work
status: needs-qa-llm
labels: [bug]
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-18
rank: zzzz106
created: '2026-09-18'
acceptance: opening a .md file shows rendered Markdown
source: 'issues/feature_intake.txt, 2026-09-18'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-intake-fixes/'], related: [VXTF, S1JP], github: null}
---
# Markdown files are not rendered as Markdown

## Request
MD's arent printing markdown

## Which path was broken

Two paths render Markdown and both were checked in the running app before anything was changed:

| Path | Verdict |
|---|---|
| Markdown in agent output in the terminal (`src/MarkdownAnsi.cpp` through `Pane::printInline`) | **already fine** — headings, bullets, emphasis, inline code and quotes all styled (`3w58-already-fine-agent-markdown-in-the-terminal.png`) |
| A `.md` file opened in a preview pane from the explorer, or from a path in the output with no line number | **already fine** — rendered (`3w58-already-fine-md-preview-from-the-explorer.png`) |
| A `.md` file opened **at a line**: `relay open PATH:LINE`, or Ctrl+clicking `notes.md:9` in the output | **broken** — raw Markdown, and the view button still said "Source" (`3w58-before-md-line-link-shows-source-labelled-source.png`) |

`FilePreview::goToLine()` has to leave the rendered view to point at a line, because a line number
counts lines in the source. It switched the stacked widget by hand and never touched
`m_markdownSource`, so the pane showed the source while the button claimed the source was still one
click away. Clicking that button then flipped the flag without changing what was on screen, so the
file looked like Markdown Relay had failed to render, with no obvious way to the render.

## Fix

`goToLine()` now goes through the same state the view button uses: it sets `m_markdownSource`,
switches the view and refreshes the button, which therefore offers "Rendered (MD)". Reopening the
file renders it again, as it always did. `FilePreview::showingSource()` exposes the state so the
rule is testable (`tests/filepanes_test.cpp`,
`markdownOpensRenderedAndALineNumberSaysItLeftIt`).

The button was partly hidden under the pane chrome buttons; that is #VXTF, which names both views
and gives the preview header the same right-hand inset a terminal pane's header has.

## Implementer check (not a QA verdict)

`ctest -j16` green. Xvfb :190 with an isolated `XDG_CONFIG_HOME`:
`docs/qa_evidence/2026-09-18-intake-fixes/`.

## QA checklist

1. Open a `.md` file from the explorer: it is rendered (headings large, `**bold**` markers gone),
   and the view button reads "Source (MD)".
2. In the terminal run `grep -rn <word> <some>.md`, then Ctrl+click the `file.md:N` link: the
   preview opens at that line showing the **source**, with the line highlighted, and the view
   button reads "Rendered (MD)".
3. Click that button: the same file renders, and the button goes back to "Source (MD)".
4. Ctrl+click a `.md` path in the output **without** a line number: it opens rendered.
5. Ask the agent for a reply containing a heading, a bullet list, `**bold**`, `*italic*`,
   `` `code` `` and a `>` quote: the terminal shows them styled, with no `*` or `#` markers left.
6. `relay open <file>.md:3` in a Relay shell opens the source at line 3 with the button offering
   the render; `relay open <file>.md` opens the render.

Implementer evidence: docs/qa_evidence/2026-09-18-intake-fixes/
