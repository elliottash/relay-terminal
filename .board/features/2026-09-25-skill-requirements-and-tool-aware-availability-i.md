---
id: K26R
type: work
status: executing
labels: [feature, skills, worker]
assignee: agent
implemented_by: openai/gpt-6-sol via codex:elliott-t-ash-gmail-com
session: 6fa2d509-138c-4a64-8910-c6cb50031874
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'tests/test_skills.py covers requires parsing, the three availability states, exact-content identity and the new discovery rules; skills_list and skills_registry rows carry availability and aliases.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [95VZ, 9FX8, HS7V], github: null}
---
# Skill requirements and tool-aware availability in the catalog

## Issue
Add structured requires to skills, check tools and local prerequisites when indexing, and show why a skill is unavailable. Replace the Warp product-bundle discovery and fixed exclude list with source- and tool-aware rules; retain provenance and exact-content identity.

## Done means
Skill manifests accept declared program, environment, secret and tool requirements. The index distinguishes runnable, hidden-for-missing-tool, and visible-with-action-needed. Exact content duplicates have one identity with every source retained; same-name differing versions remain visible in provenance. Tests cover missing tools, missing local prerequisites and source discovery.

## Plan
Refreshed 2026-09-26 against the code at `053f4458`. No open product decisions: the rules below are the ones the owner approved on #SZ1H (report §4 Q2/Q3).

**Findings.**
- `backend/relay_core/skills.py`: `SkillIndex.load` (≈209–271) keeps the first skill per folder name; `default_directories` (510–540) searches `.relay/skills`, `~/.config/relay/skills`, the workspace and home `.agents/.claude/.codex/.warp` trees, then recursive bases under `~/.warp` and `~/.claude` (depth 6, which is what pulls in Warp's `remote-server/bundled_resources`), imported skills, and the bundled dir last. `DEFAULT_EXCLUDE` (449–451) is seven Warp names. Frontmatter parses `name`, `description`, `short`, `profile` (#MSJ0); no `requires`.
- `cases.skill_version` (`cases.py:104`) already hashes the whole SKILL.md: reuse it as the content identity.
- The task-plugin `Requirement` schema and `dependency_status` (`task_plugins.py:194–204, 528–559, 919–937`, `docs/TASK-PLUGINS.md:105`) cover programs; env, secret and tool kinds do not exist yet.
- Tool inventory: native groups in `tool_groups.GROUPS`; MCP servers from `mcp_config` (enabled/trusted config), whose tool lists arrive asynchronously (`mcp_tools.py`). There is no single index-time inventory.
- Rows reach the model via the catalogue and the GUI via `skill_manage.list_skills` / `skills_registry` (`observe_protocol.py:137–158`).

**Steps.**
1. `requires:` block in SKILL.md, in the task-plugin shape: `program` (with `alternatives`, `version_arg`, `optional`, `install_hint`), `env`, `secret` (keystore name), `tool` (a native group, or an MCP server as `mcp:<server>`). Factor the program parser/checker out of `task_plugins.py` so both use one function; bad entries are warnings on the skill, as `profile:` does.
2. Availability per skill: `runnable`; `hidden` when a required tool is absent (native group missing, or MCP server not configured); `needs` with reasons when a program, env var or secret is missing, or an MCP server is configured but disabled or untrusted. MCP is judged from configuration, never from the async tool list, so indexing stays synchronous and deterministic. Hidden skills leave the model catalogue but stay in `list_skills`/`skills_registry` with their reason.
3. Inference for skills that declare nothing: a skill under a Warp `mcp_skills/<server>/` tree requires `mcp:<server>`; a description naming `mcp__<server>__*` requires `mcp:<server>`. Declared `requires` always wins.
4. Discovery: stop descending into `~/.warp/remote-server/bundled_resources` (Warp's product bundle, like Claude's built-ins) and retire `DEFAULT_EXCLUDE`; a user's explicit disable list is unchanged.
5. Identity: skills whose SKILL.md hashes match collapse to one row with `aliases` listing every source path; same name with different content keeps today's first-wins rule and lists the others as shadowed versions with their hashes.
6. Row fields `availability {state, reasons[]}`, `aliases[]`, `content_hash` on `list_skills` and `skills_registry`. **GUI rendering belongs to #H7NF**, which is changing `src/SkillRegistryView.*` now; this card does not edit that file.
7. Protocol doc: new row fields in `docs/AGENT-SESSIONS-PROTOCOL.md`; `docs/ARCHITECTURE.md` § Skills gets the availability rule.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_skills -v` with new fixtures: missing program, missing env/secret, unconfigured and disabled MCP server, inferred Warp `mcp_skills` requirement, byte-identical skills from two trees collapsing with aliases, same-name different content, bundled_resources not discovered (the existing Warp-bundle and `warpctrl` expectations are replaced). Plus `tests.test_task_plugins` still green after the shared checker moves.
