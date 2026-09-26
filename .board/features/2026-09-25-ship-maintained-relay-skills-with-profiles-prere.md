---
id: 4EMF
type: work
status: planned
labels: [feature, skills, worker, qa]
parent: SZ1H
blocked_by: [K26R]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: none, criteria: 'tests/test_bundled_skills.py passes without a model: every bundled skill has a valid profile, requires, a Try-it case, and names only tools Relay provides; one live Try-it run per skill is recorded in cases.jsonl.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [1QKM, MSJ0, 95VZ], github: null}
---
# Ship maintained Relay skills with profiles, prerequisites, and cases

## Issue
Give bundled Relay and workspace skills profiles and requirements, add a Relay skill creator, and provide Try-it cases plus a release check for every bundled skill. Keep the bundle focused on Relay capabilities.

## Done means
Every bundled skill has a profile and requirements, a runnable Try-it case and a release check. A bundled Relay skill creator produces that same structure. The bundle contains only skills whose promised tools Relay provides.

## Plan
Refreshed 2026-09-26 against `053f4458`. **Blocked by #K26R** (the `requires` schema and availability check); no open product decisions.

**Findings.**
- The bundle is now nine skills, all about operating Relay: `backend/relay_core/skills_bundled/{deliver, disk-hygiene, guest-account-setup, local-model-setup, mcp-servers, storage-cleanup}` and the plugin skills `plugins_bundled/{python/python-workspace, stata/stata-workspace, tex/tex-workspace}`. Only `deliver` has a `profile:`; none has `requires` or a case. All name tools Relay provides, so nothing leaves the bundle; the release check makes that stay true.
- Try it today is card-scoped (`tryit_protocol.py`, protocol 31.10: `try_run {card}` stages a situation in one bounded agent turn, with a sealed `expected.md`). Served cases go to `cases.jsonl` (#95VZ) with `server_version` = SKILL.md hash.
- The user's own `skill-creator` / `create-skill` would shadow a bundled skill of the same name (bundled is searched last), so the Relay one needs its own name.

**Steps.**
1. Profiles and requirements for all nine: a `profile:` per #MSJ0 and a `requires:` per #K26R (for example `local-model-setup` → optional programs `llama-server`/`ollama`/`vllm`; the plugin skills → their plugin's tool group).
2. A Try-it case beside each SKILL.md: `cases/try-it.md` holding the staged situation (a fixture folder or config under a temp `XDG_CONFIG_HOME`, never the user's real state), the prompt, and a sealed `expected.md`. Skills with side effects (`local-model-setup`, `guest-account-setup`, `storage-cleanup`) stage a dry-run fixture: no install, no login, no deletion outside the fixture.
3. A runner that serves one Try-it case: an agent turn with the skill loaded, in the fixture, writing a `cases.jsonl` row (`signal.mode: tryit`) with the verdict from the sealed expectation. Reachable as a worker call (the skill page button is #H7NF / #9FX8 GUI work and can follow). Run it once per bundled skill and link the rows as evidence.
4. `relay-skill-creator`, a bundled skill that writes a new skill with `name`, `short`, `description`, `profile`, `requires` and `cases/try-it.md`, into `.relay/skills` or `~/.config/relay/skills` (#HS7V). Its own Try-it case creates a skill in a fixture and runs the release check on it.
5. `tests/test_bundled_skills.py`, run without a model: every bundled skill parses, has a valid profile and requires with no warnings, has `cases/try-it.md` with a sealed expectation, and every tool, group or MCP server it names exists in `tool_groups.GROUPS` or the bundled plugins. Add it to the CTest/pytest list the queue gate runs.
6. Docs: `docs/ARCHITECTURE.md` § Skills (what a bundled skill must carry) and the skill-authoring notes in `docs/skills-examples`.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_bundled_skills tests.test_skills -v`; nine live Try-it rows in `cases.jsonl` linked under Evidence; the creator's fixture skill passes the same release check.
