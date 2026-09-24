---
id: JH22
type: work
status: needs-qa-llm
labels: [feature]
component: [providers, worker, gui]
milestone: desktop-alpha
workstream: providers
assignee: agent
implemented_by: Claude Opus 5 subagent under Claude Fable 5.1 (Claude Code), 2026-09-18
rank: zzzz13
created: '2026-09-18'
acceptance: '/local in the composer runs the pane on the model this machine serves, keeping the conversation; /main puts it back; with nothing set up it says so and does not switch; Local is a fourth tier in the roles modal beside Main, Flash and Lite'
source: 'owner in chat, 2026-09-18: "add a "/local" command that switches to your chosen local LLM (make that as a 4th category with main, flash, lite, local)"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-18-local-tier/], related: [24XJ, C6YX], github: null}
---
# A Local tier, and /local in the composer

## Issue
add a "/local" command that switches to your chosen local LLM (make that as a 4th category with main, flash, lite, local)

## Tasks
- [x] `local` as a fourth tier in `presets.py`: label, hint, `PROVIDER_TIERS` for the three that come from a provider, and `tier_fallbacks("local") == ("local", "main")` <!-- t:a1 -->
- [x] Resolution in `roles.py`: the `tiers.local` override, else the first saved endpoint; the fallback note; `validate_tiers` refuses anything that is not on this machine <!-- t:a2 -->
- [x] A `local` pane role beside `flash`, accepted as `agent_role` and by `set_agent_role`, and settable per role through `roles.<role>.tier` <!-- t:a3 -->
- [x] `/local` in the composer, the `role:local` row in the model chip, the `agent.localAgent` action and its palette row <!-- t:a4 -->
- [x] The roles modal's fourth row: local endpoints only, `tiers/local/preset`, disabled with a hint when nothing is served <!-- t:a5 -->
- [x] Protocol 13 and 13.7, `tests/test_local_tier.py` (26 cases) and three cases in `tests/modelsettings_test.cpp` <!-- t:a6 -->
- [x] Live: `/local`, a real turn with a tool call on Bonsai, `/main`, the empty-registry message and both roles-modal states <!-- t:a7 -->

## Decisions
- **A fourth tier that belongs to no provider.** `TIER_DEFAULTS` stays exactly three wide: a local
  server's model id is not knowable per provider, and a `local` row for each of the nine presets
  would be nine copies of the same registry lookup. `presets.PROVIDER_TIERS` names the three that
  do come from a provider, and `TIERS` is all four. One test changed for this:
  `test_presets.TierTableTests.test_every_preset_has_all_three_tiers_pointing_at_real_presets`
  compared each provider's table against `TIERS` and now compares it against `PROVIDER_TIERS` —
  same assertion, same meaning. (`test_roles.TierTests.test_tier_summary_reports_models_notes_and_no_keys`
  also literally listed the tiers `tier_summary()` returns; `local` was added to that list.)
- **What Local resolves to.** The user's `tiers.local` override, else the first endpoint in
  `localmodels.catalog()` — so with one saved server it just works and nothing has to be picked.
  `validate_tiers` accepts only a saved `local:<slug>` id or a plain-http loopback `base_url` plus
  `model` for this tier; a hosted preset is an error with a sentence, because a Local tier that is
  not local is the one thing the tier cannot be.
- **Local falls back to Main directly, never through Lite and Flash.** `tier_fallbacks` is
  index-based for the provider ladder, which is a ladder of *size* on one provider; Local is a
  different trade, not a smaller model, so it is special-cased to `("local", "main")`. Nothing set
  up is an expected state, not a misconfiguration, so it arrives as the inline note
  `"No local model is set up; using Main."` on the tier and the role — the same mechanism as the
  no-key notes — and never as a `model_roles` warning.
- **A server that is down is not a fallback case.** A saved endpoint always resolves; if nothing is
  listening the turn fails with the transport's existing "No model server is answering on … start
  it with …", which is the sentence the user needs. Only an *absent* endpoint falls back.
- **The GUI reads the endpoints from the `presets` rows it already has** (`local: true`), so
  `tier_catalog()` gained no second copy of the registry — only its docstring says where to look.
  `catalog["providers"]` still has three tiers per provider.
- **`/local` does not switch when nothing is served.** It says "No local model is set up. Settings ›
  Local models, or scripts/relay-local.py add --detect." and leaves the pane alone, rather than
  moving it to a role that would resolve straight back to Main and look like a no-op. The chip's
  `role:local` row and the palette entry appear only when an endpoint exists, for the same reason.
- **`agent.localAgent` takes no default key** (WARP.md's shortcut-hint rule): `/local` is the fast
  path, and the hint on the slow path — picking Local from the model chip — names it
  ("Tip: /local runs this pane on the local model, /main goes back") instead of reusing the
  Main/Flash hint, which names Alt+F and would be wrong here.
- **The roles modal's Local row offers endpoints, not providers, and nothing else.** No "Model…"
  and no effort box: a local server serves one model, and a local preset reports `efforts: []`
  because an OpenAI-compatible local endpoint has no effort knob Relay can rely on. With no
  endpoint the row is disabled and reads "No local model is set up." Switching the default provider
  never clears `tiers/local/preset`: the Local tier never followed the default provider.
- **Not done here:** item 11 of the brief (the advanced "Bring your own key" dialog's warning text,
  consent wording and key placeholder for a loopback URL) was handed back to the parent session,
  which is editing `Pane::configure()`. No line of that function was touched.

## For QA
- [ ] With `local:bonsai` saved and one hosted key, a new pane opens on the hosted model; `/local`
      switches it, the chip reads "Local agent · bonsai-2-27b", the conversation is kept, and the
      context gauge takes the endpoint's 131,072-token window
- [ ] A prompt that needs a tool, on the Local agent, gets a tool call and an answer from the local
      model; `/main` puts the pane back on the hosted model with the conversation intact
- [ ] `/local` again while already there says "Already on the Local agent · bonsai-2-27b." and
      changes nothing
- [ ] With an empty registry, `/local` says "No local model is set up. Settings › Local models, or
      scripts/relay-local.py add --detect." and the pane stays on its model; the model chip has no
      Local row and the palette has no "Local agent for this pane"
- [ ] Settings › Models › Model roles has a fourth row, "Local", listing only local endpoints, with
      no Model… and no effort box; with nothing saved it is disabled and notes "No local model is
      set up; using Main."
- [ ] Advanced options: a job (e.g. Summaries) can be set to the Local tier and the row then reports
      the local model; set back to "Same as tier" it reports the Flash model again
- [ ] Changing the default provider leaves the Local row alone
- [ ] `systemctl --user stop llama-bonsai`, then a prompt on the Local agent: the pane says nothing
      is answering on 127.0.0.1:8080 and how to start it — it does **not** quietly answer on Main
- [ ] A pane on a hosted provider behaves as before: Flash and Lite resolve as they did, and
      `model_roles.warnings` carries no line about the Local tier
- [ ] A pane left on the Local agent comes back on it after a restart (the saved layout's
      `agent_role`), and `Alt+F` still toggles Main/Flash without touching Local
