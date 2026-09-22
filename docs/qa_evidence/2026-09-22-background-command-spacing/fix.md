# Command gap fix — #BGSP

The Bash load binding requests `OSC 7772;input-gap` after pending job notifications
and before Readline echoes a staged command. TerminalSession handles it in stream
order, adding only the missing blank row. This is shared by the terminal cores;
the core scanner does not interpret the request as a lifecycle or input-row mark.
Native typing and its existing PS1/DEBUG handling are unchanged.

Verified on 2026-09-22:

- `python3 -m unittest tests.test_shell -v`: 12 passed, including notification-before-gap-before-command ordering, native input, multiline input, heredocs, status and row marking.
- `RELAY_ENGINE_TEST=SessionTest build/engine/relay-engine-tests`: 12 passed. Includes a real Bash PTY reproduction with the actual shell integration, exact blank rows around the command and input marking; conditional/double requests, existing gaps, unterminated output, screen edges, alternate-screen exclusion and fragmented PTY sequences.
- `RELAY_ENGINE_TEST=CoreTest build/engine/relay-engine-tests theRowRoleOscMarksItsLine theScannerReportsAnErasedRow osc133PromptMarks`: 5 passed (including setup/cleanup).
- `bash -n shell/integration.bash` and `git diff --check`: passed.
- `scripts/relay-build --target relay-engine-tests` and `scripts/relay-build --target relay`: passed. The GUI build reported an existing missing-field-initializer warning in Pane::ForkText, outside this change.
- `python3 scripts/relay-board.py check --json`: no BGSP/card/thread findings; unrelated existing board errors and warnings remain.

Live GUI: ran rebuilt Relay under Xvfb with isolated HOME/XDG directories and a
throwaway workspace, submitted `(sleep 0.5; printf 'Background output\n') &`, then
`ls`, then a normal `printf`. Inspected `fixed.png`: the Done notice is followed
by one blank row before the cyan `ls` band, another gap below it, then the listing.
The following normal command also has exactly one blank row on either side.
No live user terminal or shell configuration was changed.

The build here enables libvterm only. The session-level implementation is core
independent; Ghostty runtime behavior was not exercised in this environment.
Restart with the rebuilt Relay so the new session code and freshly sourced shell
integration are both in use. Existing shells retain their loaded functions.
