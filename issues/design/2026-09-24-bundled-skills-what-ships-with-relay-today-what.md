---
id: SZ1H
type: work
status: discussing
labels: [feature, design, skills, onboarding]
component: [worker, skills]
waiting_on: owner
rank: zzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
source: 'Claude Fable guest session in Relay, 2026-09-24, split out of #9HS0'
links: {plans: [], commits: [], evidence: [], related: [9HS0, 1QKM, XHXX, HS7V, MEPR, GSK7], github: null}
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
