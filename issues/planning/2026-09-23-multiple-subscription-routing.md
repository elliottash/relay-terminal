---
id: M8S2
type: work
status: planned
labels: [feature, models, routing]
assignee: codex
rank: m
created: '2026-09-23'
source: Owner in a Relay guest session, 2026-09-23
links: {plans: [reports/Multiple subscription routing in Relay.md], commits: [], evidence: [reports/Multiple subscription routing in Relay.md], related: [RND7, XH4K], github: null}
---
# Scope simultaneous subscription accounts in Relay

## Issue
scope out running multiple simultaneous subscriptions. look on reddit to see how people do it with claude code. and see if we can use the pipeline for the multiple accounts tracker already, will that generalize to other users

## Done means
The scope identifies a practical way to keep multiple Claude Code and Codex subscriptions logged in simultaneously, where Relay must carry account identity through model selection and launch, and how usage polling can be made user configurable. It distinguishes supported CLI behavior from community workarounds and names the implementation risks and verification steps.

## Plan
**Goal.** Let a user register and run several subscriptions at once, with each model choice tied to the correct account and quota.

**Design.** Give every subscription a stable account ID. Claude Code and Codex accounts launch with separate `CLAUDE_CONFIG_DIR` or `CODEX_HOME`; Z.AI and Kimi Code accounts use distinct preset IDs and keyring entries. Keep credentials in the CLI or keyring.

**Steps.** 1. Add account registration, display names, and account-aware model rows while preserving existing default IDs. 2. Carry the account ID through tier and job picks, process launch, planning, subagents, session persistence, resume, and failover. 3. Key usage and reset windows by account, poll each configured plan key, and use fresh quota data to weight draws only among tied ranks. 4. Add isolated launch, routing, quota, migration, and two-account live checks across supported platforms.

**Risks.** A reused harness or restored session could silently run on another login; credential-store behavior varies by platform; idle Claude/Codex quota endpoints are undocumented. Keep unknown or stale usage neutral in selection.

**Full scoping report.** [Multiple subscription routing in Relay](../../reports/Multiple%20subscription%20routing%20in%20Relay.md). The report includes Reddit evidence, official documentation, code paths, tracker portability analysis, and the test matrix.
