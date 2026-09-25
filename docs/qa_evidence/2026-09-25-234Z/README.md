# #234Z evidence — Alt+Esc leaves an ssh or mosh session

Commit `3ea766714a8c729c34f153924fb431355702431a` on `main`. Everything below ran on the
tree that commit put on `main` (land.py's verify tree, `/tmp/claude-1000/land/234z/verify`),
not on the shared working tree.

| Check | Command | Result |
|---|---|---|
| The new cases, alone, ×3 | `relay-consolemode-tests --234z-only` | `234z: all cases passed` ×3 |
| The whole console suite | `relay-consolemode-tests` | `consolemode: 21 cases, all passed` |
| The queue contract (#XCXD) | `relay-consolemode-tests --xcxd-only` | `queuecontract: all cases passed` |
| #H2KQ's Esc/Alt+Esc cases | `relay-consolemode-tests --h2kq-only` | `h2kq: all cases passed` |
| Keymap unit tests | `relay-keymap-tests` | `Totals: 7 passed, 0 failed` |

`tests/234z_cases.h` drives a real pty. Case 1 runs a foreground program whose `argv[0]`
says `ssh` and which ignores SIGINT (`bash -c 'trap "" INT; exec -a ssh sleep 30'`) — the
way a session client only passes a `^C` through to the far side — then presses Alt+Esc
once: the busy line had named the exit key (`… · Alt+Esc exits`), the client's whole
process group is dead, the stop strip clears, and the toast says `Exited ssh`. Case 2 runs
the same INT-ignoring program named `bash`: the first press is still only Ctrl+C (the
program survives it), and the press after the beat SIGTERMs it — #H2KQ's two-press rule
is unchanged for programs that are not session clients.

Two things the work found on the way, both fixed in the same commit:

- `forceInterruptShell`'s kill stage signalled `kill(-foregroundPid())`, but the backend's
  foreground pid is a member of the process group, not always its leader
  (`bash -c 'sleep 30'` has `sleep` in front) — the SIGTERM could hit a group that does
  not exist and silently do nothing. The kill now resolves the group from the kernel's
  `tpgid` (`foregroundProcessGroup()`), which is the group a stop must signal.
- One hunk of this card (the processBusy branch naming the exit key) was swept into the
  #6CSN commit `960c0e70` while it sat uncommitted in the shared tree; the rest landed in
  `3ea76671`. No action needed — the hunk is correct on `main`.

Not covered here, on purpose: a session client nested inside a multiplexer (ssh inside
zellij) still shows the multiplexer as the pane's program, so Alt+Esc stops the
multiplexer, not the inner session — the pane can only see its own foreground program.
