# Fast restart preserves the saved layout (#K6KP)

`drive.sh` runs three Relay processes in an isolated Xvfb/XDG profile. It seeds two tabs and two scrollback files, starts the owner, starts a second Relay before the owner quits, and checks that the second instance neither replaces the layout nor prunes its scrollback. A third, clean launch checks that the saved two-tab layout remains usable.

Run: `bash docs/qa_evidence/2026-09-23-restart-layout-lock/drive.sh`

Result on build `2026-09-23.10H.04`:

```text
PASS: secondary instance did not replace the two-tab layout or prune its scrollback
PASS: later clean launch preserved both tabs
```

The real 2026-09-23 incident had the replacement process start at 10:43:56 while the old process exited at 10:43:57 (`~/.local/share/relay/logs/relay.log`). The live layout file now has only one tab; the earlier layout and its per-pane scrollback files were overwritten/pruned before this fix. The individual conversation files remain in the Sessions store.
