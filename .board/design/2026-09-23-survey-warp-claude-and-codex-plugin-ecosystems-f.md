---
id: XHXX
type: work
status: needs-verification
labels: [feature, design, plugins]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: e0e12343-9768-4af9-a4ee-96b00ae19807
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23
links: {plans: [], commits: [02c408cd916ca6d05de9d9a0a2dc8fb49af3c269], evidence: [reports/Plugin ecosystems for Relay.md], related: [MEPR, P2W8], github: null}
---
# Survey Warp, Claude, and Codex plugin ecosystems for Relay

## Issue
deploy a search on what plugins are used in warp / claude / codex, to see whta we will need

## Done means
- A source-linked survey distinguishes observed extension formats from actual adoption; it covers Warp, Claude Code/desktop, and Codex without presenting catalog listings as usage statistics.
- A local inventory distinguishes installed from merely available and publishes only aggregate categories in this public repository; no credentials or personal configuration details enter the report.
- A prioritized Relay capability map says what to build, reuse, or defer for document and statistical workspaces, with dependencies and open design choices.

## Plan
**Goal.** Identify the extension capabilities Relay should support for task plugins.

**Findings.** #MEPR proposes task workspaces; Relay already has skills and a guest tool bridge (`docs/GUEST-TOOLS.md`). Official vendor catalogs and the local installed configuration need separate assessment.

**Steps.** 1. Survey current official format and catalog examples for Warp, Claude, and Codex. 2. Inventory this machine's installed skill/plugin names and MCP server names without reading credentials. 3. Inspect Relay's current extension boundaries. 4. Write a source-linked report and a prioritized capability map.

**Risks.** A marketplace listing does not prove popularity or installation. Product capabilities and packages change; date the findings.

**Verify.** Check all product claims against opened official pages, validate local counts with a read-only inventory, and compare proposed gaps against the code/docs in this checkout.

## Execution Summary
The dated [Plugin ecosystems for Relay](../../reports/Plugin%20ecosystems%20for%20Relay.md) report compares official Warp, Claude and Codex extension models and catalog examples, distinguishes available from installed, checks this checkout's skills/guest bridge/tool groups, and recommends a staged Relay capability map. The key finding is to reuse SKILL.md and MCP for agent capabilities while defining a separate Relay workspace contract for editor, runner/kernel, panes and previews. No product code changed.

## Tests
- `python3 scripts/relay-board.py check` — no warning for #XHXX.
- Manual evidence: [source-linked report](../../reports/Plugin%20ecosystems%20for%20Relay.md), including the vendor pages and dated marketplace count; local report links were checked (23 total, no missing local target).
