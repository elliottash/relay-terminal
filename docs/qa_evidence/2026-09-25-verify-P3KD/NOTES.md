# Verify P3KD — provider groups, defaults in Options (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean-worktree builds). Commit 33205025 is an ancestor.

## Code at HEAD
- `src/RelayWindowModels.cpp:770-815`: provider rows grouped by worker `kind` (plan → Subscription, etc.), order preserved within groups; three headings — in the Models pane "Coding accounts / Subscription keys / API keys and endpoints" (short inventory labels, empty groups skipped), in Options "Guest Agents / Subscription Keys / Pay-as-you-go Keys" (this card's chosen labels live on).
- "fill from defaults" exists only when `m_context.fillFromDefaults` is set (`src/ModelPicker.cpp:354`); only `src/Pane.h:2582` (the Options › Models dialog context) sets it; `src/ModelsPane.cpp` never does — the pane's priorities tab has no fill action.

## Tests (clean worktree)
- `xvfb-run -a ./build/relay-settings-tests -silent`: **51 passed, 1 failed** — the failure is the #E8V1 stale string ("Agent (Alt+Q)" vs "Helper Agent (Alt+Q)", tests/settingspane_test.cpp:1607, filed #SYTR); unrelated here.
- `xvfb-run -a ./build/relay-modelspane-tests -silent`: **25 passed, 1 failed** — same #SYTR cause (tests/modelspane_test.cpp:782).
- Card recorded 48 and 22 passing; both suites have grown since.

## Live (this sweep's drives, same day)
- Sources (the former Providers tab) shows the kind groups: "Coding accounts" with claude code + codex rows (`../2026-09-25-verify-7KPN/01-model-settings.png`, `../2026-09-25-verify-N4PW/10-narrow-sources.png`); relay pro / openrouter under their groups.
- No Defaults controls on Sources or Pick order in any of this sweep's pane captures; the defaults flow lives in the Options models page (covered by the settings suite and the card's own `02-options-defaults.png`).

## Verdict
PASS. Renames since the card (Providers→Sources; pane labels "Coding accounts/…" per the later Sources short-inventory card) do not change the grouping behavior.
