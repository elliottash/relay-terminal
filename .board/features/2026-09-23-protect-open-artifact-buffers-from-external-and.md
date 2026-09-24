---
id: F8R7
type: work
status: executing
labels: [feature, files, editor, artifacts]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Editable workspaces for Relay.md
links: {plans: [], commits: [], evidence: [], related: [P2W8, MEPR], github: null}
---
# Protect open artifact buffers from external and agent edits

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**Scope from the editable-workspace report and #P2W8.** `FilePreview` is already an editor for local text/Markdown and SSH text; it saves locally with `QSaveFile`, but it does not watch local files or compare the current disk revision with the revision loaded into a dirty buffer. The worker's file tools can write the same file. Keep the existing editor and one authoritative buffer per open file; do not build a second canvas data store. Work on local files first, then use `RemoteFile`'s version/conflict path for SSH parity. Agent writes to an open file should pass through a revision-aware patch boundary and appear as a named undo step; writes to closed files retain ordinary disk behavior. A conflict must expose base, human buffer and external revision, with explicit resolution.

## Done means
- An open local file detects external in-place writes, atomic replacements, removal and recreation; a dirty editor buffer is never silently replaced. Saving compares the loaded/merged revision with disk and presents merge, overwrite and reload choices when they differ.
- Non-overlapping edits to the same file from a human and an agent merge into the open buffer without losing cursor/selection or undo history. Overlapping edits stop at an explicit conflict view that preserves all three versions.
- Agent edits to an open buffer carry turn/provenance metadata, are undoable as one named step and are visible in a change view; a stale patch fails with a recoverable conflict instead of writing over newer text.
- The same revision-aware behavior works for SSH-hosted text, using the host revision checks already present; remote failures preserve the dirty buffer.
- Targeted tests cover in-place and atomic replacement, save races, clean and conflicting merges, open-buffer patch and undo, and SSH stale-save behavior.

## Plan
**Goal.** Make an open file a safe shared surface for manual and agent editing.

**Findings.** `src/FilePanes.{h,cpp}` owns the text buffer and local save; `src/RemoteFiles.{h,cpp}` owns remote file transfer. There is no local `QFileSystemWatcher` or revision-aware save; the agent file tools operate on disk.

**Steps.** 1. Define a file revision token and base snapshot used by local and remote editors. 2. Observe local file/directory changes, including atomic replacement, and reconcile clean buffers automatically. 3. Add a three-way merge/conflict surface and save-time revision check. 4. Route worker file patches for open files through the pane buffer with an explicit revision token and provenance; preserve undo/cursor state. 5. Extend the same contract to SSH text and surface failures. 6. Add focused tests and a live two-pane QA scenario.

**Risks.** File watchers may coalesce changes; hash/content checks must be authoritative. Agent writes can arrive while a human is typing; stale-revision refusal and merge must be deterministic. Large or binary files remain outside the text editor contract.

**Verify.** Headless merge/revision tests, file-pane tests under Xvfb and a live scenario where a human types while the agent edits the same file.

## Tasks

- [ ] Define revision/base snapshot and reconcile local file changes <!-- t:ys -->
- [ ] Implement conflict UI and revision-aware local save <!-- t:0m blocked_by=ys -->
- [ ] Apply open-file agent patches to the editor buffer with undo/provenance <!-- t:v5 blocked_by=ys -->
- [ ] Extend conflict and patch contract to SSH text <!-- t:cb blocked_by=0m,v5 -->
- [ ] Verify races, merge cases and live concurrent editing <!-- t:a8 blocked_by=0m,v5,cb -->
