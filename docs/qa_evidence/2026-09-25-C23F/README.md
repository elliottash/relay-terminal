# Guest account list and add action

Built `relay` from the isolated `land.py` verification tree for commit `5858d5c1`, then launched it under Xvfb with an isolated home and a small guest account registry. The CLI probes were test fakes; no account was created and no model request was sent.

- `01-providers.png`: default identities remain, with one Add account action at the end of Coding accounts.
- `02-choose-subscription.png`: the footer opens the Claude Code/Codex choice.
- `03-claude-form.png`: Claude Code choice opens the existing account setup form.
- `04-codex-form.png`: Codex choice opens the existing account setup form.

Targeted test selectors passed: `SettingsPaneTests::modelsSourcesScansAccountsThenLocalServersThenProfiles` and `ModelsPaneTests::providersIsWhereCustomizeGoes`.

The full `settings` and `modelspane` cases each had one unrelated existing failure: they expect `Helper Agent (Alt+Q)` while the current UI says `Agent (Alt+Q)`.
