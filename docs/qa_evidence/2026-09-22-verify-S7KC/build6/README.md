# Build6 / 11H.04 preliminary verifier findings

Fixed visually: wrapped command highlights both physical rows; queue running row names sleep 7 (05b-queued). GUI Bash handoff remains clean and completes before disconnect.

Expanded checks: queue cancel via x succeeds; Ctrl+C sleep returns exit 130 with empty queue and idle state (15-after-cancel-interrupt). Renaming the isolated master socket causes explicit "Remote completion unavailable until the SSH shell is ready", with no local-only name fallback (16-missing-socket-lookup).

New coverage limit/finding: create remote/bin/remote_only_probe and export that directory into the interactive remote PATH; composer Tab on remote_only_p does not complete (13-remote-command-completion). This may reflect noninteractive SSH completion using a different PATH from the visible shell; needs parent assessment against command completion criterion.

Zsh comparison screenshots in ../zsh-compare/ isolate native versus composer prompt rendering. Remaining main drive still in progress. No implementation edits or card writes.

## Completed results

Both drives exited 0. All 12 original event checks now pass, including exact zsh output ZSH-OUTPUT. Additional history checks confirm sleep 20 ended with exit 130 and CANCELLED_MUST_NOT_RUN never entered command history. Local Tab completion restores LOCAL_ONLY_SSH_AUDIT.txt after SSH exits (17-local-completion-restored).

Duplicate-prompt investigation: clean zsh composer success and failure each initially show one prompt (../zsh-compare/02 and 03). After Ctrl+H switches to native mode, the prior no-provider inline message disappears and the prior prompt is duplicated; the subsequent native failure itself ends with one prompt (../zsh-compare/04). This narrows the reproduction to the inline-message/native-control transition rather than zsh end-of-command output generally. Build6's main zsh failure shot itself has a single prompt.

Same-machine localhost and 127.0.0.1 are distinct SSH destinations and nested authenticated sessions, not independent machines. The control-socket-unavailable experiment renames only this isolated run's socket temporarily, restores it afterward, and does not simulate network packet loss. A delayed lookup shim was not attempted. GUI protocol stub makes no paid model calls. Other prior limits remain.

Preliminary verdict for parent: three original defects fixed; #S7GX handoff pass; expanded queue/cancel/interruption and absent-socket checks pass. Remaining assessment: interactive PATH additions are not reflected in command completion, and Ctrl+H after a failure message can duplicate the prior zsh prompt. Cards unchanged pending parent implementation landing and coordination.
