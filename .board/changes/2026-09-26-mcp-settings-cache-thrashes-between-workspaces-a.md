---
id: WPYZ
type: work
status: executing
labels: [bug, performance, mcp]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: f393dabe-52e4-4940-ae6b-14ad9f25c7f8
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: code, primary: script, also: [metric], human: none, criteria: Two-workspace regression test passes and MCP refresh does not recur while files remain unchanged., sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Relay pane, 2026-09-26
links: {plans: [], commits: [ada6b6705a87], evidence: [], related: [BT7C, J0VY], github: null}
---
# MCP settings cache thrashes between workspaces and floods GUI catalogs

## Issue
The running Relay process repeatedly launches MCP configuration reads when two windows use different workspaces. A single global cache alternates between their workspace keys; each read notifies every window and fans out app catalogs to pane workers, keeping the GUI main thread busy.

> can you run profiling and see how to optimize or prallaleize
> — elliott · [session:a9db98ce062646d8acad3376b00cf347](relay://session/a9db98ce062646d8acad3376b00cf347) · 2026-09-26

## Done means
- Reading MCP settings for workspace A, then B, then A again does not launch a third configuration read when neither file changed.
- A changed MCP configuration still reloads its workspace and updates the visible rows.
- The targeted MCP settings test and Relay build pass.

## Plan
**Goal:** Stop MCP configuration reads and catalog fan-outs caused by switching between workspaces.

**Findings:** `src/McpSettings.cpp` keeps one cache; live profiling showed its async refresh calling `SettingsWatch::notify()` about once per second while settings rows alternated between `/home/elliott` and `/home/elliott/repos/relay-terminal`. The GUI log recorded 20,656 `app_catalog_updated` events in about 29 minutes.

**Steps:** 1. Keep MCP snapshots keyed by workspace and update refresh/invalidation to target the right key. 2. Add a two-workspace regression test with a changed-file check. 3. Build and run the targeted test, then submit the commit.

**Risk:** A global MCP file edit must refresh every workspace's entry on its next read; each entry's stamp includes its mtime.

**Verify:** `ctest --test-dir build -R '^mcpsettings$' --output-on-failure` and a successful `scripts/relay-build`.

## Profile
2026-09-26, live Relay PID 17756 (release 7aa4c689): GUI CPU about 58–65% of one core, 21 panes, ample RAM and CPU capacity. In 29 minutes `relay.log` recorded 20,656 `app_catalog_updated` events, clustered across pane workers about every 1.3 seconds. A 12-second `perf record` showed `sendAppCatalog`, `appCatalogForTab`, `AppCommands::catalog`, `modelCatalog`, and `catalogFrom` on GUI stacks. A six-second bpftrace uprobe on `SettingsWatch::notify()` found four calls from the MCP `runCli` completion callback and one from usage limits. GDB sampled `mcp::settingsRows` arguments alternating between `/home/elliott` and `/home/elliott/repos/relay-terminal`. `src/McpSettings.cpp` had one global cache slot keyed by its latest workspace, so these reads repeatedly displaced one another. The running release will retain the old behavior until it is updated and restarted.

## Tests
`ctest --test-dir build -R '^mcpsettings$' --output-on-failure` — passed, including two-workspace cache and changed-file regression.
`scripts/relay-build --target relay` — passed.

### Check
The two-workspace regression passed with only two configuration reads for A → B → A. Changing A's `.mcp.json` caused a third read while B stayed cached.

## Execution Summary
Committed `ada6b670` on the session workspace branch and submitted queue job `8c61ac2b8362e270`.

`src/McpSettings.cpp` now keeps one MCP snapshot per workspace. The async completion callback, file stamp check, and explicit invalidation all address the matching workspace. This removes the A → B → A cache eviction loop while preserving refreshes when a project's `.mcp.json` or the global MCP file changes.

The running Relay process is still release `7aa4c689`; it needs the published update and a restart before live GUI CPU can be measured after the fix.
