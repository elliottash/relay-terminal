---
id: P7QK
type: work
status: needs-qa-llm
labels: [change, bug, ux]
component: [gui, backend]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: a
created: '2026-09-18'
acceptance: 'The roles modal offers only providers with a stored key, names them after the company, and a tier that names a provider runs that provider''s model for that tier (Kimi Main + GLM Flash in two clicks); "Auto detect" is called "Auto" everywhere; `docs/qa_evidence/2026-09-18-provider-names-and-tier-providers/`; a non-Claude QA session runs the checklist below'
source: 'owner in chat, 2026-09-18: "a typical use case will be, i want kimi for my main model, and glm 5.3 flash for the flash model. so the interface there should make that easy to do … in model roles, it shouldnt show options where you dont have a key assigned … for ''providers'', it should not say the model name, it should say Kimi, Z.AI (GLM), OpenRouter, OpenAI (ChatGPT), Anthropic (Claude), Google (Gemini)" and, separately, "replace ''auto detect'' with ''auto'' (its more concise)"'
links: {plans: [], commits: ['834ca1b'], evidence: ['docs/qa_evidence/2026-09-18-provider-names-and-tier-providers/'], related: [M2C1], github: null}
---
# Model roles names providers, offers only the ones you hold a key for, and gives a tier that provider's tier model

## Report

Owner, 2026-09-18, three things about Settings › Models › Model roles, and one about wording:

> "a typical use case will be, i want kimi for my main model, and glm 5.3 flash for the flash model.
> so the interface there should make that easy to do."
> "in model roles, it shouldnt show options where you dont have a key assigned."
> "for 'providers', it should not say the model name, it should say Kimi, Z.AI (GLM), OpenRouter,
> OpenAI (ChatGPT), Anthropic (Claude), Google (Gemini)."
> "replace 'auto detect' with 'auto' (its more concise). check the rest of the UI to see if 'auto
> detect' is ever used, and replace with 'auto'."

## Root cause

The roles modal was built out of the keys modal's vocabulary. A *preset* is a plan you hold a key for
— "Kimi · K3", "Z.AI · GLM-5.3 · Coding Plan" — and the keys modal is right to list one row per plan
and to name the model each plan defaults to. The roles modal reuses the same `label`, but its lists
choose a **provider** for a row whose model is decided by the tier, so the model in the label was at
best noise and at worst a claim the row does not honour.

The "easy to do" part was a real defect underneath the labels. Picking a provider for a tier writes
`tiers/<tier>/preset` with no model, and `roles.RoleResolver._tier_entry` filled the blank with
`preset.model` — the provider's *headline* model. So choosing Z.AI for Flash gave `glm-5.3`, the
Main-grade model at Main-grade prices, and the only way to the intended `glm-5.3-flash` was to type
the id into **Model…**. The GUI's own comment ("that provider's own default") described the behaviour
that was wanted, not the one that happened.

Two smaller ones: the Default provider dropdown listed all nine presets, six of them marked
"(no key)" and none of them usable; and changing the default provider cleared *every* tier override,
including one that named a different provider on purpose — which is exactly the Kimi + Z.AI pairing.

## Change

**Backend.**

- `presets.Preset` gains `provider` (the company: Kimi, Z.AI (GLM), MiniMax, OpenRouter,
  OpenAI (ChatGPT), Anthropic (Claude), Google (Gemini)) and `plan` (Coding Plan, Pay-as-you-go,
  Token Plan, standard API), both in `to_dict()`, so the GUI keeps no table of its own.
- `presets.provider_tier_model(preset_id, tier)`: that provider's own (model, extra) for a tier. Where
  the provider's entry for the tier points elsewhere — every provider's Lite is Gemini through
  OpenRouter — the named provider is kept and the nearest tier that stays on it is used instead, so
  "Lite on Z.AI" is `glm-5.3-flash` rather than a silent jump to OpenRouter.
- `roles.RoleResolver._tier_entry` uses it when an override names a preset and no model. An override
  that also names a model still wins outright, and a tier with no override is untouched.

**GUI (`src/ModelSettings.cpp`, the roles modal only).**

- Every list in the modal names the company. `providerChoice` appends `· <plan>` only when two presets
  of the same company are both on offer (a Moonshot key *and* a Kimi Code key), so the plan appears
  where it disambiguates and nowhere else. The recommended line always spells the plan out — it names
  something to go and buy.
- `choosableProviders` is what the Default provider dropdown and the tier rows offer: presets with a
  stored key, always including the one in use, and — only when nothing at all has a key — everything,
  marked "(no key)", so a fresh install still has something to pick and the **API keys…** button
  beside it has somewhere to come back to. A tier pinned to a provider whose key later goes away keeps
  showing that provider, marked, instead of silently reading "Default provider".
- `chooseProvider` now keeps a tier override that names a *different* provider, and clears only the
  ones that followed the provider being left behind.
- The keys modal is deliberately unchanged: it is a list of plans, `label` is right there, and the
  group headers (Subscriptions / Aggregator / Pay-as-you-go) already say which is which.

**Wording (`src/main.cpp`).** "Auto detect" → "Auto" in the seven places it was written: the input-mode
palette rows and their two action descriptions, the mode toast, the mode chip's menu, the Settings ›
General "Default input for new sessions" row and the submenu's current-value label. The stored value
is still `auto`; nothing is migrated because nothing changed on disk.

**Docs.** `docs/AGENT-SESSIONS-PROTOCOL.md` 13.7 (what a provider-only tier override means) and §15
(the `provider`/`plan` fields, and which modal uses which label).

## Implementer evidence

Build clean. `tests/modelsettings_test.cpp` is new — `src/ModelSettings.cpp` became the
`relay-modelsettings` static library so the dialog can be built headlessly, the way `relay-settings`
already was — and covers the lists, the plan suffix, the empty-keyring case, what picking a provider
on a tier writes, and the override that survives a change of default provider (8 cases, all passing).
`tests/test_presets.py` and `tests/test_roles.py` cover `provider`/`plan` and the tier model,
including "Lite on Z.AI stays on Z.AI" and "a typed model still wins".

`ctest --test-dir build` 29/29 and `./scripts/test.sh` 883 tests OK on this change. One earlier full
run failed `test_remote_browser.test_pair_and_drive_a_pane_from_the_browser`, which passed on rerun;
that test drives `app/app.js` and `remote/gui_host.py`, both mid-rewrite by another session in this
checkout, and nothing here touches them.

Live under Xvfb with an isolated profile, `RELAY_KEYRING=off`, fake `RELAY_*_API_KEY` strings and
every proxy variable pointed at a closed port — no account, no turn, nothing sent to a provider:
`docs/qa_evidence/2026-09-18-provider-names-and-tier-providers/` (README lists all seven shots).
The one that matters: **Main · kimi-k3** with **Flash · glm-5.3-flash · on Z.AI (GLM)**, reached by
picking two providers and typing nothing.

## Not done / follow-ups

- **The Main tier row is still read-only** ("this pane's model"). Its provider changes with the
  Default provider box at the top, which is the half of the owner's use case that matters; its
  *model* still needs `set_model` from the dialog (the pane's live model, refused mid-turn), which is
  not built. Owner asked why on 2026-09-18 and did not ask for it to be built; the same follow-up is
  recorded on `issues/features/needs_qa_llm/2026-09-17-model-settings.md`.
- **The keys modal still takes your word for which row a key belongs in.** Same day, the owner
  reported the modal calling their Kimi key pay-as-you-go when they expected a coding plan. It was not
  a bug — the stored key authenticates at `https://api.moonshot.ai/v1` and is rejected (401) by the
  Kimi Code base, and the account carries a cash balance — so nothing was changed. But a key pasted
  into the wrong row shows a confidently wrong group and only fails later, at request time, and
  **Test** already makes the call that could tell: it could report when a key authenticates against a
  sibling preset's base URL instead. Not filed as its own issue: the owner was offered it and has not
  said yes.
- **`max_tokens`: the default is 8,192 (`provider.py:238`) but `docs/INTAKE-CLARIFICATION-RESEARCH.md`
  line 79 builds the auto-compact reserve on "the `max_tokens` Relay requests (32,768 today)", which
  is the validator ceiling, not what is sent.** The reserve is therefore 4× larger than the doc says
  (safe direction, wrong number). Noticed while answering an owner question on 2026-09-18; no code
  changed. Worth one line in that doc, or a per-preset default, since the presets' own output limits
  differ by 100× (Kimi K3 1M, DeepSeek V4.1 Flash 384K, GLM-5.3 128K).

## QA checklist

1. **Names.** Settings › Models › Model roles. The Default provider box, the tier rows' provider
   boxes, the "· on X" suffixes and "Pin to a model…" all read Kimi / Z.AI (GLM) / OpenRouter /
   OpenAI (ChatGPT) / Anthropic (Claude) / Google (Gemini) / MiniMax. No model id appears in any of
   them. The keys modal is unchanged and still lists one row per plan.
2. **Only what you can use.** With keys for two providers, the Default provider box has exactly two
   entries and neither says "(no key)". Add a third key in API keys… and it appears without
   reopening the modal; remove one and it goes.
3. **Two plans, one company.** With both a Moonshot (`kimi`) and a Kimi Code (`kimi-code`) key, the
   entries read "Kimi · Pay-as-you-go" and "Kimi · Coding Plan"; with only one of them, plain "Kimi".
4. **The use case.** Default provider Kimi, then Flash row → Z.AI (GLM), nothing typed. The Flash row
   must read `glm-5.3-flash` "on Z.AI (GLM)" — not `glm-5.3`. Run a turn on the Flash agent (Alt+F)
   and confirm from the request ledger that `glm-5.3-flash` is what was called.
5. **Lite on a provider whose Lite is elsewhere.** Lite row → Z.AI (GLM): `glm-5.3-flash`, and the
   request must go to Z.AI, not OpenRouter.
6. **Typed models still win.** Flash → Model… → `glm-5.3`: the row keeps `glm-5.3`. Clear the field
   and it goes back to `glm-5.3-flash`.
7. **Changing the default provider.** With Flash on Z.AI, switch the default provider from Kimi to
   OpenRouter: Flash is still on Z.AI, Lite (which followed Kimi) goes back to following the default,
   and the pane's own model becomes OpenRouter's.
8. **A key that goes away.** With Flash on Z.AI, remove the Z.AI key. The Flash row still names
   Z.AI (GLM), marked "(no key)", and the row underneath says it is using Main. Nothing crashes and
   the modal still opens from a cold start in that state.
9. **No keys at all.** On a profile with no key anywhere, the Default provider box lists every
   provider marked "(no key)" and API keys… is reachable.
10. **Auto.** The prompt-box mode chip menu reads auto / terminal / agent; Settings › General ›
    "Default input for new sessions" offers Auto / Terminal / Agent; Ctrl+I toasts "Input: Auto";
    the actions palette's Input mode rows and its current-value label say Auto. Nothing anywhere
    still says "Auto detect". A profile written before this change still starts in the mode it was
    left in.
