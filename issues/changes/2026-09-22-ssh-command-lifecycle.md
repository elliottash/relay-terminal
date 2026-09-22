---
id: S7KC
type: work
status: needs-verification
labels: [bug, ssh, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
priority: 2
rank: mssh2
created: '2026-09-22'
source: 'Owner in Relay, 2026-09-22; delivery follow-ups to #SHPA'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/, docs/qa_evidence/2026-09-22-ssh-delivery/, docs/qa_evidence/2026-09-22-verify-S7KC/], related: [SHPA, S5SH, S7GX, S7CX], github: null}
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
- [x] Separate SSH transport, remote prompt and remote command state; reuse the host badge and fix activity/tooltips. <!-- t:wx -->
- [x] Feed remote command boundaries into shared capture, history, failure, timing, queue and handoff handling. <!-- t:9c -->
- [x] Share shell formatting behavior and negotiate prompt/redraw/lifecycle capabilities independently. <!-- t:yd -->
- [ ] Run focused shell/GUI tests and live-drive Bash/zsh, failures, sleep, read/password prompts, Ctrl+C, multiline input, full-screen apps, tmux, disabled enhancement and reconnect; document mosh limitations. <!-- t:yr s=in-progress -->

## Plan
**Goal:** Deliver the existing Done means with live evidence.
**Findings:** See Planning notes and #SHPA; the audited paths are unchanged.
**Steps:** Connect remote prompt/command marks to a per-command lifecycle; separate SSH connection status from busy status, add shell spacing/row roles, queue remote commands at prompts and finish command capture/handoffs on remote exit marks. Test shell hooks and live-drive idle, failure, sleep, input, interruption and reconnect.
**Risks:** Shared checkout; preserve other work. Remote and local command ownership must remain separate; do not route stale remote requests locally. No permanent remote shell startup changes.
**Verify:** Targeted regression tests plus isolated Xvfb/real localhost SSH; record precise limits of guest and platform testing. Independent verifier reviews acceptance and live evidence after implementation.

## Execution Summary
Remote commands now use the common submission/queue path, per-command exit/output capture, failure/watch handling, handoff reports and duration notifications. The SSH badge/cwd chip represents the connection; idle SSH clears the command activity line. Bash/zsh hooks add local-style spacing and row roles. Bash stages actual Readline rows and emits the presentation hook once for multiline submissions. Remote commands queued before disconnect are discarded, never executed locally.
Independent live verification found and drove fixes for wrapped-row colors lost in live/scrollback reflow, a misleading outer-SSH queue label, and zsh's transient end-of-line marker leaking into captures. Reflow preserves role bits across wrapped rows; zsh stages its buffer without redrawing the prompt and marks the output boundary before its temporary prompt marker.

## Tests
`tests/test_ssh_shell.py`
`ctest -R '^(completion|remotefiles|remotesession)$'`
manual: docs/qa_evidence/2026-09-22-ssh-delivery/
manual: docs/qa_evidence/2026-09-22-verify-S7KC/

Targeted engine check: `RELAY_ENGINE_TEST=CoreTest build/engine/relay-engine-tests wrappedUserRolesSurviveReflow theRowRoleOscMarksItsLine clearingARowInFullDropsItsRole` — 3 regression cases passed. Shell suite: 26 passed.

### Check 2026-09-22 11:19
- passed · unittest:tests.test_ssh_shell — tests/test_ssh_shell.py passed for this revision on spark-dcc9, 2026-09-22T15:19:41Z
- passed · ctest:(completion|remotefiles|remotesession) — ctest -R (completion|remotefiles|remotesession) passed for this revision on spark-dcc9, 2026-09-22T15:07:21Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-ssh-delivery/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-ssh-delivery/
- not-applicable · manual:docs/qa_evidence/2026-09-22-verify-S7KC/ — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-verify-S7KC/
- notice · unittest:tests.test_ssh_shell — tests/test_ssh_shell.py: 1 of 26 are slow (test_zsh_over_ssh)
- warning · card — none of the listed tests is named after anything this card changed (issues/changes/2026-09-22-guest-ssh-tools.md, issues/changes/2026-09-22-ssh-command-lifecycle.md, issues/changes/2026-09-22-ssh-host-context.md…)
history: thread
