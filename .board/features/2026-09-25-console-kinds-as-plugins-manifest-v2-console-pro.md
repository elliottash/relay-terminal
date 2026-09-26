---
id: 6FDD
type: work
status: needs-verification
labels: [feature, plugins, worker]
assignee: codex
implemented_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: 'Owner in a Relay pane, 2026-09-25; slice of #P2W8 (proposal 2, 4)'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-manifest-v2/], related: [P2W8, C0Q8, 33G0, S976], github: null}
---
# Console kinds as plugins: manifest v2 (console program, completion, block marks, slash commands) and a bundled relay.shell

## Issue
a shell pane is the main version of a console pane. but you could have others, eg ipython. it looks like a shell pane, but it doesnt auto-detect shell commands, it auto-detects python commands. it could also have special tools like compile / run / etc. there would be a plug-in spec defining that.

## Plan
Slice 4 of #P2W8 (proposals 2 and 4: a console kind is a plugin; three separate surfaces for auto-detected input, slash actions and agent tools; `relay.shell` proves the contract).

**Goal.** Manifest schema version 2, additive over v1 (`backend/relay_core/task_plugins.py:97`, `:417`, committed in ff61a838), so a plugin can declare what a console pane runs, how its statements are completed and delimited, and which slash commands it offers; and a bundled `relay.shell` package that describes today's Bash pane in the same terms.

**Fields (v2).**
- `console`: `{program: [argv with {connection_file}, {workspace}, {file} placeholders], env: [...], prompt_marks: "osc133" | "none", startup: "path relative to the package" (an IPython startup file, a bashrc fragment)}`.
- `completion`: `{kind: "static" | "shell", table: "path to a JSON list of {text, description}"}`.
- `commands`: `[{name, description, args: [{name, required}], action: {kind: "tool" | "prompt" | "program_line", tool | prompt | line}, when: {roles: [...], languages: [...]}}]` — a visible slash action; never implies a tool grant.
- `router` gains `prose_fallback: "agent" | "program"` (what an unclassifiable line does; the shell says program, python says agent).
- `schema_version: 2` accepted alongside 1; unknown keys still refused; paths validated with the existing safe-path rules.

**Steps.**
1. Schema, validation and `Manifest` dataclass fields in `task_plugins.py`; `select_for` unchanged. Tests in `tests/test_task_plugins.py`: v1 still valid, v2 fields round-trip, bad `commands.action.kind`, path escape in `console.startup`.
2. Bundled `plugins_bundled/shell/plugin.json` (`relay.shell`: activation none, always the default; `console.program` the Bash the pane starts today with its integration script; `completion.kind: shell`; `prompt_marks: osc133`; no tools, no commands) and `plugins_bundled/shell/README.md` saying it is the reference package.
3. `python/plugin.json` → v2: `console.program: ["jupyter", "console", "--existing", "{connection_file}"]`, `console.startup: ipython_startup.py` (a `Prompts` subclass emitting OSC 133 A/B/C/D), `completion.table: completion.json` (keywords, builtins, `%` magics), `commands`: `/restart` → `py_restart`, `/vars` → `py_variables`, `/export` → `py_export`. `stata/plugin.json` and `tex/plugin.json` → v2 with the fields they have (tex: `/build` → `tex_build`, `/errors` → `tex_diagnostics`).
4. A `task_plugins` CLI subcommand `describe <id>` printing the v2 surfaces, for the GUI work to read.
5. `docs/TASK-PLUGINS.md`: a new section "Schema version 2" appended at the end (the rest of the doc has another session's uncommitted hunks; do not reflow it).

**Files.** `backend/relay_core/task_plugins.py`, `backend/relay_core/plugins_bundled/shell/*` (new), `.../python/{plugin.json,ipython_startup.py,completion.json}`, `.../stata/plugin.json`, `.../tex/plugin.json`, `tests/test_task_plugins.py`, `docs/TASK-PLUGINS.md` (append only). **Not** `workspace_plugins.py`, `worker.py`, `agent.py`, `tool_groups.py` (held). The python/tex `plugin.json` files carry small uncommitted hunks: claim with `land.py begin`, `--dry-run`, and keep those hunks.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_task_plugins`; `python3 -m relay_core.task_plugins validate` over the four bundled packages; `describe relay.python` shows the three surfaces.

## Tasks

- [x] Schema v2 fields, validation, dataclass, tests <!-- t:7h -->
- [x] Bundled relay.shell package <!-- t:b4 blocked_by=7h -->
- [ ] python, stata, tex manifests to v2 with startup, completion table and commands <!-- t:6k blocked_by=7h -->
- [ ] describe CLI and docs section <!-- t:mj blocked_by=b4,6k -->

## Done means

Schema v1 packages remain valid; schema v2 validates and round-trips console, completion, commands, and prose fallback. Four bundled packages validate, and `describe relay.python` prints the declared surfaces. Invalid action kinds and escaped package paths fail validation.
