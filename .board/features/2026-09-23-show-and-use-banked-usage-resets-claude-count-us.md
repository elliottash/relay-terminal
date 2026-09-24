---
id: KQNP
type: work
status: needs-verification
labels: [feature, models, guest-harness]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 9af6a7f5-a6df-450e-9953-621582632fbf
rank: zzzzzzzzzzzzzzzzzzzy
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: The picker shows '1 usage reset, use by 22 oct' for a Claude login; /usage-reset in a Codex pane asks first and then reports the outcome., sign_off: none, effort: medium}
source: Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-24-usage-resets-KQNP/], related: [V1VM], github: null}
---
# Show and use banked usage resets: Claude (count + use-by, spend on claude.ai) and Codex (spend from Relay)

## Issue
alllow users to enter a usage reset. all my claude accounts have 1 reset that expires oct 22

the claude ones have to be used on the web site

but the codex ones can be done through the app

## Done means
- A Claude guest's limits carry `resets_available` and `resets_expire_at` read from `api/oauth/usage?cedar_ember=1` (the login's own OAuth token); the picker line reads `· 1 usage reset, use by Oct 22`.
- A Codex guest's limits carry the same two fields (`rateLimitResetCredits.credits[].expiresAt`).
- `/usage-reset` in a Codex guest pane asks for confirmation, then calls `account/rateLimitResetCredit/consume` and reports the outcome (reset / nothing to reset / no credit), then re-reads limits.
- `/usage-reset` in a Claude guest pane spends nothing: it opens claude.ai's usage page, since Claude's resets are used on the website.
- The crontab scanner records Claude resets left + use-by per account.

Verify: unit tests (codex consume + credits expiry, Claude cedar_ember parse, modelcatalog limitsText), a live probe of the cedar_ember endpoint, and a scanner run.

## Execution Summary
Landed `22464f33` (code) and `76b1feb4` (evidence: `docs/qa_evidence/2026-09-24-usage-resets-KQNP/evidence.md`).

- **Claude:** `guest_harness_claude.read_usage_resets` reads `oauth/usage?cedar_ember=1&skip_spend=1` with the login's `.credentials.json` token and the installed CLI's version, because an older User-Agent gets `ineligible_reason: cli_version`. It reads once at start, then at most every 15 minutes on a `rate_limit_event`, and merges `resets_available` / `resets_expire_at` into the `limits` event. `use_usage_reset()` answers `web` with `https://claude.ai/settings/usage`; nothing is spent.
- **Codex:** credits' `expiresAt` becomes `resets_expire_at`. `use_usage_reset(confirmed)` asks first (`confirm`, or `noCredit` when none are left), then calls `account/rateLimitResetCredit/consume {idempotencyKey}` and re-reads `account/rateLimits/read`.
- **Wire:** a `usage_reset` request and event (protocol 29.3 doc), and `resets_expire_at` on `usage_limits` and preset rows. The event is withheld from phones in `remote/wire.py`.
- **Desktop:** `/usage-reset` sends the request. On `confirm` it shows a yes/no box and re-sends with `confirm: true`. On `web` it opens the https URL. The picker and Options line read `· 1 usage reset, use by 22 oct`.
- **Scanner (outside the repo):** `check_usage.py` asks for `cedar_ember` with the installed CLI's User-Agent, and a new column I holds "resets use by". Live run: all 4 Claude accounts show 1 reset, use by 2026-10-22 12:00.

Not exercised live: actually spending a Codex reset (irreversible; the owner's call). The confirmed path is covered by a unit test with a stubbed app-server, and the live unconfirmed path returned the question: "1 left, use by Oct 22".

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_claude_usage_resets tests.test_guest_harness_codex tests.test_guest_harness_provider`: 160 run, and every new test passes. The 3 failures are all in `StartTests.test_session_replacement_preserves_bridge_and_instructions`, caused by another session's uncommitted `board_tools.py` (`'BoardTools' object has no attribute '_case'`).
- `build/relay-modelcatalog-tests`: 70 passed.
- land.py built the exact landed tree (`relay`).
