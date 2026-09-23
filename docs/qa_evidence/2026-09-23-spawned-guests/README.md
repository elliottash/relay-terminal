# #K9QA — spawned guest rows in Sessions

The user's `SJTR` search showed the QA research conversation and several Codex rows titled with
Relay's setup instructions, despite **Subagent threads** being unchecked. The Codex rows were native
transcripts of the research subagents. The separate 691-turn Claude Code conversation was started
from Claude's CLI and has no Relay agent-session copy.

Read-only inspection of the native transcript headers found the relevant provenance markers:
Codex rollouts launched through Relay have `session_meta.originator = "relay"`; the research
subagents have that marker. Relay-launched Claude transcripts use `entrypoint = "sdk-cli"`.
The independent QA planning conversation uses Claude's `entrypoint = "cli"`.

## Focused checks

`PYTHONPATH=backend:tests python3 -m unittest test_guest_sessions test_conv_index` passed:
235 tests. These include parser classification for both guests, combined and guest-only search,
incremental guest updates, and an in-place v7 → v8 index migration.

An isolated temporary index was populated from the saved `#SJTR` Relay session, its four saved
subagent thread files, and three native Codex research transcripts. It did not touch the live
Sessions index. Search results:

```text
combined SJTR:       agent a502516a…
with subagent rows:  agent a502516a…; subagent 969e6a0c…, 9a47d8cb…, df0a0617…, 81d2f256…
Codex-only SJTR:     codex 01a0c6b5… (three native transcripts)
```

The running Relay app has not been restarted with this backend code. A separate verifier should
capture the Sessions pane after restart: `SJTR` with the box off and on, plus the Claude-only and
Codex-only source views.
