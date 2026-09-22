# SSH parity audit — #SHPA

2026-09-22. Analysis and live UX investigation; no production code changed.
Source review started at `8966917e`. Tested existing Linux binary `2026-09-22.09H.01`,
SHA256 `ec57a953ac25ce88ab21e6a5ca77dbdd08f2fc13fb33bd7e2619ede3b509ffef`.
This is a recorded build, not a claim that the entire current checkout was rebuilt.
The relevant shell and Pane sources were clean during the audit.

## Main finding

SSH enhancement works, but provides a separate, smaller integration than the local shell.
Missing spacing is a real parity gap, not evidence that all enhancement failed. The drive logs
`login_resolved ... shared=1` and `login_enhance`; remote OSC prompt shading appears. There is
already a persistent host badge and hatched header. SSH need not also occupy the cyan activity line.

## Confirmed in the live GUI

| Priority | Finding | Evidence and cause | Repair |
|---|---|---|---|
| P1 | Idle SSH continually shows `Relaying · ssh…`; running `sleep 7` looks the same. | Shots 02, 05, 06. `Pane::processBusy` (src/Pane.h:955) measures the local foreground process group; `refreshBusyLine` (12816) treats SSH as the command. Its tooltip even says prompts queue until SSH exits, which contradicts the remote dispatch path. | Keep a persistent `SSH · user@host` badge; suppress command activity at a remote prompt. Show the submitted remote command while it runs, and clear it on completion. Preserve an honest unknown state when enhancement is absent. Do not globally redefine processBusy without auditing its other consumers. |
| P1 | Composer completion uses the wrong filesystem. | Shot 04b: while remote cwd is `/tmp`, `cat LOCAL_ONLY_SSH_AU` + Tab becomes `cat LOCAL_ONLY_SSH_AUDIT.txt`, a fixture present only in the local workspace subdirectory. `completeInComposer` (16359) calls `completeAt` with local m_cwd and knownCommandNames. | Use the remote cwd and remote filesystem/command source, with asynchronous completion. Until supported, clearly disable local completion inside SSH. |
| P2 | Remote commands lose the local prompt/command/output spacing and strong command-row styling. | Shot 03 shows local and remote output side by side. Local shell/integration.bash:279 marks entered command rows with OSC 7772; line 311 prints a newline before output; line 352 adds prompt spacing. Remote script only wraps existing PS1 with OSC 133 and emits command marks; it implements neither local spacing nor OSC 7772 input-row marking. | Share a presentation contract between local and remote hooks, including wrapped/multiline commands and user PS1 preservation. |
| P2 | Remote cwd is not clearly represented in the composer. | Shot 04: remote `cd /tmp; pwd` prints `/tmp`, but the directory chip still names the local test workspace. onCwdHostChanged (10833) stores m_login.cwd separately. | Show explicit host and remote cwd while keeping local project context separately named. Never replace the actual local working directory with the remote path. |
| P1 design gap | A shell-forced line submitted while a remote program reads input is delivered to that program. It is not a managed queued command. | Shot 07: remote `read` receives `echo SHOULD-BE-A-COMMAND` as its answer. `loginTakesLines` (16635) permits input whenever the primary screen is active, even when remote atPrompt is false; `typeIntoLogin` (16794) writes directly to the PTY. The toast describes the line as waiting while it is actually consumable as stdin. | Explicitly distinguish answering an interactive prompt from queuing a shell command. An answer field may intentionally feed stdin, as local Relay already supports; a queued command needs visible pending state, cancellation and a prompt-bound dispatch. This reproduction alone does not mean intentional stdin forwarding is wrong. |

## Additional gaps established by code review

1. **P1: remote commands do not enter the full local lifecycle.** `dispatch` returns through
   `typeIntoLogin` before local staging. That function remembers composer text but does not set
   command capture, command suggestions, command log, fix state, handoff completion or timing.
   `onPromptMark` stores the remote exit code in `m_lastMarkExitCode`, which has no reader in
   Pane.h. Local `pollShell` ready/loaded handling (17885–17985) does all this work. Consequently
   per-command exit feedback, completion notifications, failure/fix handling and indexed command
   records do not have remote parity. Do not claim all history is absent: editor recall remains.
   The local outer SSH command remains the capture unit; its capture truncates at the first remote
   OSC 133 A in `finishCommandCapture`, so it is not a substitute for remote command records.
2. **P1: file attachment context remains local.** `resolveComposerPath` (16448),
   `attachmentsFor` (16527), and the @ picker use m_cwd/QFileInfo. An identical relative path on
   both machines can select the local file while the terminal shows SSH. Code-only finding;
   no provider upload was performed. Remote-aware attachment lookup should accompany completion.
3. **P1 risk: nested SSH has no host-session stack.** `beginLogin` resolves the local SSH process;
   remote OSC 7 updates accept its path but discard its host while m_login.active. A subsequent
   nested remote login can therefore change visible terminal destination/cwd without changing the
   control socket used by host tools. This is a code-derived risk, not a reproduced wrong-host
   tool operation. Test two distinct hosts and disable host tools on an unverified identity change
   until an explicit remote-session model handles it.
4. **P2 risk: any OSC 133 marker is treated as Relay enhancement.** onPromptMark sets
   m_login.integration=true for every marker; maybeEnhanceLogin then skips bootstrap, while inline
   redraw assumes Relay's Ctrl+X Ctrl+P binding. An existing third-party shell integration could
   supply marks without that binding. Capability negotiation must distinguish marks, redraw,
   cwd, command text and lifecycle support. Code-only finding, requiring a pre-integrated host test.

## What worked and test limits

- Real OpenSSH transport to localhost, connection sharing, automatic remote enhancement,
  composer commands, remote prompt marks, host header badge, and return to the local shell.
- The driver starts a clean remote interactive Bash with HISTFILE=/dev/null. No remote startup
  files are edited. XDG settings/runtime/data and the GUI display are isolated; only its own
  processes and SSH masters are cleaned up.
- The worker is a protocol stub to keep this terminal UX drive free of provider calls. It does
  not validate live agent responses, actual agent tools, model routing quality or guest harnesses.
- This run does not establish zsh, mosh, tmux, alternate-screen apps, nested-host behavior,
  Windows/macOS parity, or degraded integration behavior. Existing unit tests cover portions,
  but a release claim needs live scenarios for those environments.

## Repair order

1. Separate transport connection state from remote command state; reuse the existing SSH badge.
2. Introduce remote command start/end/text/exit/cwd events and feed common completion, capture,
   history, failure, queue and notification handling. Keep local process control separate.
3. Give queued commands and program answers distinct behavior; guard nested host transitions.
4. Bring spacing and command highlighting into the shared shell presentation contract.
5. Make cwd display, Tab completion, @ attachments and suggestions host-aware.
6. Exercise bash/zsh, enhancement on/off, failed commands, queued commands, stdin prompts,
   Ctrl+C, multiline input, reconnect, tmux, full-screen apps and two-host nested SSH live.

## Verification

- `python3 docs/qa_evidence/2026-09-22-ssh-parity/drive.py` — completed twice; second adds local-only completion fixture. Eight numbered scenes plus 04b, PNGs and OCR text saved here.
- `python3 -m unittest discover -s tests -p 'test_ssh_shell.py' -q` — 25 passed.
- `PYTHONPATH=backend python3 -m unittest discover -s tests -p 'test_ssh_remote.py' -q` — 46 passed.
- `ctest --test-dir build -R '^(remotesession|remotefiles|sshconfig)$' --output-on-failure` — 3 passed.
- Initial pytest invocation could not run because system Python has no pytest; the unittest suites above ran successfully instead.
- These passing tests coexist with the reproduced failures. Add GUI/lifecycle coverage to the fixes.
