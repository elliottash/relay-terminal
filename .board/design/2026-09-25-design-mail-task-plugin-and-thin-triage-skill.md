---
id: 1E5F
type: work
status: needs-verification
labels: [feature, skills, mail, design]
assignee: claude-code
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: decision, primary: person, also: [ai-text], human: required, criteria: 'the owner reads docs/MAIL-PLUGIN-DESIGN.md and approves the scope, the BYO setup, the tool contract, the skill contract and the build plan, or names what to change', sign_off: none, effort: medium, stakes: reputation, blast: capability}
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [docs/MAIL-PLUGIN-DESIGN.md], commits: [553dd3ac], evidence: [docs/MAIL-PLUGIN-DESIGN.md], related: [9HS0, MEPR], github: null}
---
# Design mail task plugin and thin triage skill

## Issue
Design a mail task plugin that owns mail API tools and credentials in Relay's keystore, with a thin bundled triage skill on top. Define provider and authentication scope, requirements, onboarding and case-based verification before implementation.

## Done means
A design specifies mail provider/API scope, credential storage and consent, task-plugin tools, the triage skill contract and case-level verification. It identifies what the starter task says before setup and after setup.

## Plan
Refreshed 2026-09-26. Design only; nothing enters the bundle until the design is approved. **Three owner questions** in the thread decide its shape.

**Findings.** No mail code exists in Relay (`backend/`, `src/`). Credentials have a home: `backend/relay_core/keystore.py` (with `key_accounts.py`). Task plugins declare tools, skills and `requires` (`docs/TASK-PLUGINS.md`, `task_plugins.py`; manifests card #C0Q8 executing, #MEPR discussing). The owner's private `email-triage` skill is the working reference (Gmail API, own credentials). #4EMF's release check and Try-it cases apply to the thin skill; #K26R's `secret` requirement is how the skill shows "needs mail account".

**Design deliverable** (a section here plus `docs/MAIL-PLUGIN-DESIGN.md`):
1. Provider and auth scope, per the answers below.
2. Tool contract for `plugins_bundled/mail`: `mail_search`, `mail_read`, `mail_label`/`mail_archive`, `mail_draft`, and `mail_send` only if question 3 allows it, each with its data returned and its side effects.
3. Credential and consent flow: token in the keystore, scopes shown before consent, revoke from Options › Security (#3KB7).
4. The `mail-triage` skill contract: profile (`confidential: yes`, so case rows carry ids, not content), `requires: secret mail-account`, the triage steps, what it may do without asking.
5. Verification: a fixture mailbox (recorded API responses) for the release check; a Try-it case over that fixture; live cases sampled per the QA policy.
6. Starter-task copy before setup ("needs a mail account · Connect Gmail…") and after.

**Verify.** Owner review of the design; a Try-it walkthrough on the fixture once built.

## Decisions
2026-09-26, owner ("I agree with the recs. proceed"): Gmail API only first; bring-your-own OAuth client with guided setup; search, read, label, archive and draft, no send. The design can now be written.

## Execution Summary
Design written: [`docs/MAIL-PLUGIN-DESIGN.md`](../../docs/MAIL-PLUGIN-DESIGN.md) (commit `553dd3ac`), indexed in `docs/README.md`. No plugin or skill code.

- **Scope.** One Gmail scope, `gmail.modify`: the least one that covers label and archive, and it already covers read and draft. No Gmail scope allows drafts without send (`gmail.compose` and `gmail.modify` both permit `messages.send`), so the no-send rule is enforced in Relay's code. There is no send tool, and the client has a literal method allowlist, tested against Gmail's discovery document, that makes send, delete, trash and settings writes unreachable. `mail.google.com` is never requested.
- **BYO client and tokens.** A seven-step guided sheet: Cloud project, enable the API, consent screen (Internal on Workspace; on External, publish to production because Testing tokens expire after 7 days), add the scope, create a Desktop client, review, then loopback + PKCE consent. The client ID, secret and refresh token go in the OS keyring under a new `credential mail/gmail/<slug>` keystore namespace, with no env fallback. The access token stays in worker memory only. The non-secret `mail-accounts.json` holds the address and scopes. Revoke is Options › Security › Mail accounts › Disconnect: it calls Google's revoke endpoint and deletes the local copies.
- **Plugin.** `relay.mail` is a schema-2 manifest that validates today: no runner, empty `requires`, and a `/triage` command. A bundled `MailRuntime` provides six tools: `mail_status`, `mail_search`, `mail_read`, `mail_label`, `mail_archive` and `mail_draft`. The doc lists each tool's inputs, outputs and side effects, the refused system labels, a 500-writes-per-turn budget, a write journal for undo, a rate-limit bucket with backoff, and eight error codes, each with its fix.
- **Skill.** `mail-triage` is thin: it holds no API code and no personal details. It declares `requires: tool mail` plus the named secret `mail-account` (resolved through #K26R's checker). Its profile is `confidential: yes` and `sign_off: none`. It sorts mail into five tiers. Search, read, tier-2 drafts and opt-in `Relay/Triage` labels need no confirmation. Archive, the person's own labels and tier-4 actions need one confirmation per batch. Send, delete, forward and settings changes are never possible.
- **Verification.** A scrubbed, recorded fixture mailbox is served through `FixtureTransport`, and it includes an injected-instruction message. The #4EMF release check covers the skill, and `tests/test_mail_plugin.py` covers the plugin. A Try-it case has a sealed expectation, and every live run writes a confidential case row whose verdict is the person's.
- **Starter copy.** Before setup: "needs a Gmail account · Connect Gmail… (about ten minutes, once)". After setup: "<address> · sorts the inbox and drafts replies; nothing is sent".
- **Build plan.** Seven child-card-sized steps: keystore namespace → Gmail client/OAuth → plugin + tools → connect/revoke UI → skill + fixture + Try-it (after #K26R, #4EMF) → Start recipe + hints (#6VMF) → dogfood, then revisit send. Not filed yet; they get filed on approval.
- **Open for the owner.** Should mail tools be allowed on hosted models (Relay Free) or only on BYOK/local ones? Recommendation: allow, and name the provider at connect time.

## Tests
`PYTHONPATH=backend python3 -m relay_core.task_plugins validate <package holding the doc's manifest>` (ok relay.mail 0.1.0)
`manual: docs/MAIL-PLUGIN-DESIGN.md`: owner review of the design
