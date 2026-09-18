---
id: S1JP
type: work
status: needs-qa-llm
labels: [bug]
assignee: agent
implemented_by: Claude Opus 5 (1M context) (Claude Code session), 2026-09-18
rank: zzzz104
created: '2026-09-18'
acceptance: following a link from a preview leaves the original reachable
source: 'issues/bug_intake.txt, 2026-09-18'
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-intake-fixes/'], related: [3W58], github: null}
---
# A link clicked inside a file preview replaces the file with no way back

## Request
for a file preview pane, if you click on another link there, it should open a new pane, or if not , there needs to be a back button.

## What it did

The rendered Markdown lived in a `QTextBrowser` with `setOpenExternalLinks(true)` and the default
`openLinks`, so the browser followed a relative link itself and loaded the target into the same
widget. `FilePreview::open()` was never called, so nothing else in the pane moved: the header still
named the file that had carried the link, Reload and ↗ still acted on it, and the only way back was
to find the first file again in the explorer.

## Behavior as implemented

The owner's first preference: the link opens a new pane.

- The browser no longer navigates at all (`setOpenLinks(false)`, `setOpenExternalLinks(false)`).
  `FilePreview::followLink()` decides instead, and the callback the host sets is `onOpenLink`.
- A **local file or folder** goes to the host, which opens it in a pane of its own beside the
  preview — the same `openPath()` a path clicked in terminal output goes through, with a `newPane`
  flag so it does not take over the preview that is already open. A folder gets an explorer pane.
- Clicking **back and forth** does not pile up panes: with `newPane`, `openPath()` reuses a pane
  that is already showing exactly that file (never the one the link came from) and focuses it.
- A **`#section`** link scrolls the document it is in, as it did.
- An **http / mailto** link goes to the desktop, as it did.
- A link that **resolves nowhere** says so in the pane's notice line instead of blanking the view.

## Implementer check (not a QA verdict)

`tests/filepanes_test.cpp` (`aLinkInsideAPreviewIsHandedToTheHost`) and `ctest -j16` green.
Xvfb :190 with an isolated `XDG_CONFIG_HOME`: `docs/qa_evidence/2026-09-18-intake-fixes/`
(`s1jp-before-…`, `s1jp-after-…`).

## QA checklist

1. In a folder with two Markdown files that link to each other, open the first in a preview and
   click the link: the second opens in a **new pane beside it**, takes focus, and the first pane
   still shows the first file, still rendered, with its own name in its header.
2. Click the link back: focus moves to the pane already showing the first file. No fifth pane, no
   duplicate.
3. Click a link to a `.txt` file: it opens in its own pane with the text viewer (monospace), not
   inside the Markdown view.
4. Click a link to a folder: an explorer pane opens on that folder.
5. Click an `http://` link: it goes to the browser and no pane opens.
6. Click a `[x](#a-list)` style link: the same document scrolls to that heading.
7. Click a link to a file that does not exist: the pane says so above the document and keeps the
   file it had.
8. Close the linked pane: the original preview is untouched.

Implementer evidence: docs/qa_evidence/2026-09-18-intake-fixes/
