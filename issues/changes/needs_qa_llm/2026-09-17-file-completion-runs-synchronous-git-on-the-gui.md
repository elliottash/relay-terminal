---
id: GDWE
type: work
status: needs-qa-llm
labels: [change, bug]
component: [gui]
implemented_by: Claude Opus 5 subagent, reviewed and amended by Claude Fable 5.1 (Claude Code, milestone review), 2026-09-18
acceptance: 'Typing `@` never blocks the window, however slow git is; the list fills in when the listing arrives; a slow or failed listing never blanks a picker that already had this folder''s files; `ctest -R fileindex` (8 cases) passes'
rank: zzw
created: '2026-09-17'
source: Relay pane, cleanup audit 2026-09-17
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-at-picker-asynchronous/'], related: [AM2Z], github: null}
---
# @ file completion runs synchronous git on the GUI thread (multi-second block)

## Request
look for cleanup opportunities

## Findings
From the 2026-09-17 cleanup audit (verified by reading the code):

`Pane::refreshFileIndex` (src/main.cpp:6228) runs up to three synchronous `git` QProcesses with `waitForFinished(1000/2500/1500)` — worst case ~5 s blocked GUI. It is triggered from `updateAtPopup()` (src/main.cpp:6262) while typing `@` in the composer, on a 15 s cache. All waits have timeouts, so this is a responsiveness bug, not a hang.

**Suggested fix:** run the git calls asynchronously (or on a worker thread, once per cwd) and populate the popup when results arrive; the 15 s cache already defines the freshness contract.

## Change (2026-09-18)

`src/FileIndex.{h,cpp}`: `relay::FileIndex`, a small `QObject` the pane owns. `refresh(cwd)` returns
at once; `rev-parse --show-toplevel` → `ls-files --cached --others --exclude-standard -z` →
`status --porcelain -z` run as a chain of `QProcess`es on the event loop, each with a timer that
kills it. `Pane::refreshFileIndex()` is now one call, and `updateAtPopup()` is re-run when results
land, so the list fills in under the caret; the very first index shows a muted "Indexing files…" row.

- **Superseded work is dropped.** A generation counter: a `cd` while git is running kills that git,
  and a late answer for the old folder cannot overwrite the new one's.
- **Stale while revalidating.** Re-indexing the same folder keeps the old list on screen until the
  new one arrives. Another folder's files are *wrong*, not stale, so a `cd` clears them.
- **The timeouts went up, not down.** The old 1 s / 2.5 s / 1.5 s existed only to bound the freeze.
  Kept as they were, they would have left the picker *empty for good* in exactly the repositories
  this card is about (an `ls-files --others` that needs 4 s was killed at 2.5 s, every time). They
  are now 5 s / 60 s / 60 s — a stop for a git stuck on a dead mount, nothing more — and a step that
  fails or times out keeps what the picker already had instead of publishing an empty list.
- **The non-git walk** no longer blocks either: 400 entries per event-loop slice, 5,000 files, and
  a hard stop at 100,000 entries for a tree that is all hidden files.
- **A bug fixed on the way:** `status --porcelain -z` follows a rename or copy with the *original*
  path in a field of its own, without the `XY ` prefix. The old loop ran `mid(3)` over every field,
  so each rename put a second, truncated path in the changed set. That field is now skipped.

Files: `src/FileIndex.{h,cpp}`, `tests/fileindex_test.cpp` (8 cases, incl. a `git` that sleeps 2 s
— `refresh()` returns in under 100 ms — and a `git` that fails — the list survives),
`CMakeLists.txt`, `src/main.cpp` (`refreshFileIndex`, `updateAtPopup`, four members became one),
`docs/ARCHITECTURE.md` (section 5, source map). No protocol change.

## QA checklist

1. **It works.** In a git repository type `look at @Rea`: README and friends are listed, changed
   files marked as before; Enter/Tab inserts the path (`implementer-a-picker-live.png`).
2. **Untracked yes, ignored no.** A new untracked file is offered; a file matched by `.gitignore`
   (anything under `build/`) is not.
3. **It never blocks.** Put a slow git first on `PATH` (`printf '#!/bin/sh\nsleep 5\nexec /usr/bin/git "$@"\n'`),
   start Relay from that shell, type `@`: the caret keeps blinking and typing stays instant;
   "Indexing files…" shows, then the list replaces it without another keystroke.
4. **cd mid-flight.** With the slow git, type `@`, then `cd` elsewhere and type `@` again: only the
   new folder's files ever appear.
5. **Not a repository.** In `/usr/share/doc` (large, no git) `@` lists files without a stutter.
6. **Renames.** `git mv a.txt b.txt`, then `@`: `b.txt` is marked changed; nothing odd named `txt`
   or a path with its first three letters missing appears.
7. **Closing a pane mid-index** (slow git, type `@`, close the pane at once): no crash, no
   "QProcess: Destroyed while process is still running" on stderr, no `git` left running.
