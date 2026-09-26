---
id: C0Q8
type: work
status: executing
labels: [feature, plugins, workspace, agent-tools]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:ashe-ethz-ch
session: dc713c52-45bc-4954-ba65-72ecb17516de
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [ai-text], human: optional, criteria: A project plugin stays inert until enabled and workspace tools appear only while active., sign_off: none, effort: medium}
source: Owner in a Relay pane, 2026-09-23; implementation slice from reports/Plugin ecosystems for Relay.md
links: {plans: [], commits: [ff61a8387704, 13909113122c, cc0ff1a44b42], evidence: [], related: [MEPR, XHXX, HS7V, E85D], github: null}
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
**Goal.** Give Relay task workspaces a small, safe package contract. The implementation is delivered; the hermetic test repair is committed as `cc0ff1a4` and queued for publication. Independent verification follows its landing.

**State (audit 2026-09-26).** Checked against each Done means line:
1. *Schema and validation.* Landed in `ff61a838` (v1) and `b51072ff` (v2, #6FDD). `task_plugins.py:50-51` supports versions 1 and 2 and rejects others (`:476`). Unsafe paths (`..`, absolute, outside the package) get actionable errors (`:394-440`). Manifests are JSON, not the `plugin.yaml` #MEPR sketched, because Python 3.10's standard library reads neither YAML nor TOML. That choice was adopted by the implementer and never confirmed.
2. *Discovery and enablement.* The bundled, global (`$XDG_CONFIG_HOME/relay/plugins`) and project `.relay/plugins` origins are discovered with shadowing. Enable state lives outside the repo and is keyed to the package digest (`:984-1136`). A project package runs only after an explicit enable. The CLI is `python3 -m relay_core.task_plugins list|describe|validate|enable|disable|select` (`:1407-1424`). The GUI mirrors the rule read-only (`src/ArtifactContext.cpp:107-120`), but it has no screen to list or enable plugins.
3. *Selection and routing.* One tab picks a kind explicitly with the Ctrl+Alt+E chooser (#83YV `dd484d8a`) or from a file (`b70c33bf` and `4a5a4267`, #PBZ4) or foreground-program rules (`match_activation`, `:1174`). Forced shell/agent and the visible PROGRAM chip came from #S976 (`3d3b9587`, `473a4cfd`). `workspace_deactivate` restores Bash routing (`test_deactivation_restores_bash_routing`).
4. *Tools.* The worker side landed in `13909113`, which is the landing commit. The Execution Summary's `503933ca` does not exist in history. It gives lazy, namespaced `py_*`/`tex_*` tools, visible only while a workspace is active, with native/guest parity (`tests/test_workspace_plugins.py` NativeAgentTests and GuestBridgeTests).
5. *Two plugins, one API.* `relay.tex` and `relay.python` run on the same `WorkspaceManager` (TexTests and KernelTests). The second implementations reuse the contract without special pane wiring: `relay.shell`, `relay.stata` and `relay.markdown` (#6FDD); the Python console (#83YV `cf38b781`); and plugin `/` commands on file editors (#PBZ4 `b70c33bf`).

Loose ends found:
- `links.commits` is empty. The landing commits are `ff61a838` and `13909113`.
- There is no `verify` block.
- The three managed-kernel-dependent failures were fixed in `cc0ff1a4`; `python3 -m pytest -q tests/test_workspace_plugins.py` now passes all 25 cases. Publication receipt is pending.

**Split.** Out of scope here:
- #6FDD owns manifest v2 (console, completion, commands).
- #7WGJ owns the install-viewer flow.
- #9M96 owns MCP servers in Options.
- Third-party packages beyond project-local folders are #MEPR decision 5.

A GUI plugin list with enable/disable is not in this card's Done means (the CLI satisfies it). If the owner wants that screen, it gets its own card next to #7WGJ.

**Steps.**
1. Confirm publication of `cc0ff1a4`; the focused test command already passed in its workspace.
2. Propose the `verify` block: `{artifact: code, primary: script, also: [ai-text], human: optional, criteria: "a project package is listed but inert until enabled, and py_*/tex_* tools appear only while a workspace is active", effort: medium}`.
3. Hand the card to a separate verifying session through `needs-verification`, citing `13909113` and `ff61a838`.

**Risks.** A manifest command must never become an execution path from a cloned repo. The digest-keyed enablement covers this, and a verifier should try a changed package after it is enabled. `task_plugins.py` is shared with #6FDD.

**Verify.** Run `python3 -m pytest -q tests/test_workspace_plugins.py` in the published tree; it passed 25/25 in the submitting workspace. As a manual probe, enable, change and re-check a project `.relay/plugins` package with the CLI.

## Tasks

- [x] Specify manifest schema, owned directories and validation (ff61a838; v2 b51072ff) <!-- t:6r -->
- [x] Implement discover/enable/activate/deactivate lifecycle (ff61a838; worker 13909113) <!-- t:4v -->
- [x] Add workspace-scoped router, runner and preview role interfaces (13909113; GUI roles #E85D 4ad5fe87; foreground_program #S976 3d3b9587) <!-- t:4h -->
- [x] Add lazy namespaced tool registry with native/guest parity (13909113) <!-- t:9a -->
- [x] Prove the API with document and kernel plugin fixtures (13909113 TexTests/KernelTests; reused by #83YV cf38b781, #PBZ4 b70c33bf) <!-- t:e9 -->
- [x] Make test_workspace_plugins hermetic against a managed kernel venv (`cc0ff1a4`, 25/25 passed; publication queued) <!-- t:h7 -->
- [ ] Record links.commits and a verify block; hand to a verifier via needs-verification <!-- t:v8 blocked_by=h7 -->

## Execution Summary
Landed 2026-09-25 after the owner lifted the hold: `13909113` is 9825875c's content, re-applied onto the tip (a three-way merge reproduced the working copy byte for byte). It covers workspace_plugins.py (WorkspaceManager, Python KernelRuntime, TeX runtime, route through lang_router), workspace-scoped lazy py_*/tex_* tools with native and guest parity (agent.py, tools.py, tool_groups.py, guest_board_bridge.py, worker.py), protocol 36 in docs/AGENT-SESSIONS-PROTOCOL.md, and the #6FDD v2 remainder. #E34S's in-progress hunks in agent.py (one) and worker.py (four) were left out and stay in the tree for that session.

2026-09-26: Hermetic test fix `cc0ff1a4` isolates managed-kernel discovery; 25 workspace-plugin tests passed. Submitted as queue job `ada5afd3de683b49`; publication receipt pending.

## Tests
- tests/test_workspace_plugins.py::RoutingTests::test_python_workspace_routes_code_questions_and_forced_prefixes
- tests/test_workspace_plugins.py::KernelTests::test_deactivate_and_shutdown_close_the_kernel
- tests/test_workspace_plugins.py::WorkerTests::test_worker_activates_routes_and_runs_a_kernel_line
- Manual run: `python3 -m pytest -q tests/test_workspace_plugins.py` — 25 passed in 6.86s.
