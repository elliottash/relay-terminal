---
id: 1E5F
type: work
status: planned
labels: [feature, skills, mail, design]
waiting_on: owner
parent: SZ1H
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
source: 'Owner approval on #SZ1H, 2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [9HS0, MEPR], github: null}
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
