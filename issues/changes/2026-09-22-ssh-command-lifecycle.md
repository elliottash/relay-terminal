---
id: S7KC
type: work
status: inbox
labels: [bug, ssh, terminal]
assignee: null
priority: 2
rank: mssh2
created: '2026-09-22'
source: 'Owner in Relay, 2026-09-22; delivery follow-ups to #SHPA'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/], related: [SHPA, S5SH, S7GX, S7CX], github: null}
---
# SSH commands share local lifecycle, spacing and activity behavior

## Issue
great, add these issues to a card or cards so we can then deliver them

look at how relay deals with ssh. i am noticing the line breaks before and after commands arent there. that indicates to me that some of the main features / functionality isnt working in ssh. 

i also dont want it to be showing cyan "relaying - ssh..." the whole time. there should be a clear indicator for ssh, but it shouldnt be the general shell command indicator. 

first analyze the code and find bugs and issues to reach parity with the non-ssh terminal. then live drive it to check for any UX issues

## Planning notes
Reproduced with real enhanced localhost SSH in `docs/qa_evidence/2026-09-22-ssh-parity/`: shots 02/05/06 show identical cyan “Relaying · ssh…” while idle and running sleep; shot 03 compares missing remote spacing/command styling with local commands; shot 07 shows a submitted line becoming a remote read's stdin.

`src/Pane.h`: processBusy/refreshBusyLine report the local SSH transport; dispatch/typeIntoLogin bypass local command capture, timing, suggestions, queue/failure and handoff completion paths. onPromptMark stores an otherwise unused remote exit code. Editor recall does work; the gap is the richer per-command lifecycle. `shell/remote-integration.sh` omits local integration.bash spacing and OSC 7772 command-row marking.

Code-only risk to reproduce: arbitrary OSC 133 marks set integration=true, skipping bootstrap even if Relay's redraw binding is absent. Separate capability detection from prompt-mark detection.

Second delivery priority; coordinate the common remote-session contract with #S7GX and #S7CX. Preserve intentional replies to programs: the input reproduction is a misleading queue/input distinction, not evidence that all stdin forwarding should be removed.

## Done means
- The persistent SSH badge identifies the connection; an idle remote shell has no general command-busy indicator. During a remote command, activity describes that command and clears at completion.
- Enhanced remote Bash and zsh match local command spacing and highlighting for ordinary, wrapped and multiline commands without damaging user prompts or full-screen programs.
- Each remote command records its text, host/cwd, bounded output, exit status and timing through common completion/history/notification/failure handling; terminal handoffs complete per command, not when SSH exits.
- Queued shell commands are visible, cancellable and dispatched at a verified remote prompt. Program answers are explicitly treated as stdin; the UI never claims an already-consumed line is queued.
- Reconnect/exit restores local behavior; partial or disabled enhancement and third-party OSC marks do not falsely promise redraw/lifecycle capabilities or corrupt output.

## Tasks
- [ ] Separate SSH transport, remote prompt and remote command state; reuse the host badge and fix activity/tooltips. <!-- t:wx -->
- [ ] Feed remote command boundaries into shared capture, history, failure, timing, queue and handoff handling. <!-- t:9c -->
- [ ] Share shell formatting behavior and negotiate prompt/redraw/lifecycle capabilities independently. <!-- t:yd -->
- [ ] Run focused shell/GUI tests and live-drive Bash/zsh, failures, sleep, read/password prompts, Ctrl+C, multiline input, full-screen apps, tmux, disabled enhancement and reconnect; document mosh limitations. <!-- t:yr -->
