---
id: P3KD
type: work
status: needs-verification
labels: [feature, models]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in Relay, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-provider-groups/], related: [], github: null}
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
