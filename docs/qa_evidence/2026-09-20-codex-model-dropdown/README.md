# #E516 — model selection in Codex options (implementer evidence)

**Card:** `issues/features/2026-09-19-add-model-selection-in-codex-options.md`
**Date:** 2026-09-20 · **Session:** `e516-codex-models` (Relay agent pane)

## What was asked

Options › Claude Code and Codex › Codex › Model was a free-text field; it should be a
dropdown offering 5.6 luna, 5.6 terra, 5.6 sol and 6 astra.

## Root cause (step 1 of the plan: diagnose on this machine first)

The plan guessed a failing scan; on this machine the scan **works**:

```
$ codex --version
codex-cli 0.155.1
$ timeout 30 codex debug models > models.out; echo $?
0            # 393 035 bytes of JSON, ~0.06 s
```

`catalog_rows()` parses it correctly (checked directly against the real output):

```
gpt-6-astra  GPT-6-Astra   default medium  efforts low..ultra  (visibility list)
gpt-5.6-sol  GPT-5.6-Sol   default low     efforts low..ultra  (visibility list)
gpt-5.6-terra GPT-5.6-Terra default medium  efforts low..ultra  (visibility list)
gpt-5.6-luna GPT-5.6-Luna  default medium  efforts low..ultra  (visibility list)
gpt-5.5      GPT-5.5       default medium  (visibility list)   # also listed, not in the fallback
gpt-reserve / codex-auto-review              (visibility hide → dropped)
```

The bug is that the catalogue **never reaches the GUI**: `presets` is answered on the
protocol thread and must not wait for the background scan (by design, tested by
`test_the_first_presets_answer_does_not_wait_for_codex`), so the first answer carries
codex `models: []`; the scan lands ~60 ms later, but nothing re-sends `presets` — the
worker never pushes and Options renders from the pane's cached `m_presets` without
re-asking. The row therefore stays a text field for the pane's whole lifetime even on a
machine where the scan succeeds. A worker-side fallback alone would not have fixed it.

## The change

- `backend/worker.py` — the `presets` emit factored into `emit_presets(request_id=None)`;
  registered as `guest_harness_provider.set_catalog_listener`, so a fresh `presets` event
  is **pushed the moment the scan completes**, whatever it found.
- `backend/relay_core/guest_harness_provider.py` — `_CODEX_FALLBACK_MODELS` (the four
  models above, codex's own priority order, read off codex-cli 0.155.1 on 2026-09-20);
  `guest_models("codex")` serves it **only once a scan has completed empty** (missing,
  refusing, timing-out or unparseable codex). A scan with rows always wins; before
  completion the answer is still `[]`, so the first `presets` answer does not wait and
  nothing flashes a fallback before the real list.
- `src/Pane.h` — the `presets` event handler calls
  `relay::SettingsWatch::instance().notify()`, so a settings pane that is already open
  re-renders when the pushed event lands (text field → dropdown by itself).
- `tests/test_guest_harness_provider.py` — `test_a_codex_that_fails_or_is_missing…`
  becomes `…_gets_the_fallback_menu` (asserts the four slugs; the not-installed half is
  unchanged); new `test_the_scan_landing_is_what_the_worker_pushes_on` pins the listener;
  `test_the_first_presets_answer_does_not_wait_for_codex` now also proves a non-empty
  scan beats the fallback (one row, not four).
- `docs/AGENT-SESSIONS-PROTOCOL.md` §29.3 — the push and the fallback documented.

## Verified by the implementer

- `codex debug models` on this machine: exit 0, parsed by the real reader (above).
- Direct behaviour check in-process: first answer `[]`; failed scan → the four fallback
  models; non-empty scan → its rows win over the fallback; listener fires once on
  completion (`pushes: ['landed']`).
- `PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider.CatalogueTests
  tests.test_guest_harness_provider.WorkerProtocolTests` → **9 tests OK** (targeted only;
  the owner asked for no suite runs).
- `scripts/relay-build` → green, `relay` linked (51 s).

## Not verified by the implementer (owner direction: deliver, no live pass)

The live Xvfb check below is left for QA, per the owner's "no tests, deliver, move to
needs QA".
