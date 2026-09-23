# Crash recovery drive for #N6R8

`drive.sh` creates an isolated XDG profile with a pane whose saved conversation ID is present but whose guest agent waits for a first prompt. It starts Relay, waits past the 30-second terminal-text checkpoint, kills the process with `SIGKILL`, and checks the layout and scrollback file. It then restarts Relay and checks the saved conversation ID again after startup and layout save. No live user state is touched.

On build `2026-09-23.12H.09`:

```text
PASS: crash leaves session ID and checkpointed terminal text
PASS: deferred guest startup keeps session ID on restart
Evidence: /tmp/relay-crash-recovery.jQHD3z
```

The reported live crash was `SIGSEGV` at 2026-09-23 16:44:18 UTC. The subsequent process wrote eleven panes to `~/.local/share/relay/state/windows.json`, only one with a `session_id`; the other conversation files remain in `~/.local/share/relay/sessions/`. Log `turn_id` values matched six of those ten panes to saved session requests. This drive tests future recovery; it does not reconstruct the overwritten live pane mappings.
