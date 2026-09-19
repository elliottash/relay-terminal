---
id: RM1N
type: work
status: needs-qa-llm
labels: [bug, ux]
component: [gui, providers]
milestone: beta
workstream: providers
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-18
rank: hf
created: '2026-09-18'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: 'owner, 2026-09-18: "you also still cant pick the main model options" and "i think advanced options should be separate from the providers. i might want to pick kimi k3 for main agents and glm 5.3 flash for subagents, for example."'
links: {plans: [], commits: [6d98590], evidence: ['docs/qa_evidence/2026-09-18-reasoning-levels-and-model-roles/'], related: [EFRT], github: null}
---
# Model roles: the Main row has its own controls, and a job can name its own provider

## Issue

> you also still cant pick the main model options

> (this is a separate request from thee free feature) also i think advanced options should be
> separate from the providers. i might want to pick kimi k3 for main agents and glm 5.3 flash for
> subagents, for example.

## Behaviour as implemented

Every row of the modal is now the same grid — the text, `Model…`, a provider, an effort — so Main,
Flash, Lite, Local and the vision row line up, and an Advanced row puts the tier it follows in front
of the same four cells.

- **Main** reads `Main · glm-5.3 / the pane's agent and subagents · on the default provider above`
  and carries `[Model…] [Z.AI (GLM)] [high]` where it used to carry the words "this pane's model".
  It writes nothing under `tiers/main` — the worker rejects that tier, because it *is* the pane —
  so `Model…` goes out as the pane's own `set_model` (empty restores the provider's default model)
  and the effort is the pane's effort, the same value as Options › Models › Default reasoning
  effort. Its provider combo is the Default provider list itself, so changing it there and at the
  top of the dialog are one action.
- **Advanced** rows can name a provider of their own, inline: "Same as tier" plus every provider
  with a key. Choosing one clears the row's tier and enables its `Model…` and effort; choosing a
  tier clears the provider. The owner's example — Kimi K3 for the pane, GLM-5.3-Flash for subagents
  — is two clicks in the Subagents row. The disclosure says so in one line above the list.
- The two-step "Pin to a model…" wizard (`pinRole`) is retired: pinning is what the provider combo
  does, and the vision row's own wizard went the same way for an inline `[Model…] [provider]`.
- Effort combos everywhere in the dialog take their levels from the provider that serves that row
  and carry its note in the tooltip ("Kimi: medium is sent as high."); a stored level the provider
  does not offer selects the nearest offered one rather than falling back to "Model default"
  (#EFRT).
- `Model…` on an Advanced row is disabled until the job has a provider of its own — a model id means
  nothing against whichever endpoint a tier resolves to today — and its effort is disabled while the
  row follows its built-in default, because such a row is not in the `roles` object at all.
- Both Main controls fire a shortcut hint the first few times (WARP.md's standing rule): `/model`
  for the model, Alt+. / Alt+, for the effort, under the same hint id the prompt strip's effort box
  uses so one key is not taught twice.

No new settings keys and no protocol change: `roles/<role>/tier` XOR `roles/<role>/{preset,model,
effort}` as before, and `provider/model` and `agent/effort` are read, never written, by the dialog.

## Why

The Main row had no controls because the pane's model is not a tier the worker resolves, and the
answer to that was a dead label rather than controls that talk to the pane. Per-job providers
already worked, but only through a two-step modal wizard hidden behind the last entry of a
dropdown, so the dialog read as if a job could only follow a tier — which is what the owner
concluded.

## Checks

- [ ] Model roles on a keyed provider: the Main row shows `Model…`, the provider and an effort, and
      the four rows line up as a grid.
- [ ] `Model…` on Main with another model id of that provider switches the pane's model and keeps
      the conversation; empty restores the provider's default. Nothing appears under `tiers/main` in
      `relay.conf`.
- [ ] The Main effort and Options › Models › Default reasoning effort show the same value, and each
      follows the other.
- [ ] Main's provider combo and the Default provider combo move together.
- [ ] Advanced › Subagents: pick Z.AI (GLM), then `Model…` = `glm-5.3-flash`, with the pane on Kimi.
      The row reads back as that model, and `roles/subagent/tier` is gone from the settings.
- [ ] Picking a tier again for that row clears its provider and model.
- [ ] No "Pin to a model…" entry anywhere in the dialog.
- [ ] `ctest -R modelsettings` passes; the whole `ctest` passes.
