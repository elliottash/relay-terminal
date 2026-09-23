This opens the fast developer build on a disposable local workspace and isolated
Relay profile under `/tmp/claude-1000/tryit/BKMC`. It has no saved conversations,
closed panes, account settings, or external connections from the owner's normal
profile. The script opens two panes so one can be closed during the short task.
The isolated runtime may briefly show a per-pane memory-isolation notice because
it has no systemd user session; that does not affect keyboard behavior.
