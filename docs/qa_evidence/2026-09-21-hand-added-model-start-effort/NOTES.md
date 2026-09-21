# Where a hand-added model starts (card #TKN7)

Owner, 2026-09-21: *"in the 'main models' option, when i added gpt 5.6 sol, it defaulted effort to
ultra reasoning, it should be medium. review more broadly how those defaults are set. is there a
provider default we can use?"* Answered on the card: use **the provider's own default**.

## What was wrong

`Options › Models › + add a model…` handed `addToTier` the model's **top** reasoning level for every
one of the five lists (`src/RelayWindow.h`, one line). gpt-5.6-sol is `guest:codex|gpt-5.6-sol`,
whose levels are codex's own `low · medium · high · xhigh · max · ultra`, so `.last()` was `ultra`.

The `defaults` buttons never did that: `tier_list_defaults` (`backend/relay_core/presets.py`) starts
**main** at the provider's own default level, **high** at the plan-turn level (`xhigh` on codex, per
the owner's 2026-09-20 rule), and **flash**/**lite** at the lowest. Only the by-hand button used the
top level, and it used it for all five lists.

## What was changed

One rule, in the backend, consumed by the GUI:

- `presets.tier_start_efforts(efforts, default_effort, guest_id)` → `{main, high, flash, lite}`, the
  same three rules `tier_list_defaults` already used.
- Every `models` row now carries `default_effort` (the provider's own default for that model: codex's
  `default_reasoning_level`, or the `infer_effort` of a cloud model's built-in tier extra) and
  `tier_effort` (the four start levels). Cloud rows from `presets.catalog_rows()`, guest rows from
  `guest_harness_provider.guest_models()` — the single choke point for guest rows, codex's scan and
  its fallback menu alike. Protocol: `docs/AGENT-SESSIONS-PROTOCOL.md` 13.8 and 29.3.
- `src/ModelCatalog.{h,cpp}`: `Entry::defaultEffort` and `Entry::tierEffort`, parsed from those keys,
  and `models::tierStartEffort(entry, tier)` — the row's own answer, or the same three rules read off
  the entry when an older worker sends none.
- `src/RelayWindow.h`: the add button calls `models::tierStartEffort(entry, tier)`.
- `docs/ARCHITECTURE.md`: the five-lists paragraph says where a hand-added row starts.

A level the provider states but the model does not offer is stored as no level at all (the row's
`default` option): the model's own default then applies at run time.

Not touched: `src/ModelPicker.cpp`'s two `efforts.last()` uses. Those are the *display* of the level
the pane would run a not-yet-picked model at, after the five lists and the pane's current level are
both unavailable — a different question from where a new list entry starts.

## Evidence

| Run | Result |
|---|---|
| `ctest -R '^modelcatalog$|^modelpicker$'` — `1-ctest-modelcatalog-modelpicker.log` | 2/2 passed |
| `python3 -m unittest tests.test_tier_lists tests.test_presets tests.test_guest_harness_provider tests.test_guest_harness_codex` — `2-python-tests.log` | 203 tests, OK |
| The real codex on this machine through the worker's own path — `3-live-codex-catalog.log` | `gpt-5.6-sol default=low tier_effort={'main': 'low', 'high': 'xhigh', 'flash': 'low', 'lite': 'low'}` |
| `scripts/relay-build --target relay` | built (main.cpp recompiled with the new `RelayWindow.h`) |

New tests pin the rule on both sides: `tests/test_tier_lists.py::StartEffortTests`,
`tests/modelcatalog_test.cpp::aHandAddedModelStartsAtTheLevelTheWorkerNamed` and
`withoutTheWorkersAnswerTheRowItselfDecides`, and the codex fallback assertions in
`tests/test_guest_harness_provider.py`.

## What still wants a pair of eyes

The page itself. The level a new row shows is drawn by code this card did not change, but nobody has
looked at Options › Models since: open it, add Codex's **gpt-5.6-sol** to **main** and it should read
`low`, to **high** and it should read `xhigh` (not `ultra`); add a cloud model to **lite** and it
should read `low`. That is the `## QA checklist` on the card.
