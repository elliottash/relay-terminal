---
id: YC0T
type: work
status: needs-verification
labels: [feature, providers, models, routing]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: cd39c532-430d-4f30-9404-88d05e72e682
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: A second Z.AI / Kimi Code key added in Sources becomes its own preset with its own usage; a quota refusal on one account continues the turn on the other., sign_off: none, effort: medium}
source: Claude guest pane, 2026-09-25
links: {plans: [], commits: [04f22273c929], evidence: [docs/qa_evidence/2026-09-25-yc0t/], related: [M8S2, EQH0, 495G, XH4K, QK2Q], github: null}
---
# More than one Z.AI Coding Plan and Kimi Code subscription, side by side

## Issue
Relay allows one key per provider, so a second Z.AI Coding Plan or Kimi Code subscription cannot be added. Allow extra keyed accounts for those plans the way #M8S2 allows extra Claude Code / Codex logins: each account is its own preset with its own key, usage poll, row in Sources, place in the pick order and quota hold, so routing and quota failover can move between two subscriptions of the same plan.

> we also need to allow multiple subscriptions for z.ai and kimi
> — elliott · [session:eb540dbc335b48608bf140f752a963ce](relay://session/eb540dbc335b48608bf140f752a963ce) · 2026-09-25

## Plan
**Shape.** A *key account* is a name and a key for a built-in subscription preset (`glm-coding`, `kimi-code`), mirroring #M8S2. Registry `$XDG_CONFIG_HOME/relay/key-accounts.json` (`RELAY_KEY_ACCOUNTS`): `{"accounts": [{"id", "preset", "label"}]}`. Its preset id is `<preset>:<id>` (`glm-coding:ethz`); it resolves to the base preset with that id and a label naming the account, and its key is stored in the keyring under that id (`keystore` accepts `<builtin>:<slug>`). The default key stays `glm-coding` / `kimi-code` unchanged.

**Steps.** 1. `backend/relay_core/key_accounts.py`: registry, `is_account_id`, `find`, `as_preset`, `save`/`delete` (delete removes its keyring entry), protocol `key_accounts` / `key_account_save {preset, label, api_key}` / `key_account_delete {id}`. 2. Resolve account ids where custom ids resolve: `roles._preset`, `session_protocol.provider_config`, `presets.resolve_preset`; keystore id rule. 3. Worker `presets`: one row per account (base row's shape, `account`, `account_label`, `base_preset`, key state, its own `limits`). 4. `provider_limits`: poll each account with its own key; `roles._usage_weight`, the provider's refusal classifier and `Agent._spent_reason` read the account's own report (via `config.key_source`). 5. Sources: "add account…" on the two plan rows (name + key); account rows get replace key / test / remove. 6. Protocol doc, tests, live Sources drive.

**Routing.** Accounts rank independently in the five lists. Ordinary failover still skips the same host (an outage takes both); a quota refusal moves to the other account (the quota path already ignores the host), and tied ranks draw by each account's own remaining quota.

**Verify.** Unit tests for registry, id resolution, keyring, rows, per-account polling and quota weighting; a quota-failover test from a spent account to its sibling; live Xvfb drive of Sources adding an account.

## Execution Summary
Landed `04f22273` as planned. `key_accounts.py` (registry, `<plan>:<slug>` ids, save/delete with the keyring entry), resolution in roles / resolve_preset / configure / keytest, keyring id rule, worker requests and one `presets` row per account (plan rows `accounts_allowed`), per-account `provider_limits` poll, quota weighting and refusal `issue.preset` by account. Sources: "add account…" (name, then a masked key) on the Z.AI Coding Plan and Kimi Code rows; account rows with replace key / test / remove. Evidence `docs/qa_evidence/2026-09-25-yc0t/`: 8 new tests (31 with neighbours) incl. a quota refusal on `glm-coding` finishing on `glm-coding:ethz`; the real worker saving an account; a live Xvfb drive adding one.
