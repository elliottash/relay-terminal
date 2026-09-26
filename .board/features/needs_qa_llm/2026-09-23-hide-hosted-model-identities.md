---
id: H7DN
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in Relay, 2026-09-23
links: {plans: [], commits: [00872ae49dbe618f33cd9fa656059fcd92b67dc5], evidence: [docs/qa_evidence/2026-09-23-hosted-names/, docs/qa_evidence/2026-09-25-verify-H7DN/], related: [P3KD], github: null}
---
# Hide hosted model identities in Relay

## Issue
in the relay pro option, dont say waht models it is. check that the models used for relay pro and relay free arent shown in the app

## Done means
The Relay Pro provider option does not name its backing models.
Relay Pro and Relay Free appear in model selection and status surfaces by their service and role names, without revealing backing provider model names.
The internal model IDs and hosted routing remain unchanged.

## Plan
**Goal:** show service roles, rather than underlying model identities, wherever the app names Relay Pro or Relay Free models.

**Findings:** `presets.py` sends GLM names for Relay Pro catalog rows and a GLM description for its provider row. `model_name` also feeds worker events; the C++ catalog renders those names in pickers and status lines.

**Steps:** 1. Give hosted models stable public role names and remove the provider description's model names. 2. Keep internal ranking and routing based on their existing IDs and scores. 3. Check the model picker, pane box, provider option, and worker event names with focused tests and a live isolated capture.

**Risks:** changing a public name also affects catalog grouping and ranking if display and internal names are not separated.

**Verify:** focused preset/catalog/picker tests, app build, isolated UI capture.

## Execution Summary
Relay Pro's provider description no longer names its backing models. Hosted catalog rows and worker event names use public service roles such as `relay pro · main` and `relay free · flash`; unexpected hosted model IDs fall back to the service name. The C++ catalog also replaces a stale worker's hosted name before drawing it. Internal model IDs, ranking names, and hosted routing stay unchanged. Hosted roles have their own picker groups instead of folding into a direct provider's model row.

![Relay Pro provider row without a model name](docs/qa_evidence/2026-09-23-hosted-names/03-pro-row.png)

![Relay Free roles in Available and the pane's model box](docs/qa_evidence/2026-09-23-hosted-names/02-free.png)

## Tests
`python3 -m unittest tests.test_presets tests.test_model_ranking tests.test_roles tests.test_relay_pro` — 176 passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-modelcatalog-tests` — 68 passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-modelpicker-tests` — 51 passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-modelspane-tests` — 22 passed.
`scripts/relay-build --target relay-modelcatalog-tests relay-modelpicker-tests relay-modelspane-tests relay` — app built.
`docs/qa_evidence/2026-09-23-hosted-names/drive.sh` — isolated Xvfb capture.

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean-worktree builds). Evidence: `docs/qa_evidence/2026-09-25-verify-H7DN/`.

**Done means, item by item:**
- The Relay Pro provider option does not name its backing models — **passed** (live: "Enter your Relay Pro access code. per-person access through relay's hosted service", this sweep's `../2026-09-25-verify-BXMS/05-after-dismiss.png`; presets carry only service names).
- Relay Pro/Free appear by service and role names, no backing provider model names — **passed** (code: public role naming in `presets.py`; suites green; Relay-Free picker side per the implementer's capture `02-free.png` — my live drives run RELAY_HOSTED=off so hosted rows don't list).
- Internal model IDs and hosted routing unchanged — **passed** (ranking/routing tests in the green set; IDs untouched per `test_model_ranking`).

**Tests, line by line:**
- `python3 -m unittest tests.test_presets tests.test_model_ranking tests.test_roles tests.test_relay_pro` — **passed with a note**: 183/185; the 2 failures are the pre-existing GuiMirror drift (#DX4A), not hosted naming.
- `relay-modelcatalog-tests` — **passed**: 73/0 (card: 68).
- `relay-modelpicker-tests` — **passed**: 62/0.
- `relay-modelspane-tests` — **passed with a note**: 25/1, the #E8V1 stale string (#SYTR).
- Build + implementer's isolated capture — **present** (`docs/qa_evidence/2026-09-23-hosted-names/`, incl. drive.sh).

Unresolved: nothing for this card.

Reviewed 2026-09-25 by the verifying session (qa-verify-H7DN), rev `2db96643`.
