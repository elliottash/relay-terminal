---
id: C0Q8
type: work
status: planned
labels: [feature, plugins, workspace, agent-tools]
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Plugin ecosystems for Relay.md
links: {plans: [], commits: [], evidence: [], related: [MEPR, XHXX, HS7V, E85D], github: null}
---
# Define Relay task-plugin manifests, activation and tool contracts

## Issue
then lets go to your reports and write detailed cards for the editable artifacts and plugins features

## Discussion points
**Scope from #MEPR and the plugin ecosystem report.** A task plugin changes a Relay workspace's router, runner, agent tools/skills and pane roles. It is distinct from a reusable MCP integration. Built-ins should use the same versioned contract as future third-party packages, but Relay must not treat a Claude/Codex plugin manifest as executable Relay code. Relay-owned project configuration belongs under `.relay/` and global configuration under the existing XDG Relay directory (#HS7V); other tools' skills and instruction files are compatible sources. The first consumers are document (#MEPR) and Python/Stata workspaces; their second implementation should show whether the contract is genuinely reusable.

## Done means
- A versioned manifest schema declares plugin id/version, activation rules, composer language/router, runner lifecycle, tool/skill dependencies, pane roles/layout, preview adapter and required local programs; validation rejects unknown versions and unsafe paths with actionable errors.
- Built-in and project-local `.relay/plugins` packages are listed with origin, enablement and missing dependencies. Merely opening a cloned project does not launch its command, kernel or MCP server; activation requires a deliberate enable action for executable project content.
- One tab can select a workspace kind explicitly or from supported file/foreground-program rules; Bash/agent forcing remains available, and router choice is visible before execution. Deactivation restores ordinary Relay routing without losing the terminal.
- Plugin tools use a namespaced, lazy capability registry; native and guest agents see only tools granted to the active workspace, with execution and output ownership tied to that workspace.
- A minimal built-in document plugin and kernel plugin can use the same schema and API without special-case pane wiring; manifest, activation and permission tests prove it.

## Plan
**Goal.** Give Relay task workspaces a small, safe package contract.

**Findings.** `router.classify` currently assumes Bash; `tool_groups.GROUPS` is fixed; `guest_board_bridge.py` bridges existing tools outward, while no native general MCP client exists. #E85D supplies pane-group and preview roles.

**Steps.** 1. Specify and validate a versioned manifest under Relay-owned paths, plus equivalent bundled manifests. 2. Define activation/deactivation and dependency status without automatic execution from a cloned project. 3. Make router and runner interfaces workspace-scoped while preserving forced shell/agent paths. 4. Register namespaced, lazy tool capabilities with native/guest parity. 5. Exercise the contract with document and kernel fixtures, then connect the two real plugins.

**Risks.** A manifest that merely lists a command can become an unintended code-execution path; separate discovery from enablement and launch. Plugin tools must not leak credentials or kernel state across workspaces. Do not promise arbitrary external plugin compatibility; import supported skills and MCP definitions through explicit adapters.

**Verify.** Schema and path-validation tests, activation tests, router regression tests, native/guest tool discovery tests, and a second-plugin integration review.

## Tasks

- [ ] Specify manifest schema, owned directories and validation <!-- t:6r -->
- [ ] Implement discover/enable/activate/deactivate lifecycle <!-- t:4v blocked_by=6r -->
- [ ] Add workspace-scoped router, runner and preview role interfaces <!-- t:4h blocked_by=6r,4v -->
- [ ] Add lazy namespaced tool registry with native/guest parity <!-- t:9a blocked_by=4v -->
- [ ] Prove the API with document and kernel plugin fixtures <!-- t:e9 blocked_by=4h,9a -->
