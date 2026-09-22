# Independent rerun on 11H.03

Both `../final-drive.py` and `../final-handoff.py` exited 0 after the final build success marker (58 seconds, start 11:06:11; full log in build-marker.txt). Original build5 evidence is preserved in the parent folder.

**Verdicts remain: #S7KC fails full acceptance; #S7CX scoped GUI checks pass with earlier limits; #S7GX Bash GUI handoff passes.**

- Still reproduced: first physical row only highlighted for the wrapped command (`07c-wrapped-command.png`), extra `%` in Zsh history (`events.jsonl`, `event-checks.json`), queue running row naming outer ssh rather than sleep (`05b-queued.png`).
- Additional visual observation on this rerun: `10-zsh-command.png` displays the zsh prompt twice after the failing command. The first build5 capture showed one prompt; this is recorded without attributing a cause.
- Normal explicit shell failure now prints the expected no-provider message naming localhost (`04-remote-failure-cwd.png`). Exit-1 history still passes.
- Remote completion and picker, read/queue separation, multiline output, original-shell capability restoration after nested SSH, and local restoration still pass.
- Handoff rerun displays command and HANDOFF_STDOUT, and worker ask report contains clean output and exit 1 before SSH closes (`12-terminal-handoff.png`, `handoff-events.jsonl`).
- `verify-events.py`: 11 true checks, zsh clean-output false. Detailed criteria and same-machine/stub/no-model limits remain in ../README.md.

Static review: `onCwdHostChanged` hides both popup types on remote cwd change; `endLogin` hides both on exit; `typeIntoLogin` revokes identity and publishes it before Enter; `runInTerminal` sets watch/fix state for remote commands; `startFix` uses remote cwd and explicitly requires remote host; completion suggestions carry host:cwd and suggestions.py displays that as Directory. These were reviewed as code, not all separately exercised through provider calls.

No implementation edits, card updates, status moves or commits. No relay_board messaging tools exposed in this harness. Parent coordination required before landing evidence. Land session ssh-verifier-a2.
