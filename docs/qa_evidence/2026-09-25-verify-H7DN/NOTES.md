# Verify H7DN — hide hosted model identities (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean worktree). Commit 00872ae4 is an ancestor.

## Code
- `backend/relay_core/presets.py:267,281`: hosted presets carry public service names ("relay free", "relay pro"); role naming at :621-623 maps to the service. GLM appears only in capability comments (windows, thinking), never as a shown name.
- The C++ catalog maps hosted rows to service roles before drawing (`src/ModelCatalog.cpp` hosted-name replacement, per Execution Summary).

## Tests
- `python3 -m unittest tests.test_presets tests.test_model_ranking tests.test_roles tests.test_relay_pro` — **183 of 185 pass**; the 2 failures are the known GuiMirror drift (src/Pane.h mirror vs backend tables) filed as #DX4A, unrelated to hosted naming.
- `./build/relay-modelcatalog-tests -silent` — **73 passed, 0 failed** (card: 68; grew).
- `relay-modelpicker-tests` / `relay-modelspane-tests` — run earlier this sweep in the same clean worktree: 62/0 and 25/1 (the 1 is the #E8V1 stale string, #SYTR).

## Live (this sweep's drives)
- The Relay Pro provider row reads "Enter your Relay Pro access code. per-person access through relay's hosted service" — no model names (`../2026-09-25-verify-BXMS/05-after-dismiss.png`; also visible in the Sources captures `../2026-09-25-verify-7KPN/01-model-settings.png`).
- Relay Free role names in pickers: RELAY_HOSTED=off in these drives hides hosted models, so this side is covered by the implementer's `docs/qa_evidence/2026-09-23-hosted-names/02-free.png` plus the green catalog/picker suites.

## Verdict
PASS.
