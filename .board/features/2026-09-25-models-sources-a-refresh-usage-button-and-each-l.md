---
id: EQH0
type: work
status: needs-verification
labels: [feature, models, ui]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: cd39c532-430d-4f30-9404-88d05e72e682
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [probe], human: optional, criteria: 'tests/test_usage_refresh.py passes; in a real Relay, Sources shows each login''s email and refresh updates the usage figures.', sign_off: none, effort: low}
source: Claude guest pane, 2026-09-25
links: {plans: [], commits: [18c575d1ca65], evidence: [docs/qa_evidence/2026-09-25-eqh0/], related: [WBFM, XH4K, M8S2, KQNP], github: null}
---
# Models › Sources: a refresh-usage button, and each login's email on its row

## Issue
Sources shows usage only as fresh as the last 15-minute poll or guest turn; add a button that polls every subscription's usage now (Claude/Codex logins and accounts, Z.AI Coding Plan, Kimi Code). Also show the email each Claude Code / Codex login is signed in as on its row, including the default logins and the already-registered accounts, read from the login's own local credentials.

> add a refresh button on the sources page that updates usage. 
>
> add emails to the accounts that are already in there
> — elliott · [session:eb540dbc335b48608bf140f752a963ce](relay://session/eb540dbc335b48608bf140f752a963ce) · 2026-09-25

## Execution Summary
Landed `18c575d1`. Worker `usage_refresh {id?}` runs `guest_usage_poll.poll_once` and `provider_limits.poll_once` on a thread, then `usage_refreshed {id, at}`. `guest_harness_provider.login_email` reads Codex `auth.json` id_token `email` / Claude Code `.claude.json` `oauthAccount.emailAddress` (mtime-cached); guest preset rows carry `email`. Sources: "usage · refresh" row above the groups, "logged in as <email>" on guest rows, email in the row's search aliases. Protocol doc updated. On this machine: Claude elliott.ash@gess.ethz.ch, Codex e@elliottash.com, account ashe@ethz.ch. Evidence: `docs/qa_evidence/2026-09-25-eqh0/` (live Xvfb drive: emails shown, refresh sent, Codex weekly redrawn 20% → 88% left; 6 unit/worker tests).

## Done means
- Models › Sources has a "usage · refresh" row above the provider groups; clicking it sends `usage_refresh`, the worker polls every subscription (Claude Code / Codex logins and accounts, Z.AI Coding Plan, Kimi Code) at once, and the rows redraw with the new figures.
- Every Claude Code / Codex row — default logins and registered accounts — says "logged in as <email>", read from the login's own files; nothing is fetched or logged.
