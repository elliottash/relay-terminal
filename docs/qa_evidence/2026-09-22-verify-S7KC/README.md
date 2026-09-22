# Independent SSH GUI verification — 2026-09-22

Verifier: Relay child a2 / Codex. Implementation untouched. No card status changes or commits; awaiting implementing parent's coordination. The harness exposed no relay_board tools, so live agent_message was unavailable; this file records early findings for the parent.

Ran the copied `drive.py` and `handoff.py` after `/tmp/ssh-build5.log` ended with `relay-build: built` (60 seconds; build start 11:02:37). Full build log saved as `build-marker.txt`. The scripts already derive OUT from `__file__` and ROOT from `parents[3]`. These drives exercised the shared working-tree build, including the implementer's then-uncommitted Pane/shell changes, not a clean committed export.

## Verdict

**#S7KC: fails full acceptance.** Ordinary command lifecycle, spacing, activity, multiline input and queue/stdin separation pass. Wrapped highlight and zsh capture defects remain below.

**#S7CX: scoped checks pass; full acceptance is not established by this run.** Remote path completion, host-labelled picker, cwd changes, nested connection capability withholding and local restoration pass. Two genuinely distinct machines and delayed/disconnected lookup were not exercised here.

**#S7GX GUI handoff portion: passes.** Real remote Bash command displays in the terminal, returns clean stdout and exit 1 to the worker before SSH exits. This supplements a1's guest/MCP verification; it is not paid-model CLI integration coverage.

## Findings needing implementation review

1. **Wrapped command highlight is incomplete**: `07c-wrapped-command.png` shows cyan on the first physical command row only; its second physical row of `x` characters remains unhighlighted. The command is `printf "WRAP-%s\\n" ` followed by 180 x characters, in a 1200×900 window. Explicit multiline input in `07c2-multiline.png` correctly highlights both logical rows. This directly fails #S7KC's wrapped highlighting criterion.
2. **Zsh history capture includes a shell marker**: `events.jsonl` records output `"ZSH-OUTPUT\n%"` for `printf 'ZSH-OUTPUT\\n'; false`, although `10-zsh-command.png` displays only ZSH-OUTPUT followed by the normal prompt. Exit 1 is correct. Bash ordinary, multiline and handoff captures are clean. `verify-events.py` records `zsh_clean_output: false`.
3. **Queue running row names SSH transport**: `05b-queued.png` and `07-command-queued.png` show the outer `ssh -t localhost ...` as the running queue item while the cyan busy line correctly names `sleep 7` or `read ...`. This is misleading per-command UI; dispatch itself works.

## Checked evidence

All 21 PNG screenshots were opened and visually inspected by this verifier, in addition to reading JSONL and OCR.

- `01-local`, `02-ssh-idle`, `03-remote-command`: local/remote command spacing match; ordinary command rows highlighted; persistent localhost badge and remote cwd chip; idle SSH has no cyan general activity.
- `04-remote-failure-cwd`: remote `/tmp` cwd chip, output /tmp, exit-1 toast. The history entry retains the command's starting cwd `localhost:/home/elliott` and exit 1.
- `04b-local-completion-in-ssh`: local-only `LOCAL_ONLY_SSH_AU` remains uncompleted while remote cwd is /tmp. `04c-remote-completion`: `REMOTE_ON` expands to `REMOTE_ONLY_PROOF.txt` in remote fixture cwd. `04d-remote-picker`: `inspect @coll` offers `localhost:collision.txt` despite local and remote collision filenames.
- `05-remote-busy`, `05b-queued`, `06-remote-idle-again`: cyan activity names sleep; second command visibly queued with remove affordance; dispatch after seven-second sleep; output QUEUED-AFTER-SLEEP; activity clears.
- `07-command-queued`, `07b-after-answer`: command queued during read. Ctrl+H enters native control; ANSWER becomes stdin; Ctrl+Shift+H returns control. Captured output INPUT> ANSWER / RECEIVED=ANSWER, then separate SHOULD-BE-A-COMMAND execution/history. No command was silently consumed as an answer.
- `07c2-multiline`: both logical rows highlighted; one history entry with command text containing a newline and output MULTI-A / MULTI-B.
- `07d-nested-ssh`, `07e-nested-return`: real nested SSH to 127.0.0.1; JSONL lines 129–134 withhold control_path and reachable; after nested command completion line 135, lines 136/138 restore capability on original shell return. The visible badge remains localhost; no inner-host rebinding is promised.
- `08-local-again`, `09-zsh-idle`, `10-zsh-command`, `11-local-return`: exit clears remote context and restores local cwd and badge; new zsh SSH connection works, idle activity clears, command renders normally and exit 1 is reported; second exit again restores local context.
- `12-terminal-handoff`: visibly executes `printf 'HANDOFF_STDOUT\n'; false` (actual newline inside quoted argument), two highlighted command rows, ordinary spacing. `handoff-events.jsonl` contains the report with exact output block `HANDOFF_STDOUT` and exit 1 while remote_session remains reachable. It reports before later SSH exit.

## Validation

- `python3 docs/qa_evidence/2026-09-22-verify-S7KC/drive.py` — exit 0.
- `python3 docs/qa_evidence/2026-09-22-verify-S7KC/handoff.py` — exit 0.
- `python3 docs/qa_evidence/2026-09-22-verify-S7KC/verify-events.py` — 11 checks true, zsh clean-output check false; results in `event-checks.json`. Script intentionally writes all verdicts without stopping at the known defect.
- `PYTHONPATH=backend:. python3 -m unittest tests.test_ssh_shell -q` — 26 passed, 4.624 seconds.

## Limits

Real OpenSSH sessions used localhost and 127.0.0.1 aliases on the same machine, not two independent hosts. GUI worker is a minimal deterministic protocol stub, not a paid Codex/Claude CLI or production worker; no model calls. This run does not verify attachment bytes (a1 owns collision/content verification), remote command-name completion, suggestions, latency/disconnected lookup, queue cancellation, Ctrl+C, full-screen programs, tmux, disabled/partial integration, third-party OSC, custom user prompts, handoff cancellation, other platforms or mosh. History exposes command timestamp, not elapsed duration; this run verifies ordering and seven-second sleep separation, not all notification/timing internals. Exit-command captures include disconnect text and the restored local prompt. The copied scripts both place runtime logs in `runtime/`; the later handoff run overwrites its relay.log, while separate gui logs, events and screenshots retain each drive independently.

## Final-build rerun

Repeated both drives on 11H.03; see [final/README.md](final/README.md). All three earlier discrepancies persist. The final zsh screenshot additionally shows a duplicate prompt after failure. Bash handoff still passes. No cards moved or evidence committed.

## Build6 / 11H.04

All three original failures are fixed. Extended drive and separate zsh control comparison recorded in [build6/README.md](build6/README.md); it includes two narrower follow-up findings for parent assessment.

## Latest shell binding comparison

The staging-only zsh redraw widget was independently retested in [zsh-staged/README.md](zsh-staged/README.md). Ordinary submits show one prompt, but Ctrl+H after no-provider inline text still duplicates the preceding prompt. This narrower artifact is unresolved.

## Delayed completion

[delayed2/README.md](delayed2/README.md) passes live standard remote command completion plus delayed-result discard after draft edit and after SSH disconnect. The first shim attempt was invalid and is explicitly labelled in delayed/README.md.

## Final native-input verdict

2944d892 resolves the last native-control finding in an independent fresh drive using landed shell code: [zsh-final/README.md](zsh-final/README.md). All reproduced defects have verified fixes. Earlier failure notes remain as audit history.
