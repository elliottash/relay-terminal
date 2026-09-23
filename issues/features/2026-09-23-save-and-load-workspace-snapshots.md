---
id: 6WKR
type: work
status: planned
labels: [feature, sessions]
assignee: ''
rank: m
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [], related: [N6R8], github: null}
---
# Save and load workspace snapshots

## Issue
can you save the current workspace so if it doesnt work, i can still restore manally? and add a card for a save/load workspace function

## Done means
Relay has palette actions to save the current workspace as a named snapshot and to load one back. A snapshot is a self-contained, portable folder holding the window/tab/pane layout, each pane's conversation reference, its saved terminal scrollback and its prompt history, plus the session files those references point at, under a versioned manifest. Loading names what will be replaced and asks before touching the live workspace, and it reports missing or unreadable session data before changing anything. Failure shows as: a snapshot that cannot be loaded back after a quit (layout, conversation or text missing), a load that silently discards the current workspace, or a corrupt/incomplete snapshot that changes the layout instead of being refused with a reason.

## Planning notes
The current automatic restore uses one `state/windows.json` layout and separate scrollback and session files. Design the save/load action around those existing formats, with a versioned manifest and an atomic restore path. Cover a failed restart and a snapshot containing session files that are no longer present in the live store.

## Plan
**Goal.** Give the owner an explicit "save this workspace under a name / load it back later" pair of actions, built on the existing restore formats, producing a portable folder that can also be restored by hand when Relay cannot start.

**Findings.** Everything a snapshot needs already has a file format and an atomic writer:

- Layout: `$XDG_DATA_HOME/relay/state/windows.json` (schema-versioned, `relay::windowstate`, `src/WindowState.h`/`WindowState.cpp`); captured by `WindowManager::captureWindows()`, written by `writeWindows()`/`saveLayoutNow()`, read back by `restoreSavedLayout()` (all `src/WindowManagerImpl.h`). Pane nodes carry `session_id` and a `scrollback` id.
- Per-pane terminal text: `state/scrollback/<pane-id>.txt` (`windowstate::writeScrollback`/`readScrollback`/`scrollbackIds`), checkpointed every 30 s by `m_scrollbackTimer`.
- Per-pane prompt history: `state/prompt-history/<pane-id>.txt` (`relay::prompthistory`, `src/PromptHistory.h/.cpp`), same pane id.
- Conversations: Relay sessions live in `$XDG_DATA_HOME/relay/sessions/<workspace-digest>/<id>.json` with sidecars `.meta.json`, `.scrollback.txt`, `.rewound.jsonl`, `.rewound-<n>.scrollback.txt`, `.blobs/`, `.threads/` (`backend/relay_core/sessions.py`); guest scrollbacks live in `relay/sessions/guests/<source>/<id>.scrollback.txt` (`backend/relay_core/conv_index.py`).
- Actions: registered in `src/Keymap.h` (`add("windows.fresh", "window", …)`, line ~285), listed in the palette in `RelayWindow.h` (~line 5090), dispatched in the `RelayWindow` action if-chain (~line 1331). Confirmation precedents: `Start a fresh window set` uses `QMessageBox::question` (`RelayWindow.h`:1216).

**Steps.**

1. **New pure module `src/WorkspaceSnapshot.h/.cpp`** (`relay::snapshot`, same style as `windowstate`): `save(name) -> path|error` and `inspect(path) -> manifest|error`. A snapshot is `$XDG_DATA_HOME/relay/snapshots/<slug>/` with a versioned `manifest.json` (`{"version": 1, "name", "created", "counts": {windows, tabs, panes}, "sessions": [{id, source, session_dir, files}], "scrollback_ids": [...]}`), a copy of `windows.json`, `scrollback/` and `prompt-history/` copies of the ids the layout names, and `sessions/` holding each referenced session's `.json`, `.meta.json` and scrollback/rewound sidecars (guest sources included; `.blobs/` copied too — they are content-addressed and bounded by what checkpoints referenced). Names are slugified and validated like scrollback ids; a manifest with a foreign version or a bad id is refused. Include a short `README.md` inside the snapshot folder describing manual restore (copy the files back over `state/` and `sessions/`).
2. **GUI save action** `workspace.snapshotSave` ("Save workspace snapshot…"): force `saveLayoutNow()` + `saveScrollbacks()` first so the snapshot is current, ask for a name with `QInputDialog`, call `relay::snapshot::save`, then `notice()` the result path. Register in `Keymap.h`, the palette list and the dispatch chain; add a shortcut-hint registry entry per the standing rule (no default keybinding).
3. **GUI load action** `workspace.snapshotLoad` ("Load workspace snapshot…"): pick from the snapshots present (name, date, pane counts from each manifest). Show a `QMessageBox` naming what will be replaced (current window/tab/pane counts vs the snapshot's) and listing any session files the manifest names that are missing or unreadable in the snapshot *before* anything changes; Cancel leaves everything untouched. On confirm: suspend layout saving, close the live windows, stage the snapshot's state files into `state/` via the existing atomic writers (`windowstate::write`, `writeScrollback`, prompthistory file copy), copy session files back into their recorded `session_dir`s (never overwriting a *newer* session file — skip and report), then call `restoreSavedLayout()` and re-enable saving. A load that fails staging mid-way must leave the previous `state/` intact (stage into a temp dir, then swap).
4. **Docs**: `docs/ARCHITECTURE.md` state-files section gets the snapshot folder; `README.md`'s restore paragraph names the two actions.

**Risks / decisions for the owner.**

- Live load closes the current windows; the alternative (load on next start only) is safer but much less useful. Plan goes with live load behind a confirmation — flag if you disagree.
- `.blobs/` (code-rewind pre-images) can be sizeable; plan copies them so a restored workspace keeps `/rewind-code`. Alternative: skip and accept that rewind history is lost on load.
- Loading overwrites session files shared with *other* workspaces' panes if the same conversation is open elsewhere; the newer-file skip mitigates but does not eliminate this.

**Verify.**

- New `tests/snapshot_test.cpp` (ctest) for the pure parts: manifest round-trip, foreign version/bad id refused, missing-session reporting, staged restore leaves old state on induced failure.
- An Xvfb drive under `docs/qa_evidence/2026-09-23-workspace-snapshots/` with isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`: build a two-window workspace with conversations and scrollback, save a snapshot, close everything / corrupt `state/`, load the snapshot, assert layout, session ids and scrollback return; assert a snapshot with a deleted session file is refused with the missing file named.
- `scripts/relay-build --target relay` and the new ctest case; no full suite.
