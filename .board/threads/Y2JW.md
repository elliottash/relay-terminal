<!-- relay:entry 20260919T181140Z-14 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-new-models-command.md

<!-- relay:entry 20260919T181221Z-z4 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T181221Z-z5 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
new /models command.
```

</details>

<details><summary>after</summary>

```
new /models command. it should bring to the models options. 

it should be easy to un-check models. so they dont show up in the picker.
```

</details>

<!-- relay:entry 20260919T200305Z-v0 author=agent kind=event model=glm-5.3 pane=switchboard turn=e17598d07151419d86ddfea89de60412/c1c5a69cae214f50bedb9384a2802d81 -->
- ✦ agent updated this card · labels: (unset) → ["feature", "settings"]

<!-- relay:entry 20260920T040021Z-v4 author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T040051Z-ts author=owner kind=comment mode=discuss -->
in general, its not easy enough to just instantly pick the set of models that are in my picker. so that deserves a broader investigation.

<!-- relay:entry 20260920T040051Z-tt author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260920T040241Z-6h author=agent kind=event model=glm-5.3 pane=switchboard turn=fa69ad868d574ec2b867daea6b3f8a46/81fc22af2fc74ad5a69332f835eecf60 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; title: new /models command. → Curate which models show in the picker (/models command)

<!-- relay:entry 20260920T040241Z-f2 author=agent kind=rewrite model=glm-5.3 pane=switchboard turn=fa69ad868d574ec2b867daea6b3f8a46/81fc22af2fc74ad5a69332f835eecf60 -->
- ✦ rewrote title

<details><summary>before</summary>

```
new /models command.
```

</details>

<details><summary>after</summary>

```
Curate which models show in the picker (/models command)
```

</details>

<!-- relay:entry 20260920T040247Z-70 author=agent kind=event model=glm-5.3 pane=switchboard turn=fa69ad868d574ec2b867daea6b3f8a46/81fc22af2fc74ad5a69332f835eecf60 -->
- ✦ agent updated this card · appended to `## Investigation notes`

<!-- relay:entry 20260920T040254Z-k1 author=agent kind=question model=glm-5.3 pane=switchboard turn=fa69ad868d574ec2b867daea6b3f8a46/81fc22af2fc74ad5a69332f835eecf60 -->
1. Where does un-checking live? (a) only in Options › Models, opened by the new `/models` command; (b) also reachable from the picker itself — a "Customize models…" row at the bottom of the model picker that opens the same checklist. **Recommend (b)**: one checklist, two doors, so it's instant from wherever you noticed the clutter.
2. What can be hidden? Picker row-level entries only (roles, tiers, guests — Main always stays), or also individual models inside a provider? **Recommend row-level first** — it matches what you see in the picker and covers the pain; per-model hiding can come later.
3. One global list of shown models, or per-pane? **Recommend global** (like the rest of Settings › Models); per-pane invites "where did my model go" confusion.

Next step once these are answered: press **Plan** on this card to scope the broader picker-curation investigation.

<!-- relay:entry 20260920T040303Z-aq author=agent kind=comment mode=discuss model=glm-5.3 turn=fa69ad868d574ec2b867daea6b3f8a46/81fc22af2fc74ad5a69332f835eecf60 -->
I looked at what exists today, and the pain is real: the model picker always draws everything — Main, every role, tier rows, guest rows — and **nothing anywhere can hide a picker entry**. The only shaping tool is the Roles dialog (Settings › Models), several clicks away, with no checkboxes. There's no `/models` command yet; `/model` just switches.

Card changes on #Y2JW:
- Retitled it to *"Curate which models show in the picker (/models command)"* to match the broader scope, and set it waiting on you.
- Added an **Investigation notes** section with the current-state facts above (file references included).
- Asked three questions with my recommendations:
  1. Un-checking lives in a checklist opened by `/models` **and** by a "Customize models…" row inside the picker itself.
  2. Hiding is row-level first (roles, tiers, guests; Main always stays) — per-model hiding later.
  3. One global shown-list, not per-pane.

Once you answer those, the move is **Plan** on this card to scope the curation design properly.

<!-- relay:entry 20260920T040508Z-p1 author=owner kind=comment mode=discuss -->
1 yes (b), allow unchecking models, and "add models" in the picker

2 it needs to be flexible, i might want kimi k3 and glm 5.3 flash and gpt luna for example. 

3 global

<!-- relay:entry 20260920T040531Z-04 author=agent kind=decision model=glm-5.3 pane=switchboard turn=6f0f9a44e24f42db931568c42c6e6894/601fb424c11a4dbbb08a848180186c35 -->
1. "yes (b), allow unchecking models, and \"add models\" in the picker" — one checklist, two doors: opened by `/models` and by a row inside the picker; the picker also gets an "add models" affordance, not just un-checking.
2. "it needs to be flexible, i might want kimi k3 and glm 5.3 flash and gpt luna for example." — curation is per-model, not row-level: any individual model (across providers, named examples like Kimi K3, GLM 5.3 Flash, GPT Luna) can be in or out of the picker.
3. "global" — one global shown-models list, shared across panes.

<!-- relay:entry 20260920T040537Z-nm author=agent kind=event model=glm-5.3 pane=switchboard turn=6f0f9a44e24f42db931568c42c6e6894/601fb424c11a4dbbb08a848180186c35 -->
- ✦ agent updated this card · waiting_on: owner → (unset); appended to `## Decisions`

<!-- relay:entry 20260920T040551Z-ya author=agent kind=event model=glm-5.3 pane=switchboard turn=6f0f9a44e24f42db931568c42c6e6894/601fb424c11a4dbbb08a848180186c35 -->
- ✦ agent updated this card · replaced `## Decisions`

<!-- relay:entry 20260920T040555Z-8z author=agent kind=event model=glm-5.3 pane=switchboard turn=6f0f9a44e24f42db931568c42c6e6894/601fb424c11a4dbbb08a848180186c35 -->
- ✦ agent moved this card · Discussing → Ready to start · Owner answered all three scoping questions (two doors + add, per-model, global); ready for a plan.

<!-- relay:entry 20260920T040604Z-st author=agent kind=comment mode=discuss model=glm-5.3 turn=6f0f9a44e24f42db931568c42c6e6894/601fb424c11a4dbbb08a848180186c35 -->
The owner answered all three. Recording their decisions on the card:



The Decisions text got truncated by a stray quote — fixing the section:_resolver: fe</arg_value><5b656597><arg_key><2b53f23f>tool_use_id</arg_key><ac7a3bd7><arg_value><b88a6f17>toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB</arg_value><5b656597></tool_call><c4cf82b7></tool_call><c4cf82b7></think><5e9c7f0d>Malformed calls on my part — redoing the section fix properly:



**All three answers are recorded on #Y2JW** — quoted verbatim as a decision, written into a new `## Decisions` section, waiting-on-you cleared, and the card moved to **ready**:

1. **Two doors + add** — checklist opens via `/models` and from a row in the picker itself, plus an "add models" affordance in the picker.
2. **Per-model and flexible** — your Kimi K3 / GLM 5.3 Flash / GPT Luna example means curation at the individual-model level, not just role/tier rows. That's the bigger build: the checklist has to offer *any* model across providers, including ones not bound to a role or tier today.
3. **Global** — one shown-models list, all panes.

One wrinkle a plan should settle: "add models" implies picker entries that aren't just current roles/tiers — so the design needs a small per-model entry format (id, label, provider) rather than reusing role rows. That's Plan's scope, not a question for you.

Next step: press **Plan** on #Y2JW to scope the design and the picker-entry format.

<!-- relay:entry 20260920T051516Z-0q author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T052127Z-2p author=agent kind=event model=kimi-k3 pane=switchboard turn=443f7d94fa4844ddba9676e8bb8b9cef/6b5c6822e4814cffab116a2ce84f6d0f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T052137Z-01 author=agent kind=comment mode=plan model=kimi-k3 turn=443f7d94fa4844ddba9676e8bb8b9cef/6b5c6822e4814cffab116a2ce84f6d0f -->
I have the full picture now. Writing the plan onto the card.

**Plan is on #Y2JW.** It builds one checklist dialog (new `ModelsDialog` beside `RolesDialog`), opened three ways — `/models`, a "✚ Customize models…" row in the picker, and Settings › Models — persisting a global `models/picker_shown` list that both pickers (the status combo and the `/model` modal) read through one helper. Per-model entries ride the existing `set_model {preset, model}` override, so no worker changes; absent/empty setting keeps today's behavior.

Nothing is needed from you — press **Execute** when ready. The only judgment calls I baked in (both reversible at QA): guest rows are checkable too, and "add models" is the dialog's per-provider "add by id" field plus re-checking hidden entries, rather than a separate picker affordance.

<!-- relay:entry 20260923T182506Z-1n author=owner kind=comment mode=discuss -->
i think this is resolved -- check if there are any good points here we still need

<!-- relay:entry 20260923T183041Z-q0 author=agent kind=evidence model=kimi-k3 pane=switchboard turn=1ebc6fed9377680339fda5fc25ee16e0/7dba9e6ee501457b986d8a1fb9471ab1 -->
Resolution check (Discuss turn, owner: "i think this is resolved"). Everything this card asked for shipped via the card #MDL1 model-catalog redesign (2026-09-20/21):

1. **/models command** — exists; opens the Models pane. `docs/ARCHITECTURE.md:3635`: "the models pane (Ctrl+Shift+M, `/model`, `/models`)"; `src/SettingsPane.cpp:68` maps `agent.modelOptions` → `/models`.
2. **Un-check models so they leave the picker** — the step-2 "available" checklist: `models/available` + `curation::setAvailable` (`src/ModelCatalog.h`), edited per-model in the Models pane's available tab / Ctrl+Alt+M dialog; an un-checked model leaves the tier lists, the box and its typed filter.
3. **Add models** — per-provider "+ add a model…" rows (`src/ModelPicker.cpp` `addModelById`/`promptAddModelById`), persisted as `models/custom`.
4. **Per-model, flexible** (Kimi K3 / GLM 5.3 Flash / GPT Luna) — the whole catalog is per-model entries `<preset>|<model>` (`relay::models::Entry`), so any individual model across providers is in or out independently.
5. **Global** — all curation lives in QSettings, shared by every pane and window.

Note: the plan on this card (`models/picker_shown` + a `ModelsDialog`) was built and then deliberately retired by card #MDL1 t:a10 in favor of `models/available` — the shipped shape differs from the plan, but the requested outcome is fully covered. No leftover points found.

<!-- relay:entry 20260923T183050Z-2w author=agent kind=event model=kimi-k3 pane=switchboard turn=1ebc6fed9377680339fda5fc25ee16e0/7dba9e6ee501457b986d8a1fb9471ab1 -->
- ✦ agent moved this card · Ready to start → Done · Owner confirmed resolved; verification found every ask shipped via the card #MDL1 model-catalog work (/models command, per-model available checklist, add-by-id, global QSettings). The card's own plan was superseded by that redesign, but the outcome is delivered. · implemented_by kimi/kimi-k3 · verified_by kimi/kimi-k3

<!-- relay:entry 20260923T183100Z-3z author=agent kind=comment mode=discuss model=kimi-k3 turn=1ebc6fed9377680339fda5fc25ee16e0/7dba9e6ee501457b986d8a1fb9471ab1 -->
Checking whether the planned pieces actually landed in the code.











































All the pieces check out. Recording the verification on the card and closing it.



**Done: #Y2JW closed.** You're right — it's resolved. I verified against the code and found nothing still needed:

- **`/models` command** — exists and opens the Models pane (`docs/ARCHITECTURE.md:3635`, `src/SettingsPane.cpp:68`).
- **Un-checking models** — the "available" checklist (`models/available` in `src/ModelCatalog.h`) removes a model from the lists, the box, and its filter, per individual model.
- **Add models** — per-provider "+ add a model…" rows persist to `models/custom` (`src/ModelPicker.cpp`).
- **Flexible per-model curation** (your Kimi K3 / GLM 5.3 Flash / GPT Luna example) — the catalog is per-model entries, and it's all global QSettings.

One wrinkle recorded on the card: the plan written here (`models/picker_shown` + a `ModelsDialog`) was actually built and then deliberately retired by card #MDL1 t:a10 in favor of the shipped `models/available` design — different mechanism, same outcome. I logged the full verification as an evidence entry and moved #Y2JW to **done**.
