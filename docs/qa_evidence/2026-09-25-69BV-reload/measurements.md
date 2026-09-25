# Reload scrollback evidence for #69BV

The live window identified panes `266dc3f5` and `825839fb`. Their saved layout maps them to
scrollback files `1a09e2d3-c1fb-4d7e-9c6b-f469bce9a749.txt` and
`81e219d7-7544-4ae8-858b-220e6fd8264c.txt`, respectively.

| Pane | Transcript turns | Saved pane bytes | Saved `✦` prompts |
| --- | ---: | ---: | ---: |
| `266dc3f5` | 1 | 19,184 | 31 |
| `825839fb` | 4 | 7,503 | 0 |

For `825839fb`, the saved pane text and conversation sidecar are both 7,503 bytes and contain
only the post-reload recap and related lines. The session JSON still holds 64 messages.

Validation on 2026-09-25: `scripts/relay-build --target relay` succeeded, and
`ctest --test-dir build -R '^transcriptreplay$' --output-on-failure` passed (1/1).
The running desktop app has not been restarted onto this build; an independent reload check
should confirm the restored display.
