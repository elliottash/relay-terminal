# Model roles: provider names, keyed providers only, a tier per provider (2026-09-18)

Implementer evidence for `issues/changes/needs_qa_llm/2026-09-18-model-roles-provider-names.md`.
Reproduce with `./drive.sh` (Xvfb, xdotool, ImageMagick). Every run is a fresh profile in
`/tmp`, `RELAY_KEYRING=off`, fake `RELAY_*_API_KEY` values and all three proxy variables pointed at
a closed port: no key of the owner's is read and nothing reaches a provider.

| Shot | What it shows |
|---|---|
| `implementer-a-roles-modal.png` | The modal on a Kimi key and a GLM Coding Plan key. Default provider reads **Kimi** — the company, not "Kimi · K3". |
| `implementer-b-default-provider-list.png` | The Default provider list holds **two** entries, Kimi and Z.AI (GLM). The six providers with no key are gone; nothing says "(no key)". |
| `implementer-c-flash-provider-list.png` | The Flash row's provider list: the same two, under "Default provider". |
| `implementer-d-flash-on-zai.png` | After picking Z.AI (GLM) on Flash and typing nothing: **Main · kimi-k3**, **Flash · glm-5.3-flash · on Z.AI (GLM)**. This is the owner's case, in two clicks. Lite has no OpenRouter key here, so it steps to Flash and says so. |
| `implementer-e-two-kimi-plans.png` | With a Kimi Code key as well, the two Kimi entries become **Kimi · Pay-as-you-go** and **Kimi · Coding Plan**; Z.AI, alone in its company, stays plain. |
| `implementer-g-mode-chip-menu.png` | The prompt-box mode chip: **auto** / terminal / agent (was "auto detect"). |
| `implementer-f-input-default-auto.png` | Settings › General › "Default input for new sessions": **Auto** (was "Auto detect"). |

`relay-stderr-*.log` are the worker/GUI logs of the three runs; they are empty or carry only the
usual startup lines, and contain no key material.

Not covered by a screenshot, because it is covered by a test instead: that picking a provider on a
tier row writes `tiers/<tier>/preset` and **no** `tiers/<tier>/model`
(`tests/modelsettings_test.cpp::aTierOffersTheKeyedProvidersAndStoresTheOneChosen`), and that the
worker turns that into the provider's own model for the tier
(`tests/test_roles.py::test_a_tier_that_names_only_a_provider_uses_that_providers_tier_model`).
