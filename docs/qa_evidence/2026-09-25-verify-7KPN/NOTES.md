# Verify 7KPN — Add provider shows provider names (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8. Both claimed commits (07cb34a3, 8d9f24cf) are ancestors. The landed code survives verbatim at HEAD in `src/RelayWindowModels.cpp:460-487` (`providerName` = provider field, name part before " (", lower-cased; picker rows carry no model-name note; `askForKey` gets the provider name) — it moved there from RelayWindow.h in #243T's split (f6480574).

## Live drive (Xvfb :97, isolated XDG_CONFIG_HOME/RELAY_WORKSPACE)
- `01-model-settings.png`: the "+ add provider" row reads "a custom endpoint, or kimi, z.ai, minimax, openai, anthropic, google, deepseek" — provider names, no model names.
- `04-picker.png`: the picker lists kimi (pay-as-you-go / coding plan), zai (standard api / coding plan), minimax (token plan), openai / anthropic / google / deepseek (pay-as-you-go) — provider + plan columns only; no model names anywhere.
- `05-key-prompt.png`: choosing "kimi pay-as-you-go" opens "Key for kimi / It is saved to the desktop keyring…" — the provider's name, not a model's.
- Note from the drive: a stray "Add claude code account" modal (my own misclick) blocks the picker until dismissed — behavior consistent with a modal, not a defect.

## Verdict
PASS. Build: `scripts/relay-build --target relay` green earlier this sweep (the card's only build check).
