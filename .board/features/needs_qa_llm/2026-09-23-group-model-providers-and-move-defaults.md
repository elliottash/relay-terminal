---
id: P3KD
type: work
status: needs-qa-llm
labels: [feature, models]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in Relay, 2026-09-23
links: {plans: [], commits: [332050253c018edeedbc32a903e3fe30ef8e530d], evidence: [docs/qa_evidence/2026-09-23-provider-groups/, docs/qa_evidence/2026-09-25-verify-P3KD/], related: [], github: null}
---
# Group model providers and move defaults

## Issue
in providers, put 3 sections -- what do you think about these labels? 

Guest Agents

Subscription Keys

PAYG keys

also put profiles at the top 

for defaults, i feel like that should be in options.

## Decisions
Use “Guest Agents”, “Subscription Keys”, and “Pay-as-you-go Keys” for clarity; keep Profiles above them. Defaults belong in Options.

## Done means
The Providers tab shows Profiles first, followed by three account-type sections with each provider in the right one. Defaults controls are absent there and on Priorities, and can be used from Options. Existing keys, profiles, and model rankings keep working.

## Plan
1. Group existing provider rows by provider kind, preserving order within each group.
2. Put Profiles before provider groups and show defaults only in Options; move fill-from-defaults actions there.
3. Update focused tests, build, and capture the visible layout.

## Execution Summary
Profiles is first on the Providers tab. Existing provider rows are grouped by worker `kind` into Guest Agents, Subscription Keys, and Pay-as-you-go Keys, preserving the saved order inside each group. Options retains the shared provider and profile controls, plus Defaults; its Model priority lists action fills from the served pane's worker defaults. The Models pane's Priorities tab no longer offers that action.

![Profiles and Guest Agents on Providers](docs/qa_evidence/2026-09-23-provider-groups/01-providers.png)

![Defaults action in Options](docs/qa_evidence/2026-09-23-provider-groups/02-options-defaults.png)
![Subscription and Pay-as-you-go sections](docs/qa_evidence/2026-09-23-provider-groups/01b-provider-keys.png)

## Tests
`scripts/relay-build --target relay-settings-tests relay-modelspane-tests relay` — pass.
`QT_QPA_PLATFORM=offscreen ./build/relay-settings-tests` — 48 passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-modelspane-tests` — 22 passed.
`docs/qa_evidence/2026-09-23-provider-groups/drive.sh` — live isolated Xvfb capture of Providers and Options.

## QA checklist
Verified 2026-09-25 by a verifying session at rev `2db966438ab8bc61e0f9f1ff89a51853286ad0f8` (clean-worktree builds). Evidence: `docs/qa_evidence/2026-09-25-verify-P3KD/`.

**Done means:**
- Providers (now Sources) shows Profiles first, then three account-type sections with each provider in the right one — **passed with a rename note**: kind-grouping confirmed in code (`RelayWindowModels.cpp:770-815`) and live ("Coding accounts" holding claude code + codex, subscription/API groups below). The pane's headings were later relabeled ("Coding accounts / Subscription keys / API keys and endpoints"); the card's own labels survive on the Options side ("Guest Agents / Subscription Keys / Pay-as-you-go Keys"). Profiles: the later Sources redesign keeps profiles on the Options models page rather than the pane's Sources tab — grouping behavior intact, location moved by later cards.
- Defaults controls absent from Providers/Priorities, usable from Options — **passed**: "fill from defaults" renders only when the context supplies it (`ModelPicker.cpp:354`), only the Options context does (`Pane.h:2582`); none of this sweep's Sources/Pick-order captures show defaults controls.
- Existing keys, profiles, rankings keep working — **passed** (fresh-profile drives this sweep carried over nothing; the settings suite's profile/tier tests green).

**Tests, line by line:**
- `relay-settings-tests` — **passed with a note**: 51/1; the 1 is the #E8V1 stale string (#SYTR), unrelated.
- `relay-modelspane-tests` — **passed with a note**: 25/1, same #SYTR cause.
- Live capture `docs/qa_evidence/2026-09-23-provider-groups/` (incl. drive.sh) — **present**, re-confirmed live this sweep.

Unresolved: nothing for this card.

Reviewed 2026-09-25 by the verifying session (qa-verify-P3KD), rev `2db96643`.
