---
id: SHPA
type: work
status: needs-verification
labels: [bug, ssh, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-22'
source: Owner in Relay, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-ssh-parity/], related: [S5SH], github: null}
---
# Audit SSH terminal parity and live UX

## Issue
look at how relay deals with ssh. i am noticing the line breaks before and after commands arent there. that indicates to me that some of the main features / functionality isnt working in ssh. 

i also dont want it to be showing cyan "relaying - ssh..." the whole time. there should be a clear indicator for ssh, but it shouldnt be the general shell command indicator. 

first analyze the code and find bugs and issues to reach parity with the non-ssh terminal. then live drive it to check for any UX issues

## Done means
- Explain the missing command spacing and persistent SSH busy indicator with code evidence.
- Compare remote command lifecycle, input handling, history, cwd and host identity with local behavior.
- Live-drive a real SSH login in isolated Relay, save evidence, distinguish reproduced bugs from code-only risks, and propose prioritized parity work.

## Plan
**Goal:** Audit before implementation; deliver measured gaps and a repair order.
**Findings:** Local shell hooks live in shell/integration.bash; remote hooks in shell/remote-integration.sh; src/Pane.h has a separate RemoteLogin lifecycle.
**Steps:** 1. Trace local and SSH command paths. 2. Drive local and localhost SSH commands, idle, busy, failure, cwd, and exit in an isolated GUI. 3. Record evidence and prioritized fixes.
**Risks:** Existing build may lag source; record both. Preserve user settings and sessions. Do not alter remote startup files.
**Verify:** Real localhost SSH plus GUI screenshots and focused SSH tests; no provider calls required.

## Execution Summary
Completed code audit and two real localhost SSH GUI drives in isolated Xvfb. Confirmed missing spacing, persistent idle SSH activity, local-only Tab completion inside SSH, and ambiguous cwd/input behavior. Documented code-only lifecycle, attachments, nested-host and capability risks separately. Report and repair order: docs/qa_evidence/2026-09-22-ssh-parity/README.md. No production changes; this card delivers the requested analysis and live UX audit.

## Tests
- manual: docs/qa_evidence/2026-09-22-ssh-parity/README.md
- manual: docs/qa_evidence/2026-09-22-ssh-parity/03-remote-command.png
- manual: docs/qa_evidence/2026-09-22-ssh-parity/04b-local-completion-in-ssh.png
- manual: docs/qa_evidence/2026-09-22-ssh-parity/07-input-consumed.png

Report records all targeted invocations and outcomes: 25 shell, 46 backend, 3 C++ tests passed. Full board format check has pre-existing errors elsewhere (12 errors, 754 warnings); no finding names SHPA. Production bugs remain open for implementation; this audit does not claim SSH parity.
