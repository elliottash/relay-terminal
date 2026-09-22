# Independent final PATH/lifecycle drive — 11H.05

Passed expanded drive on build7, after build success marker (11H.05; 58 seconds; start 11:22:45). Implementation commits 5e07a8d6 and 0b4b1798. No implementation edits by verifier.

- Custom interactive PATH now works: created remote/bin/remote_only_probe, exported that bin into the visible remote shell PATH, and composer Tab expanded remote_only_p to remote_only_probe (`13-remote-command-completion.png`). This resolves the build6 finding.
- All 12 recorded event checks pass (`verify-events.py`, `event-checks.json`): ordinary clean output/host/exit, failure exit 1, seven-second queue ordering, read ANSWER versus separate queued command, multiline output, zsh exact output and exit 1, withheld nested binding and restored original binding, context clearing, Ctrl+C exit 130, cancelled command never dispatched.
- Wrapped command remains highlighted on both physical rows. Queue running label names actual command. Missing socket availability refuses lookup with explicit message; local completion restores after exit. Screenshots preserved for each checkpoint.
- `delayed7/` separately verifies standard remote command completion and actual two-second delayed replies discarded after draft edit and after SSH disconnect. Shim logs three delayed compgen requests and executes real /usr/bin/ssh with original socket/arguments.
- Handoff was independently verified on 11H.04, with exact HANDOFF_STDOUT and exit 1 before SSH closes; later build7 changes concern PATH payload/completion. Evidence retained in ../build6/.
- Latest targeted shell test run: `PYTHONPATH=backend:. python3 -m unittest tests.test_ssh_shell -q` — 27 passed in 4.913 seconds.

Remaining narrow native-control issue: committed staging-only zsh widget does not prevent Ctrl+H from erasing inline error and duplicating the preceding prompt. Isolated conditional-widget trial succeeds and preserves the message (`../zsh-trial/README.md`); parent may apply that implementation. Trial is not represented as a landed fix.

Actual SSH uses localhost and 127.0.0.1 as distinct authenticated SSH destinations on one physical machine. GUI worker is deterministic stub; no paid model CLI. Missing socket experiment temporarily renames the isolated socket; delayed shim delays subprocess execution, not network packets. No full-screen/tmux/disabled-integration/custom-prompt live claim. Suggestions reviewed statically only; attachment content verified by a1 separately.

Verifier added QA checklists/scoped verdicts to S7KC/S7CX and GUI addendum to S7GX, with no status movement. Board check reports pre-existing global issues (12 errors, 754 warnings); none of its errors identify these three cards/threads. Evidence and cards land through ssh-verifier-a2 only.

## Native-input follow-up resolved

Parent landed conditional redraw as 2944d892. Fresh verifier zsh-final.py uses the actual repository script and passes: inline error preserved, one fresh prompt, no adjacent duplicate. See ../zsh-final/README.md. Earlier pending-artifact notes are superseded by this result.
