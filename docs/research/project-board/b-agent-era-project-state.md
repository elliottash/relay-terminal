# Per-project state in the agent era: agents, task trackers, run trackers

Research date: 2026-09-25. Sources are primary: official docs (fetched as HTML or via `.md` / `llms.txt` endpoints), GitHub READMEs, repo docs sources, and the Claude Code changelog. Anything not confirmed from a fetched page is marked *unverified*. This complements `docs/research/global-project-board/b-plaintext-and-git-native.md`; the 2025-12 passes (OpenHands microagents, early Codex/Claude/Cursor) were refreshed against today's docs.

One correction to the folklore in the brief: **Beads is no longer SQLite-backed.** It now uses Dolt, and `issues.jsonl` is an export, not the store (https://github.com/steveyegge/beads).

## Part 1 — Coding agents' project files

### Claude Code (Anthropic)

Object kinds: instructions (`CLAUDE.md`), auto-memories, skills (`SKILL.md` folders), hooks (shell commands), settings (JSON), session state.

- Memory doc: https://docs.claude.com/en/docs/claude-code/memory
- Skills doc: https://docs.claude.com/en/docs/claude-code/skills
- Hooks doc: https://docs.claude.com/en/docs/claude-code/hooks
- Settings doc: https://docs.claude.com/en/docs/claude-code/settings

`CLAUDE.md` hierarchy:

- Three scopes: Enterprise-managed, Project (`.claude/CLAUDE.md` or `CLAUDE.md` at root), User (`~/.claude/CLAUDE.md`).
- "closest to the working directory wins" when nesting per directory.
- `@import` lets a CLAUDE.md pull in extra files.
- Staleness is manual: the docs suggest asking Claude to update the file when it notices drift; `#`-prefixed inputs offer to append a note to CLAUDE.md.

Auto memory (the closest published analog to Relay's memory cards):

- "MEMORY.md index files reference individual memory files, one per topic."
- Each memory lives in `~/.claude/memories/<project-id>/memory-name.md` — per-project, keyed to the git repo.
- Files carry a frontmatter "modified" timestamp "for staleness checks."
- "Memory files are limited to 200 lines / 25KB"; the agent is told to prune stale entries.
- Escalation ladder from the changelog (https://github.com/anthropics/claude-code/blob/main/CHANGELOG.md): warn on truncation → "remind the agent to compact its MEMORY.md index when nearing the size limit" → "now raise an explicit error when writes would exceed limits."
- Memories are machine-local and not committed to git.

Skills:

- `.claude/skills/<name>/SKILL.md`, one skill per directory, and "skills can be nested within subdirectories of a project."
- Personal scope: `~/.claude/skills/`; plugin skills bundle with plugins.
- `synced` is a reserved top-level name: "Synced skills are downloaded from claude.ai."
- `/run-skill-generator` "records the run recipe as a per-project skill" — turning what worked into a reusable playbook.
- Best-practice docs: bundle scripts/references next to SKILL.md; keep one skill focused ("do one thing, do it well").

Settings and hooks:

- Four files + managed policy: user `~/.claude/settings.json`; shared project `.claude/settings.json` ("commit it so teammates get it"); project-local `.claude/settings.local.json` (auto-gitignored, "for you, in this one project only"); enterprise managed settings.
- Hooks live in settings and call scripts in `.claude/hooks/`, e.g. `${CLAUDE_PROJECT_DIR}/.claude/hooks/block-rm.sh`.
- Docs admonition: "Hooks run with your full user permissions. Never configure hooks that you have not reviewed."

Links between kinds: MEMORY.md → memory files; skills → sibling scripts/references; settings → hook scripts. No object ids; plain markdown paths throughout.

### OpenAI Codex

Docs live at learn.chatgpt.com (2026-09). Object kinds: `AGENTS.md` (global + per-repo), skills, plugins, memories, legacy custom prompts (deprecated in favor of skills).

- `AGENTS.md` precedence: "guidance defined in files closer to your working directory takes precedence" — `~/.codex/AGENTS.md` global, then repo `AGENTS.md`. https://learn.chatgpt.com/docs/customization/overview
- Format spec: https://agents.md/ — "AGENTS.md is a simple, open format for guiding coding agents... Think of it as a README for agents."
- Skills locations (https://learn.chatgpt.com/docs/build-skills.md):
  - `.agents/skills/<name>/SKILL.md` — REPO scope (committed, inherited by subdirectories).
  - `$HOME/.agents/skills` — USER scope.
  - `.codex/skills` — legacy compatibility location.
  - Frontmatter `name` + `description`; "Only skill metadata is visible to the agent. Skill bodies are read on demand."
  - Optional `.agents/skill-metadata.json` hides or shadow-overrides skills per repo.
- Plugins are the packaging unit: "a bundle of skills, agents, custom prompts, and templates" installed under `~/.codex/plugins` — a skill **server** with sidecars, which is exactly Relay's alias-card idea.
- Memories (https://learn.chatgpt.com/docs/customization/memories.md): opt-in; stored in `~/.codex/memories.jsonl` — "a JSONL file with entries for State, User, and Context memories", each with `created_at`; auto-deduplicated on write; `codex memory export` dumps markdown.
- Custom prompts documented as deprecated and folded into skills.

### Cursor

- Rules: `.cursor/rules/*.mdc`, one rule per file, YAML frontmatter (`description`, `globs`, `alwaysApply`). https://cursor.com/docs/rules.md
- "A plain `.md` file in `.cursor/rules` is ignored by the rules system because it has no frontmatter... If you prefer plain markdown, use AGENTS.md instead."
- Four rule types: Always, Auto-attached (by `globs`), Agent-requested (by `description` match), Manual (`@ruleName`).
- "Rules in nested directories are supported."
- Team rules: "Once team rules are created, they automatically apply to all team members and are visible in the dashboard" — dashboard-managed, pushed down from the app.
- `/create-rule` writes a new rule file for you; docs claim "Rules provide persistent, reusable context at the prompt level."
- Memories and Notepads: as of 2026-09 neither appears in Cursor's docs index (https://cursor.com/llms.txt); `https://cursor.com/docs/context/memories` redirects to `/docs/rules`; `https://cursor.com/docs/features/notepads` is 404. The 2025-era features are *unverified — apparently folded into Rules/AGENTS.md or removed*. That churn is itself a finding: agent "memory" features died; durable, versioned rule files survived.

### Devin (Cognition)

- Playbooks: "a collection of reusable prompts that tell Devin exactly how to perform a specific task" — saved in the web app as a team-shared library, organized by tags; not repo files. https://docs.devin.ai/product-guides/creating-playbooks.md
- Knowledge (memories) is deprecated: "In an upcoming release (late September 2026), Devin will replace memories with skills." https://docs.devin.ai/product-guides/knowledge.md
- Legacy Knowledge scopes were Organization / Team Space / Repository, with "repo-pinned" entries; the docs admit repo-pinned knowledge "could drift if the code changes."
- Skills (the replacement): repo-level `.agents/skills/<name>/SKILL.md` per the Agent Skills standard, so they "live in the repository where the work happens"; Devin web can suggest a skill from a good session and give you a Create PR button to commit it. https://docs.devin.ai/product-guides/skills.md
- DeepWiki: AI-maintained repo wiki; steering file `.devin/wiki.json` plus generated `wiki/` docs; `/refresh deepwiki` to sync; "Do not store API keys, tokens, or credentials in `.devin/wiki.json`" (it is committed). https://docs.devin.ai/work-with-devin/deepwiki.md

### OpenHands (OpenHands Agent Server)

- Microagents were renamed **Skills**; repo guidance now lives in a `.openhands/` directory — "used for all repo-level guidance — similar to a CLAUDE.md — plus optional setup script (`setup.sh`)." https://docs.openhands.dev/openhands/usage/customization/repository.md
- Skills: `.openhands/skills/<name>/SKILL.md` with frontmatter `name` and `triggers` (keyword list) or path globs; a skill activates when a trigger appears in the conversation or a matching path is opened. https://docs.openhands.dev/sdk/guides/skill.md
- AGENTS.md content is injected as a `<REPO_CONTEXT>` block; official default skills (e.g. condenser) ship with the runtime. https://docs.openhands.dev/sdk/guides/prompting.md
- Scope: repo `.openhands/` (committed) vs user-level skills configured in the app; staleness handling is just git review.

### GitHub Copilot

- Repo instructions: `.github/copilot-instructions.md` (repository-wide) plus path-specific `NAME.instructions.md` files under `.github/instructions/` with `applyTo:` glob frontmatter; when a path matches both, "the instructions from both files are used." https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/add-custom-instructions/add-repository-instructions
- Org-level instructions sit above repo instructions and can override; personal instructions are the other end of the scale. Repo instructions cap at 13,000 characters.
- Skills: `.github/skills/` (project), `.claude/skills/` and `.agents/skills/` also read, `~/.copilot/skills` personal; installable with `gh skill <install|search|create>`; Copilot participates in the **Agent Skills open standard** (https://agentskills.io/specification.md) alongside Claude Code and Codex. https://docs.github.com/en/copilot/concepts/agents/about-agent-skills
- Coding agent task tracking — the task object *is* the GitHub object:
  - Assign an issue → Copilot always creates a PR; prompt-started sessions work on a branch.
  - "Sessions do not create pull requests automatically."
  - "It does not see comments added after assignment, so post follow-up information on the pull request instead."
  - Progress and review happen as PR comments/timeline; research → plan → iterate happens in the session. https://docs.github.com/en/copilot/how-tos/copilot-on-github/use-copilot-agents/kick-off-a-task and .../research-plan-iterate
- No persisted plan file inside the repo is documented; any session file location is *unverified*.

### Windsurf → Devin Desktop (Cognition)

- Memories: auto-generated — "user preferences, project standards, organizational context, workflow discoveries" — with an editable list UI; stored locally (`~/.codeium/windsurf/memories`), not committed. https://docs.windsurf.com/windsurf/cascade/memories.md
- "Memories apply only to the legacy Cascade agent... Devin Local does not persist memories."
- Migration advice: "we recommend migrating important memories to Rules or AGENTS.md."
- Rules: human-written, versioned; `.devin/rules/*.md` with activation modes in frontmatter (Always On / Model Decision / Glob / Manual), workspace or global scope.
- AGENTS.md is processed by the same rules engine; admins can push system-wide `/etc/devin/rules/`.
- The Windsurf agent itself is deprecated (Nov 17, 2026, per the same page).

### Aider

- Conventions: a single markdown file (e.g. `CONVENTIONS.md`) loaded with `/read CONVENTIONS.md` or `aider --read CONVENTIONS.md`. https://aider.chat/docs/usage/conventions.html
- "This way it is marked as read-only, and cached if prompt caching is enabled."
- Aider maintains a community conventions repo of style guides to adopt per language/framework.
- One file, no versions, no links, no task/spec objects.

### Cline and Roo Code (the "memory bank" pattern)

Cline's Memory Bank is a documented methodology (not a built-in feature): https://docs.cline.bot/best-practices/memory-bank.md

- `memory-bank/` of six markdown files: `projectbrief.md` (foundation, source of truth), `productContext.md`, `systemPatterns.md`, `techContext.md`, `progress.md`, and `activeContext.md` which "updates most frequently" and depends on the others.
- Update triggers: start of session with "Review memory bank", during idle time, or the explicit command "update memory bank" — which "MUST review ALL files."
- Instructions are injected via a rules file (`.clinerules/memory-bank.md`); conditional rules can activate memory-bank instructions only when memory-bank files are touched.
- Staleness handling is entirely convention: the human decides when a full re-read-and-rewrite happens.

Roo Code generalizes the same pattern into rule directories: https://roocodeinc.github.io/Roo-Code/features/custom-instructions

- Global: `~/.roo/rules/` plus per-mode `~/.roo/rules-{modeSlug}/`.
- Workspace: `.roo/rules/` — "take precedence over global rules when they conflict"; `.roorules` single-file fallback; `.rooignore` for context exclusion.
- A Prompts tab manages mode prompts in the app.

### Amp (Sourcegraph)

- `AGENTS.md` lookup order: repo root → `$HOME/.config/amp/AGENTS.md` → `$HOME/.config/AGENTS.md`; closest-to-cwd wins.
- Threads are first-class objects: saved, shareable by URL, and referencable from other threads ("@T-7f395a45-…"); the agent "reads the referenced thread and extracts relevant content."
- The `handoff` command drafts a new thread with relevant files/context from the old one; Amp explicitly rejects compaction and says "Abandon threads if they accumulated too much noise."
- Skills install via `amp skill add anthropics/skills` (GitHub or local sources).
- Source: archived capture of https://ampcode.com/manual (web.archive.org, 2025). The live site is a JS-rendered docs site whose nav lists Projects / Threads / Skills / AGENTS.md pages; details beyond the archived text are *unverified*.

### Google Jules

- Reads `agents.md` or `readme.md` for environment setup hints. https://jules.google/docs/environment/
- Per-repo task history and settings live in the app's Repo view. https://jules.google/docs/repo/
- Plan review is a gate: Jules shows a plan before coding; you approve or revise. https://jules.google/docs/review-plan/
- "Continuous AI" positioning doc: https://jules.google/docs/guides/continuous-ai-overview
- No memory object documented in the fetched pages.

### Kiro and Google Antigravity

- *Unverified.* kiros.dev returned 404 for `/docs/` on 2026-09-25 (homepage repurposed); the Kiro specs pattern (`.kiro/specs/<feature>/{requirements.md, design.md, tasks.md}` plus `.kiro/steering/`) is widely cited but I could not confirm it from a live primary source today.
- Antigravity's docs are a JS-rendered SPA; no static content retrievable on this pass. Treat both as unconfirmed.

## Part 2 — Git-native task trackers agents use

### Beads (`bd`)

- Pitch: "a persistent, structured memory for coding agents. It replaces messy markdown plans with a dependency-aware graph." https://github.com/steveyegge/beads
- IDs are hash-based (`bd-a1b2`) to "prevent merge collisions in multi-agent/multi-branch workflows."
- Storage: Dolt database, two modes — **embedded** (default, `.beads/embeddeddolt/`, single writer) or **server** (external `dolt sql-server`, `.beads/dolt/`).
- Cross-machine sync is git-native: `bd dolt push` / `bd dolt pull` against `refs/dolt/data` on your git remote.
- `.beads/issues.jsonl` is "an export for viewers and interchange, **not the source of truth or a backup**"; `bd export --all` is the backup path.
- Links: `bd dep add <child> <parent>` for blocks/related/parent-child; graph link types `relates-to`, `duplicates`, `supersedes`, `replies-to` "for knowledge graphs"; auto-ready task detection for the agent.
- Guardrails worth copying:
  - A schema version guard refuses to open a DB migrated by a newer binary ("database is at v45, binary knows up to v42") instead of failing cryptically.
  - `bd init --contributor` routes planning issues to a separate repo (`~/.beads-planning`) so planning churn stays out of PRs.

### git-bug

- Bugs are "embedded in the git repository" as git objects; sync via `git bug push` / `git bug pull` over a normal git remote; "works offline." https://github.com/MichaelMure/git-bug
- Identities are first-class; bridges import/export GitHub/GitLab issues; terminal and web UIs are views over the same objects.
- Truth is git objects; nothing lives in an app database.

### Backlog.md

- "Everything is stored as human-readable Markdown in a project-local backlog folder such as `backlog/`, `.backlog/`, or a custom path" via `backlog.config.yml` (`backlog_directory:`). https://github.com/MrLesk/Backlog.md
- Task ids are prefixed sequential (`TASK-1`, configurable prefix); git is optional (`--no-git`).
- Ships a board CLI and web UI; `backlog instructions overview` is the agent entrypoint; search spans "tasks, docs & decisions."
- Agent workflow is explicit about **when plans are written**: "Ask the agent to research and write an implementation plan in the task. Do this right before implementation so the plan reflects the current state of the codebase."
- Three human review checkpoints: decompose → plan → verify.
- Milestones and dependencies: "task detail showing what a task waits on and what waits on it."

### Task Master (`task-master`)

- PRD-first: `.taskmaster/docs/prd.txt` plus templates in `.taskmaster/templates/`. https://github.com/eyaltoledano/claude-task-master
- Tasks carry dependencies and tags act as boards (`backlog` / `in-progress` / `done`); `task-master move --from-tag=backlog --to-tag=done --with-dependencies`.
- Complexity analysis and 36 MCP tools; JSON state under `.taskmaster/`.
- Exact `tasks.json` schema *unverified* this pass (README structure only).

## Part 3 — Artifact and run tracking in research workflows

### MLflow

- Run record: "a run records metadata (such as metrics, parameters, and start and end times) as well as artifacts (output files such as model weights, tables, or plots)." https://github.com/mlflow/mlflow/blob/master/docs/docs/classic-ml/tracking/index.mdx
- Runs group into Experiments; source tracking records "code versions" (git commit).
- Registry: registered models are versioned ("each new model added... increments the version"); **aliases** (`@champion`) are the modern promotion pointer; legacy "stages" are deprecated. https://github.com/mlflow/mlflow/blob/master/docs/docs/classic-ml/model-registry/index.mdx
- MLflow 3 makes **Logged Models** first-class: "MLflow links the model to the run that created it" — lineage runs model → producing run → params/metrics → artifacts. https://github.com/mlflow/mlflow/blob/master/docs/docs/classic-ml/mlflow-3/index.mdx
- The app is truth; the git commit is the link back to files.

### Weights & Biases Artifacts

- Artifacts are append-only versioned directories; **aliases** are named mutable pointers (e.g. `production`). https://docs.wandb.ai/support/models/tags/artifacts.md
- A run that `use_artifact`s an input and `log_artifact`s an output forms the lineage graph — the docs' FAQ framing is exactly Relay's question: "How can I find the artifacts logged or consumed by a run."
- Sweeps: "each run in a sweep will create a new version of the model artifact."
- References can point outside W&B (S3/GCS) with "versioning... ETags" for staleness detection.
- Documented trap: "Deleting a run in W&B permanently deletes all artifacts logged by that run."

### DVC

- `dvc.yaml` "define[s] stages, parameters, metrics, and plots. Stages form the pipeline(s) of a project." https://dvc.org/doc/user-guide/project-structure
- `.dvc` files are content-addressed placeholders for data files; `.dvc/` holds config and the local cache; "These metafiles are typically versioned with Git."
- `dvc.lock` is the per-commit run receipt: "file to record pipeline state"; `dvc repro` updates "dependencies and outputs... in dvc.lock and .dvc files, as needed." https://dvc.org/doc/command-reference/repro
- Truth: files + content-addressed cache; the lock ties a commit to exact dependency/output hashes.

### Sacred (and Hydra's convention)

- Sacred's FileStorageObserver writes one directory per run: `my_runs/run_3mdq4amp/` containing `run.json`, `config.json`, `cout.txt`, `info.json`. https://sacred.readthedocs.io/en/stable/observers.html
- `run.json` is the minimal viable run record: command, status, start/stop/heartbeat times, `experiment.dependencies` (pinned package versions), `experiment.repositories` (`commit`, **`dirty` flag**, url), `experiment.sources` (script → content-hashed copy under `_sources/`), and a `host` block.
- Hydra: every job runs in `outputs/YYYY-MM-DD/HH-MM-SS/` containing the composed `config.yaml`, `hydra.yaml`, and `overrides.yaml` — the config *is* an artifact of the run. https://hydra.cc/docs/tutorials/basic/running_your_app/3_working_directory
- Both are file-truth: one append-only directory per run, nothing to garbage-collect.

### Provenance for papers

- Zenodo's API makes paper↔artifact links first-class: `related_identifiers` with typed relations (`isSupplementTo`, `isVariantFormOf`, `isIdenticalTo`, `isNewVersionOf`, `isPreviousVersionOf`, `isPartOf`, `cites`, ...), plus `conceptrecid` / concept DOI so a "latest" pointer survives new versions. https://developers.zenodo.org/
- Quarto: `freeze: true` ("never re-render during project render") or `freeze: auto` ("re-render only when source changes") keeps a committed `_freeze/` state, so figures in a paper are pinned to the execution that made them. https://quarto.org/docs/projects/code-execution.html
- Quarto caveat: incremental renders re-execute; global renders keep frozen output.

## Comparison table

| Tool | Object kinds kept per project | File layout & scope | Versioning / staleness | Links between kinds | Truth: files vs app |
|---|---|---|---|---|---|
| Claude Code | instructions, memories, skills, hooks, settings | `CLAUDE.md`, `.claude/{skills,rules,hooks,settings.json,settings.local.json}`; memories machine-local `~/.claude/memories/<project>/` | memory files: frontmatter `modified`, 200-line/25KB caps, hard error on overflow; instructions: manual | MEMORY.md index → memory files; skills → sibling scripts; settings → hook scripts | instructions/skills/settings = files (committed vs local split); memories = local, app-managed |
| Codex | AGENTS.md, skills, plugins, memories | repo `AGENTS.md`, `.agents/skills/`, `.agents/skill-metadata.json`; global `~/.codex/AGENTS.md`, `~/.codex/memories.jsonl`, `~/.codex/plugins` | closest-cwd precedence; memories deduped on write, `created_at` timestamps | plugin bundles skills+agents+prompts+templates; per-repo skill visibility overrides | files; memories local JSONL |
| Cursor | rules (was: memories, notepads) | `.cursor/rules/*.mdc` with frontmatter; nested dirs; team rules in dashboard | team rules app-pushed; file rules manual | rules ← globs/description; plain md not recognized → AGENTS.md | project = files; team rules = app truth |
| Devin | playbooks, skills, deepwiki steering, (knowledge: deprecated) | `.agents/skills/*/SKILL.md` (repo), `.devin/wiki.json` + `wiki/` (repo); playbooks in web app | knowledge drift admitted; replaced by repo-committed skills (late Sep 2026) | skills suggested from sessions via Create PR; dynamic workflows attach to skills | skills/wiki = files; playbooks/knowledge = app |
| OpenHands | skills (ex-microagents), repo context, setup script | `.openhands/` + `setup.sh`; `.openhands/skills/*/SKILL.md` with `triggers` | manual (git) | trigger words/paths activate skills; AGENTS.md → `<REPO_CONTEXT>` | files |
| Copilot | repo+path instructions, skills, tasks (=issues/PRs) | `.github/copilot-instructions.md`, `.github/instructions/*.instructions.md` (`applyTo` globs), `.github/skills/` | 13k-char cap; org overrides repo | issue → PR is the task graph; reads `.claude/skills`/`.agents/skills` too | instructions/skills = files; task state = GitHub app |
| Windsurf / Devin Desktop | rules, memories (legacy), AGENTS.md | `.devin/rules/*.md` with activation frontmatter; memories `~/.codeium/windsurf/memories` (local) | memories not committed and not persisted in Devin Local → migrate to rules/skills | rules engine reads AGENTS.md as rules; `/etc/devin/rules/` pushed by admins | rules = files; memories = local app |
| Aider | conventions | any md via `--read` (e.g. `CONVENTIONS.md`) | none | none; read-only + prompt-cached | files |
| Cline / Roo | memory bank files, rules, (modes) | `memory-bank/{projectbrief,productContext,systemPatterns,techContext,activeContext,progress}.md`; `.clinerules/`, `.roo/rules[-mode]/`, `.roorules` | "update memory bank" = full manual re-read of ALL files | activeContext depends on the other five; conditional rules bind instructions to file paths | files, agent-rewritten on demand |
| Amp | AGENTS.md, threads, skills | repo/global `AGENTS.md`; threads server-side with `@T-…` ids; `amp skill add` | "abandon threads when noisy"; handoff over compaction | thread→thread references by id/url | AGENTS.md = files; threads/skills installs = app |
| Jules | (reads agents.md/README), tasks, plans | repo files read-only; tasks/plans/history in app | plan approval gate per task | task → repo; plan → task | app |
| Beads | issues/tasks + dependency graph | `.beads/` Dolt db (embedded/server), `refs/dolt/data` in git remote; `issues.jsonl` export only | schema version guard with actionable error; `bd export --all` backup | `bd dep add` (blocks/parent-child), `relates-to/duplicates/supersedes/replies-to` | db is truth; JSONL is a view |
| git-bug | bugs, identities | git objects in repo refs; push/pull via remotes | immutable git objects | comments/labels/identities in the object DAG | git objects |
| Backlog.md | tasks, plans, milestones, docs, decisions | `backlog/` (or `.backlog/`, configurable) markdown, prefixed ids | plan written into task right before implementation (3 review checkpoints) | task dependencies; search across tasks/docs/decisions | files (git optional) |
| MLflow (server) | runs, experiments, artifacts, logged models, model versions | app/db + artifact store; code version = git commit per run | model versions increment; aliases (`@champion`) as mutable pointers | model → producing run → params/metrics/artifacts (lineage) | app; code hash links back to repo |
| W&B | runs, artifacts (versions+aliases), sweeps | app-side; artifacts can reference S3/GCS with ETag versioning | append-only versions; deleting a run deletes its artifacts | `use_artifact`/`log_artifact` edges = lineage graph | app |
| DVC | pipelines (stages), data placeholders, lock | `dvc.yaml`, `.dvc` files, `dvc.lock`, `.dvc/` cache; all git-versioned | dvc.lock = per-commit run receipt (dep/out md5s) | stages → deps/outs/params/metrics/plots DAG | files + content-addressed cache |
| Sacred / Hydra | run records + configs | `my_runs/run_<id>/{run.json,config.json,cout.txt,info.json}`; `outputs/<date>/<time>/{.hydra/config.yaml,overrides.yaml}` | none (append-only dirs); `dirty` flag on repo | run.json embeds git commit + dirty + source hashes + host | files (one dir per run) |

## Patterns worth stealing for Relay's project board

1. **One index file + one file per memory, with `modified` timestamps and size caps** (Claude Code auto-memory; Codex memories.jsonl). Relay's memory-card design is validated by two vendors. Copy Claude Code's escalation ladder: warn → suggest compaction → hard error. Silent loss is the bug class.
2. **Scope by distance from cwd, always** (Codex, Amp, Claude Code, Copilot's org→repo→path stack). One rule everywhere: closest to the working directory wins. Define the same precedence for project board vs global board vs alias sources.
3. **A packaging unit bigger than one skill** (Codex plugins; Devin dynamic workflows; Claude Code plugin skills). Bundling skills + agents + prompts + templates + hooks into one installable folder is the concrete form of Relay's "skill = server that serves cases."
4. **Turn a successful run into a skill with one command** (Claude Code `/run-skill-generator`; Devin's session → "Create PR" skill suggestion). `runs.jsonl` should have a "promote this run to a skill/card" path.
5. **The run record needs: id, config, status, times, git commit + dirty flag, hashed source copies, host** (Sacred `run.json`; Hydra's `.hydra/config.yaml` in the run dir). Sacred is the smallest published schema that answers "can I reproduce this figure?" — including the `dirty` flag nobody else records.
6. **A mutable pointer on top of append-only versions** (W&B aliases; MLflow `@champion`; Zenodo concept DOI). Versions never change; aliases move. Relay cards should work the same: history append-only, "current" is a pointer.
7. **Hash ids to survive branches and multi-agent merges** (Beads `bd-a1b2`). Sequential ids collide across agents/branches. Beads also shows the value of a schema-version guard that fails with an actionable message instead of a corrupt read.
8. **Write the plan into the task right before implementation, and gate on human review** (Backlog.md's three checkpoints; Jules' plan-approval gate). Stale plans are a design flaw, not user error: Backlog.md literally schedules plan-writing late so it reflects the current codebase.
9. **Link objects by typed relations, not prose** (Beads graph link types; Zenodo `related_identifiers`; Copilot issue→PR). `supersedes` / `duplicates` / `implements` as first-class fields beat "see also" lines in markdown.
10. **Per-path activation via frontmatter globs** (Cursor `.mdc`; Copilot `applyTo`; OpenHands `triggers`). Instructions that only load for matching paths are how these tools fight context bloat without deleting knowledge.
11. **Make the committed-file vs app-state split explicit, and offer migration paths between them** (Devin Knowledge→Skills; Windsurf memories→Rules). Every vendor is converging on: durable knowledge = committed files; volatile state = app. Relay should say which cards are which, and make migrating a memory card into a rule/skill card cheap.
12. **A "keep out of PRs" planning lane** (Beads `--contributor` → `~/.beads-planning`). Planning churn and work tracking have different audiences; Relay's thread files vs cards may need the same separation.

## Traps

- **Stale instruction files are structural, not accidental.** Devin deprecated repo-pinned Knowledge because "it could drift if the code changes"; Windsurf tells users to migrate memories into Rules because memories don't persist; Cursor removed the Memories/Notepads docs pages entirely (as of 2026-09). Anything not in git decays or dies with the vendor.
- **Silent truncation of the memory index.** Claude Code shipped three successive fixes: truncation warnings, "remind the agent to compact" prompts, and finally an explicit error when writes would exceed limits. If Relay ever auto-trims, follow that ladder.
- **Memory bloat by re-read.** Cline's "update memory bank" requires the agent to "review ALL files" on every update — six files is already heavy; the pattern scales badly, and the docs themselves recommend starting small ("let structure evolve").
- **Artifact overwrite / cascade delete via shared names.** W&B documents that "Deleting a run in W&B permanently deletes all artifacts logged by that run" — app-state deletes must not cascade into files. DVC avoids identity-by-filename with content-addressed `.dvc` placeholders.
- **Lineage loss when the run record doesn't pin code.** Sacred records `commit` **and** `dirty: false/true` plus content-hashed source copies; MLflow/W&B record code versions per run. A run ledger without the dirty flag quietly produces unreproducible artifacts.
- **Export mistaken for truth.** Beads explicitly warns `.beads/issues.jsonl` is "not the source of truth or a backup." Relay's `cases.jsonl` / `runs.jsonl` need the same statement about what regenerates them and how to rebuild.
- **Frozen output that silently re-renders.** Quarto's `freeze` re-executes on incremental renders; only global renders guarantee frozen output. A published figure can diverge from the frozen state unless the render mode is pinned.
- **Vendor-owned object kinds vanish.** Devin Playbooks and Knowledge live in the web app; Cursor team rules live in a dashboard; Jules' tasks live in the app. Relay's bet — the board is a git folder — is precisely the target these tools keep migrating *toward* after losing state to the app.
- **Secrets in committed steering files.** Devin's DeepWiki docs repeat it: "Do not store API keys, tokens, or credentials in `.devin/wiki.json`." Committed context files will get secrets pasted into them; Relay card docs should state where secrets may never live.
