---
id: SZ1H
type: work
status: planned
labels: [feature, design, skills, onboarding]
component: [worker, skills]
rank: zzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
source: 'Claude Fable guest session in Relay, 2026-09-24, split out of #9HS0'
links: {commits: [b74afc4f68e6, c1a9585ff165, a9870d95e14b, 1566435b6107, 42d1cf11c47e, 24f637974061], evidence: [reports/Bundled skills for Relay.md], github: null, plans: [], related: [9HS0, 1QKM, XHXX, HS7V, MEPR, GSK7, MSJ0, 95VZ, 9FX8, JVEJ, 4YKJ]}
---
# Bundled skills: what ships with Relay today, what the starter tasks need, and what the skill catalogue shows an agent

## Issue
i do want to talk about bundled skills but lets separate that. can you write another card on that on what you learned or observed so far, so i can work on it in antoher pane?

## Discussion points
Observations gathered while doing the #9HS0 onboarding research, written down so this can be worked in another pane. Nothing here is decided.

**What ships today.** Three bundled skills in `backend/relay_core/skills_bundled/`: `deliver` (work a request through the Board), `guest-account-setup` (a separate Claude Code or Codex login), `local-model-setup` (run a model locally and register it, with a `recipes/` folder). Two more ride inside the bundled task plugins: `python-workspace` and `tex-workspace` (`backend/relay_core/plugins_bundled/{python,tex}/skills/`), plus `stata`. Every one of them is about operating Relay itself. None is about a person's job: mail, papers, data, files, images.

**What the starter tasks in #9HS0 would need.** The owner's list (start coding, analyze data, triage my email, organize my folders, set up providers, organize subscriptions, painless SSH, generate art, develop a game) maps onto skills as follows. *Start coding*, *organize my folders*, *set up providers*, *SSH*: nothing beyond the agent's tools and the Models pane. *Analyze data* and *write a paper*: the python/stata and tex task plugins, which already declare `requires` (the local programs they need) and are checked before activation (`docs/TASK-PLUGINS.md`). *Triage my email*: only the owner's private `email-triage` skill (Gmail API credentials). *Organize subscriptions*: the owner's `subscription-manager` is about Amazon channels; model-subscription routing is `reports/Multiple subscription routing in Relay.md` and needs no skill. *Generate art*: the `media_*` tools and a provider that serves images, no skill. *Develop a game*: the owner's `rendered-map-level-pipeline` and `sprite-atlas-import` are specialised; a generic first-game recipe needs nothing. So the gap for a bundled starter set is small and specific: a mail skill with a credential story, and a decision on whether a starter task may ship saying "needs a skill" and pointing at the skills dialog.

**What the catalogue shows an agent.** A guest session on this machine is handed 61 skills (the `configured … skills=61` line in the worker log). Sources, in the order `default_directories` searches them (`backend/relay_core/skills.py:510-540`): the workspace's `.agents/.claude/.codex/.warp/skills`, the same four under `~`, then anything under `~/.warp` and `~/.claude` to depth 6 holding `<name>/SKILL.md` (Warp's bundled skills, Claude Code's synced ones), and Relay's bundled dir last so a user's own wins. What this produced in this session:

- **Near-duplicates under different names**: `create-skill` (Warp bundled) and `skill-creator` (Claude synced) are the same skill; `built-in-browser`, `chrome-browser`, `computer-use`, `deep-research`, `docs`, `docx`, `pdf`, `pptx`, `xlsx`, `import-memory`, `morning` each appear once from `~/.claude/skills/synced/<id>/` and again under an `anthropic-skills:` prefix from the harness's own catalogue. The duplicate-name rule (`skills.py:259`, first directory wins) does not catch a different name for the same skill.
- **Skills that target another product's UI are still listed**: `change-keybinding` (Warp's keybindings.yaml), `tab-configs`, `factory-files`, `tui-migrate-setup`, and the nine `figma-*` skills from `~/.warp/remote-server/bundled_resources/bundled/mcp_skills/figma/`, which need Figma MCP tools Relay does not have. `DEFAULT_EXCLUDE` (`skills.py:450-451`) names seven Warp skills and misses these.
- **Trigger text is clipped**: `MAX_SHORT = 100` and `MAX_DESCRIPTION = 150` (`skills.py:27,33`) cut the catalogue line mid-sentence ("…which uses Google SSO. Uses Playwright attached to a real Brave session so the Google login works normally; defaults to e-check/ACH and always confirms before…"), so the words that say when to use a skill are the ones lost.
- **A skill has no `requires`.** Task plugins declare the programs they need and are checked before activation; a skill that needs Playwright and a Brave profile (`beam-rent`), Gmail credentials (`email-triage`), Editorial Manager credentials (`jle-*`) or the provenance kernel (`provenance`) is listed as available on any machine. The first the person learns is a failed run.
- **No bundled skill has a `Try it` or a test.** #1QKM proposes skills as the Board's second object with cases served against them; #YZ8G is the QA skilling card; the bundled three have `profile:` front matter (`deliver`) but no case that proves them.

**Questions this card is for.**

1. Which skills should ship with Relay: only operating-Relay skills as now, or a starter set for the #9HS0 tasks (mail, folders, a first paper, a first analysis, a first game)? A bundled skill becomes a promise on every machine, so each needs a `requires` and a credential story.
2. Whether `SkillIndex` should dedupe by content or by a declared `id`, and whether skills that name tools Relay lacks (`mcp__figma__*`, Warp's `warpctrl`) should be hidden by a tool-availability check rather than by name in `DEFAULT_EXCLUDE`.
3. Whether a skill manifest gets `requires` (programs, environment variables, MCP tools) like a task plugin, checked at catalogue time and shown as "needs …" rather than dropped.
4. Whether the catalogue line should be the skill's own `short:` when present and otherwise the first sentence of the description, instead of a 100-character clip.

Related: #HS7V (`.relay` as the canonical home for skills), #GSK7 (global skills reach guests), #XHXX and `reports/Plugin ecosystems for Relay.md` (what Warp, Claude and Codex bundle), #MEPR / #C0Q8 (task plugins and their `requires`), #1QKM (skills as the Board's second object).

### Research, 2026-09-24

The survey and the measurements are in [Bundled skills for Relay](../../reports/Bundled%20skills%20for%20Relay.md): the skill concept read from #1QKM, what Claude Code, Codex, Warp, VS Code, Cursor, Gemini CLI, Kiro and OpenCode bundle and how they show it, Relay's catalogue on this machine loaded with `SkillIndex` itself, and a recommendation per question. The short version:

**From #1QKM.** A bundled skill is a server Relay ships and vouches for, so it needs what a shipped program needs: a declared environment (`requires`), a version (`cases.skill_version` already hashes the manifest), evidence that arrives with it (a `Try it` case) and a release test. The profile's *rot rate* is the factor that decides what may be bundled: Relay can only keep the promise for skills whose tools Relay itself provides. The codification axis (§2) says a "mail skill with a credential story" is a program (a tool group owning the credential) plus a thin skill, the way `python-workspace` sits over `relay.python`. A starter task is a case template, not a skill.

**What the others do.** Every product bundles a small *operating* set (Claude about ten: run, verify, doctor, code-review, loop…; Cursor about fifteen; Warp fourteen plus eight Figma skills in a separate `mcp_skills/` tree; Codex `skill-creator` and `plan`) and none bundles job skills; document and mail skills arrive by account sync, plugin or marketplace. The bundle is switchable as a set (`disableBundledSkills`) and loses every conflict. Requirements come as free text (spec `compatibility`), a structured MCP dependency (Codex `agents/openai.yaml` `dependencies.tools[]`) or by shipping the dependency inside the package (Kiro powers, Gemini extension `settings`); nobody checks programs, so Relay's task-plugin `requires` is already ahead. Availability is gated by tools or paths, never by a name list. Nobody dedupes by content. Invocation policy (`disable-model-invocation`, `allow_implicit_invocation`, OpenCode `permission.skill`) is a first-class switch everywhere and absent in Relay. Catalogue budgets: Codex 8,000 characters, Claude 1,536 per skill, Relay 5 KiB for the whole section.

**Measured here.** 61 skills: 30 from the owner's Warp tree, 15 from Warp's product bundle, 13 Claude-synced, 3 Relay. The prompt section settled on **50-character** triggers (not 100) because 61 lines do not fit 5 KiB, so every line is cut before the words that say when to use it. About a third (21) name a tool Relay does not have (Figma MCP, Claude desktop surfaces, Warp's UI tools); about a quarter need an undeclared credential or install; the rest are portable. Hiding the unrunnable third alone gives the survivors 70-character lines under today's budget. On a Claude guest the `[Relay skills]` block repeats the 13 synced skills the harness already lists, which is the doubled catalogue observed above.

**Recommendations.** *Q1:* keep the bundle to operating-Relay skills, add a Relay `skill-creator` (writes a skill with `profile`, `requires` and a `Try it` case), never bundle a skill that drives another product's UI; mail becomes a task plugin (`mail_*` tools, credential in the keystore) plus a thin bundled skill, as a design card of its own. *Q2:* identity by content hash (same hash shown once as "same as"), hide by tool availability from a declared `requires` with two inferences for undeclared skills (Warp `mcp_skills/<server>/`, `mcp__x__*` in a synced description); stop discovering Warp's `remote-server/bundled_resources` tree by default (it is Warp's product bundle, as Claude's built-ins are Claude's), which retires `DEFAULT_EXCLUDE`; omit the guest's own home tree from the guest block. *Q3:* yes, `requires: |` in the task plugin's shape (`program` with alternatives, version flag, install hint, optional; plus `env`, `secret`, `tool`), checked at index time: a missing tool hides, a missing program or secret lists with "needs X"; `money: yes` or `sign_off: send` in the profile makes a skill explicit-invocation only. *Q4:* `short:` else first sentence, `MAX_PROMPT_BYTES` to 8 KiB, and let hiding do the rest. Plus a release test for the bundle (`tests/test_bundled_skills.py`).

**Suggested delivery** (report §5): (1) `requires`, tool hiding, content identity — worker, medium; (2) catalogue line, budget, guest block — worker, small; (3) the bundle as servers: profiles, `requires`, `skill-creator`, `Try it` cases, release test — worker, medium; (4) mail as a task plugin plus skill — design, large, owner's scope decision.

### State refresh, 2026-09-25

Re-checked after a day of other sessions' work; the recommendations stand unchanged.

**Landed since the research.** #MSJ0 (skill `profile:` with the claim-time default) and #95VZ (the case ledger: `cases.jsonl`, `board_case`, and `skills_list` items carrying `cases`, `last_served`, `pass_rate_30`, `stale`) — so the report's premise "skills are servers with no case record" is half-built: the record now exists and the bundled three are exactly the servers with no cases served against them yet. #9FX8 put the registry in the Board (Cards | Skills | Memories, with a skill page; #JVEJ retires SkillsDialog), and #4YKJ extends profiles with step outcomes and the harden/soften hint. Nothing of the four recommendations has been implemented yet.

**Re-measured today.** `skills.py` is unchanged where it matters: `MAX_PROMPT_BYTES` still 5 KiB, `DEFAULT_EXCLUDE` still the seven-name list, Warp's `remote-server/bundled_resources` still discovered. The catalogue is now 62 skills (one more synced from Claude) and the trigger budget settled at 60 characters — same conclusion, every line still cut before the words that say when to use it.

**Delivery approved.** The owner approved the recommendations and added semantic deduplication plus a first-run maintenance skill. Five planned child cards now hold the delivery work: #K26R, #G8JN, #4EMF, #M91Y and #1E5F.

## Decisions

2026-09-25 — Owner approved the research recommendations: keep bundled skills focused on Relay operations; add skill `requires` and availability checks; improve catalog text and budget; give bundled skills profiles and release cases; design mail as a task plugin with a thin skill. For identity, collapse byte-identical skills automatically and find semantic equivalents as review candidates. Add a skill-maintenance skill that runs during first-install onboarding to discover and import skills from other agents, then maintains the catalog. Keep source, version and differences visible; do not silently merge distinct instructions. See the verbatim owner decision in the thread.

## Plan
**Goal.** Make Relay's skill catalog trustworthy from first install through ongoing use, while shipping only skills Relay can maintain. #1QKM supplies the server, case and verification model; this card owns catalog ingestion, requirements and the bundle.

**Findings.** #HS7V defines Relay's canonical skill home; #9FX8 supplies the Skills registry; #MSJ0 and #95VZ supply profiles and case history. Their landed code is in needs-verification. The five approved children are #K26R, #G8JN, #4EMF, #M91Y and #1E5F.

**Sequence.**
1. Verify the shared foundations (#HS7V, #MSJ0, #95VZ, #9FX8) and settle their exposed contracts. Keep a failing or changed contract on its owning card rather than reimplementing it here.
2. Build #K26R's requirement checks, source provenance and exact-content identity first. #G8JN's catalog text, prompt budget and guest filtering can proceed alongside it, then be tested against the same mixed-source fixture.
3. Build #4EMF's bundled profiles, creator, Try-it cases and release check on those requirements. Build #M91Y's first-run import and maintenance workflow against the same index and #HS7V's canonical destination. Inventory and compare before any import; preserve source files and require a choice for semantic consolidation.
4. Run #1E5F as a design track in parallel. Its mail plugin and triage skill enter the bundle only after the credential, consent and verification design is settled; it does not block the core catalog rollout.
5. Exercise one end-to-end fixture with Relay, Claude, Codex and Warp sources: import proposal, exact and semantic duplicate handling, unavailable requirements, catalog display, a Try-it case and a repeat maintenance run. Link each result to its child card and check that #1QKM's case ledger records the served case.

**Risks.** Semantic similarity is fallible: it produces review candidates, never an automatic merge. First-run import must stay reversible and must not copy secrets. Guest catalogs may already list their own skills, so filter at the prompt boundary. Treat pending QA on foundation cards as a gate to claiming their contracts stable.

**Verify.** Each child records its own targeted tests and outcome. Close this orchestration card only when all five children have their own verdicts, the integrated catalog fixture passes, and the mail design has its scope decision.
