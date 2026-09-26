---
id: T7XQ
type: work
status: inbox
labels: [bug, isolation, terminal]
rank: m
created: '2026-09-26'
source: 'C1 verifier (#8J0A) staging the Relay GUI under Xvfb for #3MH4, 2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [8J0A, 3MH4, Y4RX], github: null}
---
# An isolated profile shares the local tmux holder socket, and a too-long socket path leaves a blank window

## Issue
Two faults in the local tmux holder (`shell/remote-holder.sh`, `tmux -L relay`), measured while staging the #3MH4 GUI under Xvfb with an isolated profile (fresh `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR`, `HOME`), relay built at 4af516ea.

1. **Cross-profile adoption.** The holder socket is tmux's default `/tmp/tmux-$UID/relay`, which none of those variables moves. The isolated `relay --fresh -w <repo>` shared the socket with every other Relay on the machine (63 sessions from other sessions' test profiles), and its first window showed a pane from another session's detached tmux session (`proj2` under a W6ES scratch path, created at 14:01) instead of the staged project. Setting `TMUX_TMPDIR` to a private directory stops it.
2. **Blank window for a long socket path.** With `TMUX_TMPDIR` under the scratch dir, the socket path `<TMUX_TMPDIR>/tmux-1000/relay` was about 115 bytes, over the 107-byte `sun_path` limit. Each new pane leased its workspace and started a worker (`worker_start pane=… workspace_set=1` in relay.log), but no terminal pane ever appeared, and Ctrl+T added nothing visible. No banner, no log line. A short `TMUX_TMPDIR` (`/tmp/c1t.XXXX`) fixed it.

Expected: a profile isolated through the XDG variables gets its own holder socket, and a holder that cannot start says so in the pane.
