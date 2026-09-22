# SSH delivery live evidence — 2026-09-22

Cards #S7GX, #S7KC, #S7CX. Implementer drive on the Linux libvterm build; isolated Xvfb/XDG profile, actual authenticated OpenSSH sessions to localhost and nested 127.0.0.1. A deterministic worker exercises GUI protocol without model calls. Each script cleans up its own profile and control masters. No permanent remote startup-file changes.

## Final drive

`python3 docs/qa_evidence/2026-09-22-ssh-delivery/drive.py` completed against build6 (11:16:59 source snapshot) and the final remote script. Screenshots 01–11, matching OCR and events.jsonl record:

- Local/remote spacing, persistent host identity and remote cwd; no idle command activity.
- Actual per-command busy/queue labels, failure exit 1, command history and bounded output.
- Remote-only completion and host-labelled @ picker; local-only path is not completed remotely.
- Queue during sleep and during read; native control sends ANSWER as stdin, then the queued command runs separately.
- Both physical wrapped rows highlighted after reflow; multiline command has one history entry.
- Nested SSH withholds remote capability, returning to the original shell restores it, exit restores local context.
- Zsh command produces exact ZSH-OUTPUT with exit 1, without a captured percent marker or duplicate prompt.

`handoff.py` records an agent-requested visible remote command with clean HANDOFF_STDOUT and exit 1 returned before SSH closes; see handoff-events.jsonl and screenshot 12. It uses the real GUI handoff protocol and real SSH, with a deterministic worker.

## Targeted checks

- targeted-tests.json: 90 passed, zero failed/skipped: SSH shell, guest bridge, remote tools, attachments. The runner reports a stale prose “no results” message despite its populated 90-test records; counts and per-test results establish the outcome.
- Three selected CoreTest cases passed, including wrappedUserRolesSurviveReflow for live/scrollback role preservation without duplicated prompt boundaries.
- completion, remotefiles and remotesession CTest cases passed.
- scripts/relay-build --target relay relay-engine-tests passed.

Independent backend verifier: ../verify-S7GX/README.md. Independent GUI verifier: ../verify-S7KC/README.md. Earlier verifier findings drove fixes before this final drive.

## Scope

SSH destinations are aliases on the same physical machine, not independent remote machines. Guest MCP transport/name adapters were exercised without paid Codex/Claude model sessions. Linux/libvterm was built and driven; other platforms/cores and mosh are not live-certified. Enhancement off and unsupported/nested shells deliberately withhold authenticated-host tools. Additional edge-case evidence is recorded by the independent verifier. Runtime records contain only fixture requests and output.
