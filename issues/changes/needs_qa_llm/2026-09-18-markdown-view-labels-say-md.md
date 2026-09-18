---
id: VXTF
type: work
status: needs-qa-llm
labels: [bug]
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-18
rank: zzzz103
created: '2026-09-18'
acceptance: the two view buttons on a Markdown preview name the format
source: 'issues/bug_intake.txt, 2026-09-18'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-intake-fixes/'], related: [3W58], github: null}
---
# A Markdown preview should label its views "source (MD)" and "rendered (MD)"

## Request
in markdown, it should probably say "source (MD)" rather than "source" (ditto for rendered)

## Behavior as implemented

- The preview's view button reads **"Source (MD)"** while the render is on screen and
  **"Rendered (MD)"** while the source is. The tooltips are unchanged. An image preview's button
  (Fit / 100%) is untouched, and every other file kind still hides the button.
- The label is wide enough to reach the floating pane buttons, and it did: "Source" was already
  half under them, so the named label would have been unreadable and the button unclickable. The
  preview and explorer headers now take the same right-hand inset a terminal pane's header takes
  (`FilePreview::setHeaderRightInset`, `FileExplorer::setHeaderRightInset`, wired from
  `PaneChrome::syncHeaderInset`), so the title elides instead of the buttons disappearing.

## Implementer check (not a QA verdict)

`tests/filepanes_test.cpp` (`markdownViewButtonNamesTheFormat`) and `ctest -j16` green. Xvfb :190
with an isolated `XDG_CONFIG_HOME`: `docs/qa_evidence/2026-09-18-intake-fixes/`
(`vxtf-before-…`, `vxtf-after-source-md-…`, `vxtf-after-rendered-md.png`).

## QA checklist

1. Open a `.md` file in a preview pane: the button reads "Source (MD)" and is fully visible, left
   of the ⟳ and ↗ buttons and clear of the pane's ⬓+ ◫+ ⇱ × row.
2. Click it: the source appears and the button reads "Rendered (MD)". Click again: back to the
   render, "Source (MD)".
3. Open a `.png`: the button reads "100%" / "Fit", not "(MD)". Open a `.txt`: no view button.
4. Narrow the preview pane until the file name elides: the name shortens, the buttons stay put and
   stay clickable.
5. In an explorer pane, the folder line in the header elides before it reaches the pane buttons,
   and the `.*` hidden-files button is still clickable.

Implementer evidence: docs/qa_evidence/2026-09-18-intake-fixes/
