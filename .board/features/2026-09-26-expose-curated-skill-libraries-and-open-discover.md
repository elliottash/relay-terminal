---
id: H7NF
type: work
status: planned
labels: [feature, skills, onboarding]
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-26'
verify: {artifact: system, primary: script, also: [person], human: optional, criteria: 'A new user can discover, preview and import one compatible skill with its source and requirements clear before import.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: this Relay pane, 2026-09-26; follows discussion of which skills libraries to expose
links: {plans: [], commits: [], evidence: [], related: [M91Y, K26R, G8JN, 9FX8], github: null}
---
# Expose curated skill libraries and open discovery in Globals › Skills

## Issue
Add a reviewable discovery path for external Agent Skills libraries. Lead with installed skills, feature a small set of compatible sources, and allow broader search and repository import with clear availability and provenance.

> I agree with this direction. write a plan on a card
> — elliott · [session:15271cf0c35e43bc91433afe814b3a16](relay://session/15271cf0c35e43bc91433afe814b3a16) · 2026-09-26

## Done means
Globals › Skills shows installed project and personal skills first, with source and current-pane availability. A Discover area offers individual skills from a small featured set (Anthropic and Vercel), a conditional Warp/Oz source, a route to skills.sh for broad discovery, and Import from repository for arbitrary Git sources. Selecting a skill shows its instructions, files, provenance, pinned revision, requirements and conflicts before the user imports it; no library is imported wholesale or silently updated. Duplicates and unavailable skills are labelled accurately, and guests do not receive a second copy of skills their harness already lists.

## Plan
**Goal.** Let a new Relay user find useful Agent Skills without flooding the agent's catalog or implying that a skill can run when its tools are absent.

**Findings.** Globals › Skills and its registry are in `src/SkillRegistryView.*` and `docs/ARCHITECTURE.md` (§ Skills). `import_skills_preview`/`confirm` and update checks already import selected skills from a pinned Git revision (`backend/relay_core/skill_manage.py`, `docs/AGENT-SESSIONS-PROTOCOL.md`). #M91Y handles local first-run inventory and ongoing maintenance; #K26R owns requirement checks and duplicate identity; #G8JN owns model-facing catalog text and guest filtering. The research in `reports/Bundled skills for Relay.md` found many listed skills cannot run in Relay. OpenAI marks `openai/skills` deprecated, so it is not a featured source.

**Steps.**
1. Add a Discover section to Globals › Skills, below installed project and personal skills. Keep discovery results out of the model-facing catalog until a skill is imported.
2. Seed a small, maintained source list: Anthropic's `anthropics/skills` for general/document skills and Vercel's `vercel-labs/agent-skills` for web development. Show Warp's `warpdotdev/oz-skills` as conditional, with its tool requirements visible. Each entry links to its upstream repository and carries a short scope description and last checked revision.
3. Reuse repository preview to browse a source and select individual skills. The review shows the `SKILL.md`, supporting files/scripts, license when present, Git origin and commit, required tools/programs/secrets, availability in the current pane, and any name/content conflict. Import only checked skills after a user action; preserve update provenance.
4. Add “Explore skills.sh” as an outbound discovery path plus the existing “Import from repository…” escape hatch. Treat skills.sh as an index, not as a trust or compatibility verdict; a chosen repository still goes through Relay's preview.
5. Integrate #K26R's availability and exact-duplicate results and #M91Y's semantic-review suggestions. Show “works here,” “needs setup,” or “unavailable here” with a reason. Keep semantically similar skills separate until the user chooses.
6. Exercise project, personal, guest, and fresh-install cases. Check that selecting one skill imports one skill, provenance survives update checks, unavailable skills do not appear as runnable, and the guest prompt omits skills already supplied by its harness.

**Risks and decisions.** Upstream repositories and skills.sh can change; store pinned commits and source metadata, and review the featured list at release. A repository's scripts are untrusted until reviewed. Start with curated source browsing and an external skills.sh link; native cross-repository search can be added after the import and availability flow is reliable. Keep OpenAI's deprecated skills repository out of featured sources; revisit its current plugins repository only if Relay gains a plugin-compatible import path.

**Verify.** Targeted backend tests for source metadata, preview/selective import, requirements and duplicates; UI tests for ordering and states in Globals › Skills; one manual pass from a clean profile through discovery, preview, import and update check, including a guest pane.
