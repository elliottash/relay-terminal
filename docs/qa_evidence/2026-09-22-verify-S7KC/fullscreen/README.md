# Independent alternate-screen live drive — PASS

Copied parent edge-drive scenario and ran independently on 11H.05 with latest landed shell script. All three screenshots personally inspected. Remote command enters DEC 1049 alternate screen, displays SCREEN-PROOF and blocks on one-character input. Ctrl+H and q deliver input; returning control restores normal prompt and composer without stray alternate-screen contents. Next command prints AFTER-SCREEN, records exact output and exit 0. Script exited 0.

This verifies actual alternate-screen entry/exit and native input, not a complete curses application or tmux. Real localhost SSH, protocol stub, no paid model calls.
