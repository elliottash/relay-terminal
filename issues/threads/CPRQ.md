<!-- relay:entry 20260923T012600Z-c1 author=claude-code kind=decision -->
### Claude Code · 2026-09-23 01:26 UTC
Owner: "another suggestion: ctrl q to clear the prompt box (like ctrl shift c in claude / codex). or tell me if q should do something" and, two turns later, "ctrl z is undo text edit; ctrl shift z is undo close." Q has nothing else to do in Relay (`src/Keymap.h` binds nothing on Ctrl+Q), so it is the clear key.
<!-- relay:entry 20260923T012600Z-c2 author=claude-code kind=note -->
### Claude Code · 2026-09-23 01:26 UTC
Filed as ready: the owner asked for it and the behaviour in Planning notes needs no further decision (one undo block, empty no-op, prompt box only, plain Ctrl+Q reaches a program that has the keyboard). No code changed.
