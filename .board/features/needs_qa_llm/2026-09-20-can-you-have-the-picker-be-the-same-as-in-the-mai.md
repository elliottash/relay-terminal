---
id: PK5Q
type: work
status: needs-qa-llm
labels: [feature, models, switchboard]
assignee: claude-code
rank: m
created: '2026-09-20'
source: 'owner, terminal, 2026-09-20'
links: {plans: [], commits: [78143918, 3669df02, cddde462, b193f83c, eb45e571, 7226236a, 8984840c], evidence: ['docs/qa_evidence/2026-09-20-helper-picker-like-the-pane/', 'docs/qa_evidence/2026-09-25-verify-PK5Q/'], related: [BRD3, FEJQ, PBX1, GH5T], github: null}
---
# The helper agent's model box is the pane's model box

## Issue
can you have the picker be the same as in the main terminal

## Notes
The helper agent's boxes — the Switchboard agent's panel, the card page's reply box and the
Options / Actions / Sessions panels — drew their own short list (Follow Main, Flash, Lite, the
usable providers, "⚙ Model roles…"). A terminal pane's box draws the role rows, the per-model
catalog, "more models…" and the gear. Two lists over one idea.

## Tasks
- [x] one row builder for both boxes <!-- t:r1 -->
- [x] one pick path: role rows, catalog entries, the picker dialog, the gear <!-- t:r2 -->
- [x] Alt+M / Ctrl+Alt+M / /model in a helper composer <!-- t:r3 -->
- [x] live evidence, both popups side by side <!-- t:r4 -->

## Decisions

- **A guest harness keeps its row in a helper's box, and the pick is refused.** The owner asked
  for the same list, so hiding the row would have made the two lists differ in the one place the
  card is about. The refusal is the sentence `guest_harness_provider.helper_refusal` already
  writes for card #GH5T, said before anything is stored, so the worker never has to raise it.
- **The Lite row is gone from a helper's box.** The pane's role rows are Main, Flash and — where
  this machine serves one — Local; "the same as in the main terminal" is those three. Lite is one
  of the five lists on Options › Models, which is where the tiers have lived since this morning.
- **The role value did not have to grow.** Protocol 13.7 has carried `preset` + `model` + `effort`
  since the roles modal; what was missing was a writer that names all three, which is
  `RolesDialog::writeRoleEntry`. An old value that names only a provider still means that
  provider's own model (`tests/test_roles.py`).

## Tests

- `ctest --test-dir build -R '^modelrows$'` — the one list: order, the Main row's entry never
  repeated, the exhausted mark, guest rows, `/model <words>`
- `ctest --test-dir build -R '^helpermodelbox$'` — a helper's box is a pane's box, row for row;
  which row a resolved role sits on; the panel's model box and its slash commands
- `ctest --test-dir build -R '^board$|^boardpane$|^settings$|^conversations$|^modelpicker$|^modelcatalog$|^modelsettings$'`
- `PYTHONPATH=backend RELAY_KEYRING=off python3 -m unittest tests.test_roles`
- `manual: docs/qa_evidence/2026-09-20-helper-picker-like-the-pane/`

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean-worktree builds). Evidence: `docs/qa_evidence/2026-09-25-verify-PK5Q/`. **Context: this card's surface was later retired** — #AGNT step 9 (`db186adc`) deleted the helper's panel, its model box and `tests/helpermodelbox_test.cpp`. The shared row builder this card created (`relay-modelrows`, `src/ModelRows.cpp`) survives and is what the pane's box draws.

Original manual items, as verifiable today:
- Both boxes (Switchboard beside a pane) list the same rows — **not applicable / retired**: the helper's panel no longer exists (#AGNT). The pane side is confirmed live: `01-pane-modelbox-altm.png` (Alt+M: role rows main/flash, all models, model settings, active model).
- Pick a catalog model in the helper's box; Options › Models agrees — **retired surface** (#AGNT); the underlying pick path is covered by `modelrows` (PASS).
- "more models…" opens the same dialog (Ctrl+Alt+M) — **passed by test evidence** on the pane side (`modelrows`, `modelpicker` PASS); helper-side retired.
- Claude Code / Codex row refused with one sentence — **passed by test evidence** (`modelrows` guest-row cases PASS); helper-side retired.
- `/model`, `/model <name>`, `/models` — **passed by test evidence** (`modelrows` `/model <words>` case PASS; `modelsettings` PASS).
- Options in a fresh tab still lists models — **passed** (live drives this sweep show Options › Models populated from a fresh profile).

Named test lines:
- `ctest -R '^modelrows$'` — **passed**.
- `ctest -R '^helpermodelbox$'` — **missing**: deleted by `db186adc` (#AGNT).
- `ctest -R '^(board|boardpane|settings|conversations|modelpicker|modelcatalog|modelsettings)$'` — **passed except settings + conversations**, both failing only on the #E8V1 rename fallout (#SYTR), unrelated to this card.
- `PYTHONPATH=backend RELAY_KEYRING=off python3 -m unittest tests.test_roles` — **passed** (OK).

Unresolved: nothing for this card's own code. The retirement of its subject is #AGNT's (also in needs-verification); the stale #E8V1 tests are #SYTR's.

Reviewed 2026-09-25 by the verifying session (qa-verify-PK5Q), rev `2db96643`.
