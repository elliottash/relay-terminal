---
id: C23F
type: work
status: needs-verification
labels: [feature, models, providers]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 2771b0b3-bc51-435a-9caa-2c5497ee1ab2
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: visual, primary: probe, also: [script], human: optional, criteria: Both default identities and one Add account footer are visible; the footer opens the chosen CLI setup dialog., sign_off: none, effort: low}
source: Owner in Relay, 2026-09-25
links: {plans: [], commits: [5858d5c1c7ed, ec9412a02594], evidence: [docs/qa_evidence/2026-09-25-C23F/], related: [21KQ, M8S2], github: null}
---
# Clarify default guest accounts and move Add account below list

## Issue
Show the identity of each default Claude Code and Codex login, keep each as a selectable account, and place one Add account action after the Guest Agents list.

> so should we remove the first two? and put add account at the bottom?
> — elliott · [session:6a5b30a50b8f4918a5c207e1711a4232](relay://session/6a5b30a50b8f4918a5c207e1711a4232) · 2026-09-25

## Done means
- Both default subscription logins remain selectable and show their account identity where known.
- Guest Agents has one Add account action after the account rows in Options and Models.
- The action lets the user choose Claude Code or Codex and opens the existing account setup dialog; no account is removed or created until that dialog is submitted.

## Plan
**Goal.** Make each subscription account legible while offering one account creation control.

**Findings.** `src/RelayWindowModels.cpp` builds guest rows and groups them into Guest Agents. Default rows already own the setup dialog callback at button index 2.

**Steps.** 1. Put known email in each default row label. 2. Remove Add account from default row buttons, save the existing callbacks, and add one footer action that chooses the CLI. 3. Build and verify the providers view and chooser in an isolated profile.

**Risks.** Preserve the default login rows because they are real subscriptions and participate in routing. Keep the footer inside the Guest Agents group in both views.

**Verify.** Build Relay and run the targeted Models pane test; inspect the resulting rows and setup dialog under Xvfb.

## Tests
`relay-settings-tests modelsSourcesScansAccountsThenLocalServersThenProfiles`
`relay-modelspane-tests providersIsWhereCustomizeGoes`
manual: docs/qa_evidence/2026-09-25-C23F/

The targeted selectors passed. Full `settings` and `modelspane` cases each had one unrelated existing text expectation failure: `Helper Agent (Alt+Q)` versus the current `Agent (Alt+Q)`.

## Execution Summary
Kept the default Claude Code and Codex rows as selectable subscriptions and appended each known email to its row label. Removed Add account from those rows and added one footer action in the Guest Agents/Coding accounts group. The footer chooser reuses the existing account setup dialog for either CLI.

Code: `5858d5c1`. Evidence: `ec9412a0` and `docs/qa_evidence/2026-09-25-C23F/` (provider list, chooser, Claude form, Codex form).

The app compiled in the isolated verification tree. Full `settings` and `modelspane` ctest cases each have one preexisting text expectation mismatch for the Helper Agent button; targeted selectors passed.
