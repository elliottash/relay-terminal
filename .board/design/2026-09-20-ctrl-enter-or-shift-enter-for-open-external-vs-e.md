---
id: SEJ2
type: work
status: needs-verification
labels: [feature, design]
assignee: agent
implemented_by: glm/glm-5.3
session: cb542d99-5a96-41b3-a14c-3cf5f3698431
waiting_on: owner
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: pane (switchboard session ae279c67), 2026-09-19
links: {related: [V9V1], commits: [5c3c6c94, c7764626, b3a11c20], evidence: [docs/qa_evidence/2026-09-20-edit-here/]}
---
# Ctrl+Enter or Shift+Enter for open-external vs edit-here in file panes

## Issue
is there a card on whether ctrl enter or shift enter should open-external or edit here … yeah lets start a card. whats more intuitive for users? research what other programs do.

## Discussion points
The file explorer (`src/FilePanes.cpp`) binds **plain Enter** to `activate()` — open internal (a preview pane). Neither Ctrl+Enter nor Shift+Enter is bound there today, so both chords are free. #V9V1 settled the right-click menu order (Open internal / Open external / Open folder) but said nothing about keys.

**What other programs do with a modified Enter when plain Enter opens:**

| Program | Enter | Ctrl+Enter | Shift+Enter |
|---|---|---|---|
| VS Code (Quick Open / Explorer) | open in editor | **open to the side** (variant inside the app) | — |
| Firefox (address bar, history, bookmarks) | open in current tab | open in **new tab** (Alt+Enter in the address bar) | open in **new window** |
| Windows Explorer | open default app | open in **new window** | — |
| Browsers, on a link (click analogues) | follow here | Ctrl+click: new tab, stay put | Shift+click: new window |
| Chat composers (Slack, Discord…) | send | (send, in some) | **newline — the soft in-place variant** |

The pattern that repeats: **Ctrl keeps you in the same app and does the parallel variant** (new tab, open to side); **Shift is the bigger departure** — a new window, i.e. leaving the current context. "Open external" in Relay is exactly the bigger departure: the file leaves Relay and goes to the desktop's default application.

**What Relay itself already teaches:**

- Conversation list: **Enter resumes here, Shift+Enter opens it in a new pane** (`Conversations.cpp:893`) — Shift+Enter already means "not here, somewhere else, leaving this one as it was".
- Composer: **Ctrl+Enter = send now / act immediately** (`ContinueTurn.h`), Ctrl+Shift+Enter = run in terminal. Ctrl+Enter is Relay's universal "do the strong action right here, right now" chord.
- Board pane: Ctrl+Enter saves/plans — again "commit this, in place".

**Putting it together (recommendation):**

- **Enter** — open internal (preview), unchanged.
- **Ctrl+Enter** — the other *internal* action, "edit here" (open as editable text rather than a rendered preview): you stay in Relay, and it matches both VS Code's Ctrl+Enter = in-app variant and Relay's own Ctrl+Enter = act here now.
- **Shift+Enter** — open external: the file leaves the app, matching Shift = "elsewhere" in Firefox (new window), browser Shift+click, and Relay's own Shift+Enter = open in a new pane.

This also avoids a subtle conflict: Ctrl+Enter as "open external" would teach "Ctrl+Enter sometimes sends my work away from this app", cutting against what the composer drills in. Shift carrying the "leave" meaning keeps the two chords' semantics consistent across the whole app.

If we only bind one modifier (no edit-here action), the same logic still points at **Shift+Enter for open external**, leaving Ctrl+Enter free for a future in-app variant.

## Plan
**Goal:** In the file explorer — Enter = open internal (unchanged), **Ctrl+Enter = edit here** (preview pane, editable), **Shift+Enter = open external**. Plus an **Edit button** in the preview pane header, local save, and dirty-file guards so the new editing cannot silently lose work.

**Findings (exact paths):**
- `src/FilePanes.cpp` `FileExplorer::eventFilter` (~707): plain Enter only; both modifier chords free. `activate()` (382) routes files to `onOpenFile`.
- `src/FilePanes.cpp` `FilePreview`: editable mode exists but is remote-only (`setEditable` 1387, `save()` remote-only, local `open()` forces `setEditable(false)` at 1101). Header buttons built ~899–918 (`m_mode`, `m_reload`, `m_external`, `m_save` hidden unless editable).
- `src/RelayWindow.h` `openPath()` (611) creates/reuses preview panes; explorer callbacks wired at ~6795. `closePane()` (9221) has no dirty guard — today a remote editable preview closes silently.
- Tests: `tests/filepanes_test.cpp` (patterns: `enterOnFileOpensIt`, `aRemoteTextFileOpensEditableAndSaves`).

**Steps:**
1. `FilePreview`: public `startEditing()` — Markdown switches to Source view first, then `setEditable(true)`, focus the editor. No-op for image/PDF/info kinds.
2. `FilePreview::save()`: local branch — atomic write via `QSaveFile` (UTF-8, matching the UTF-8 read path), success clears ● and posts "Saved · <time>"; failure says why in the notice. Save-shortcut hint entry for local save.
3. Dirty guard in `open()` for local files too (Discard/Cancel before throwing edits away), extending the remote one — covers ⟳ reload, since `reload()` is `open(m_path)`.
4. Header **✎ Edit button** (`filePreviewEdit`): visible for local Text/Markdown kinds while read-only; click → `startEditing()`; hidden while editable and for remote (already editable), image, PDF, info.
5. `FileExplorer` keys: Ctrl+Enter on a file → new `onEditFile` callback; Shift+Enter on a file/folder → new `onOpenExternal` callback, defaulting to `QDesktopServices::openUrl` when unset (the seam the tests stub); remote (`ssh://`) rows ignore Shift+Enter and treat Ctrl+Enter as Enter (remote opens editable already). Folders: Ctrl+Enter navigates like Enter. Mirrored in the filter-box Enter handler. Class comments updated (local files are no longer always read-only).
6. `RelayWindow`: `openPath()` gains `edit = false` → after open/reuse, `preview()->startEditing()`; wire explorer `onEditFile` at pane creation. `closePane()`: a dirty ToolPane Preview asks Save/Discard/Cancel (also closes the existing silent-loss hole for remote edits — part of this feature's safety, not a detour).
7. Shortcut hints (standing rule): menu click "Open external" → "Next time: Shift+Enter"; Edit button click → "Next time: Ctrl+Enter in the explorer".
8. Tests in `tests/filepanes_test.cpp`: Ctrl+Enter fires onEditFile; Shift+Enter fires onOpenExternal (Enter unchanged); local text file startEditing → modify → save → bytes on disk, ● clears; Edit button visible for text, hidden for image; Markdown startEditing lands in Source view.

**Risks:**
- Writes are UTF-8 — the read path already decodes as UTF-8, so round-trip is consistent.
- QSaveFile mitigates truncate-write corruption.
- Dirty-close guard changes close behavior for remote editable previews too (intended).

**Verify:** `scripts/relay-build`; `ctest --test-dir build -R filepanes`; Xvfb live run (Enter read-only, Ctrl+Enter editable, Edit button, Ctrl+S saves, dirty-close dialog) with evidence in `docs/qa_evidence/2026-09-20-edit-here/`. Shift+Enter has no desktop handler under Xvfb — code-read only, as #V9V1 did.

## Tasks

- [x] FilePreview: startEditing(), local save via QSaveFile, local dirty guard in open() <!-- t:xy -->
- [x] Header Edit button (filePreviewEdit) with visibility rules <!-- t:cq -->
- [x] FileExplorer: Ctrl+Enter → onEditFile, Shift+Enter → onOpenExternal (both view and filter box) <!-- t:ey -->
- [x] RelayWindow: openPath edit flag, wire onEditFile, dirty-close guard in closePane <!-- t:p5 -->
- [x] Shortcut hint for menu "Open external" → Shift+Enter (landed as 5c3c6c94 with its test) <!-- t:7j -->
- [x] Tests in tests/filepanes_test.cpp <!-- t:qa -->
- [x] Xvfb evidence run → docs/qa_evidence/2026-09-20-edit-here/ (c7764626 + b3a11c20, 17/17) <!-- t:tp -->

## Done means
- In the file explorer, Enter on a text file opens a read-only preview, Ctrl+Enter opens it editable, and Shift+Enter hands it to the desktop's default app. It fails if either chord does nothing or does the other chord's action.
- A local file edited in the preview saves to disk with Ctrl+S, and closing or reloading a dirty preview asks before throwing edits away. It fails if an edit is lost silently.
- Clicking "Open external" in the menu, or the ✎ button, shows a "Next time:" hint that names the chord. It fails if either click shows no hint.
- A live Xvfb run in `docs/qa_evidence/2026-09-20-edit-here/` shows each of the above.

## Execution Summary
The feature landed in three waves. `5dfec93d` (earlier session) carries the chords, `startEditing()`, the local QSaveFile save, both dirty guards, the ✎ button and the tests. `5c3c6c94` (this session) routes the menu's "Open external" through `onOpenExternal` and adds the "Next time: Shift+Enter" hint plus `openExternalFromTheMenuTeachesShiftEnter`, build-gated by land.py with `ctest -R '^filepanes$'` green in the gated tree. `c7764626` + `b3a11c20` record the live evidence: `docs/qa_evidence/2026-09-20-edit-here/` holds the drive (`drive.sh`, `phases.sh`), 17/17 checked screenshots with OCR sidecars, the stub `xdg-open` log and `verification.md`. The run drove the built-from-main binary: Enter previews read-only, Ctrl+Enter opens editable and typing lands, Ctrl+S writes the bytes to disk, a dirty close asks Save/Discard/Cancel and Discard keeps the saved bytes, the ✎ button and the menu each teach their chord, and both Open-external paths hand the file to the desktop (logged by the stub).

## Tests
- `ctest --test-dir build -R '^filepanes$'` — green in land.py's gated tree at `5c3c6c94` (includes `openExternalFromTheMenuTeachesShiftEnter`, `shiftEnterOnFileGoesToTheDesktop`, `ctrlEnterOnFileOpensItEditable`, `enterOnFileOpensIt`, and the save/guard/button-visibility cases).
- Live Xvfb drive `docs/qa_evidence/2026-09-20-edit-here/drive.sh` — **17 passed, 0 failed** (`notes.txt`), each check backed by a screenshot (`NN-*.png` + OCR `.txt`) or by bytes on disk; the desktop's side of both Open-external paths is in `xdg-open.log`. Reproduce with `./drive.sh <relay> <outdir>`; needs Xvfb, xdotool, import, tesseract.
