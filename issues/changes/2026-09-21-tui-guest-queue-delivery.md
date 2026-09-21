---
id: TG7Q
type: work
status: needs-verification
labels: [bug, guest, terminal]
assignee: codex-tui-a2
rank: m
created: '2026-09-21'
source: 'Relay delegated TUI task, 2026-09-21'
links: {plans: [], commits: [e2db1d06050a20bc7609a8ad60f3bd92fcfc1a7d], evidence: [docs/qa_evidence/2026-09-21-tui-guest-queue/README.md], related: [QG4C], github: null}
---
# Serialize TUI guest prompts independently of the launch command

## Issue
fix the TUI and the harness issues in separate relay subagents

## Plan
**Goal:** Fix the two TUI queue defects documented in #QG4C; harness work stays with its separate agent.

**Findings:** `src/Pane.h` removes guest entries before asynchronous busy signals arrive, and its global active-command guard blocks prompts for guests launched by queued shell commands.

**Steps:**
1. Latch successful guest delivery until guest progress; reset on guest departure and preserve failed entries.
2. Gate guest queue heads on the guest resource independently of the shell launch command.
3. Add targeted delivery-state regressions, build, and leave live acceptance to the owner.

**Risks:** Busy signals are asynchronous; an idle snapshot alone must not acknowledge delivery. Existing uncommitted Pane edits belong to other sessions.

**Verify:** Targeted `queuesubmit` tests, Relay build, owner live Claude/Codex TUI queue acceptance.

## Execution Summary
Added a delivery reservation before PTY writes; only busy progress releases the reservation to
the existing busy gate. Duplicate idle/completion reports cannot drain the queue. Guest exit
resets the reservation, rejected writes retain their entry, and guest queue heads no longer
wait for the shell command that launched the guest. Harness Python files were not changed.

## Tests
`ctest -R ^queuesubmit$`

## QA checklist
- [ ] Restart rebuilt Relay and rapidly submit distinct Claude/Codex TUI prompts; verify sequential delivery.
- [ ] Queue a guest launch behind shell work, then guest prompts; confirm launch lifetime does not block prompts.
- [ ] Check paused/selected head, guest departure, and local slash/menu interactions.
- [ ] Owner performs live acceptance; no paid guest turns were used by the implementer.
