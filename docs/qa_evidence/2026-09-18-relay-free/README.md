# Relay Free on the desktop — implementer evidence (2026-09-18)

Phase 3 of the hosted-inference plan (`issues/features/2026-09-18-relay-free-hosted-inference.md`,
`docs/RELAY-FREE.md`): a fresh install with no provider key lands on Relay Free, says once where its
prompts go, streams a reply, shows the day's allowance, and when the allowance is used up says what
to do instead while the terminal keeps working.

Driven by `drive.sh` under Xvfb with an isolated HOME, `XDG_*`, `XDG_RUNTIME_DIR` and `TMPDIR`,
`RELAY_KEYRING=off`, `--clean-shell --fresh`, and no key stored. The gateway is `fake-gateway.py`
(`tests/test_hosted.py`'s `FakeGateway`) on 127.0.0.1, reached through `RELAY_HOSTED_URL`; nothing
here touches `api.relay-terminal.ai`. Relay Dark, 1280×800.

    docs/qa_evidence/2026-09-18-relay-free/drive.sh [build-dir] [free|byok]

| Screenshot | What it shows |
|---|---|
| `implementer-00-fresh-install-lands-on-relay-free.png` | No `[provider]` in the config and nothing in the keyring: the pane configured itself on Relay Free (model chip "Relay Free", not "relay-main"), printed the one-line disclosure in the transcript, and the strip carries the "Free" chip with no figure yet. No modal, no "No stored provider keys" toast. |
| `implementer-01-an-ask-streamed-and-the-chip-shows-the-allowance.png` | `*say hello` went through the gateway and streamed `FREE_OK`. The `hosted_quota` event after the call set the chip to "Free · 99% left" (1,200 of 250,000 tokens; rounded down, so one call spent never reads as 100%). The disclosure was not printed again. |
| `implementer-02-api-keys-modal-with-the-included-group-first.png` | Options › Models › API keys… (via the palette): the new **Included** group sits above Subscriptions / Aggregator / Pay-as-you-go; the Relay Free row's Key column reads "Included · 76% left today" from the live quota; with that row selected, Add / replace… and Remove are disabled, Test is enabled, and the link button reads "About Relay Free…". The header hint gained its Relay Free sentence. The chip followed the same quota. |
| `implementer-03-allowance-used-up-the-line-and-the-dialog.png` | The fake gateway switched to 429 `quota_exhausted`. The next ask ends with the red line "Relay Free allowance used for today; resets at 22:21. Add a key under Options › Models › API keys… to keep going." (no other key is stored, so no provider list), the chip drops to "Free · 0% left" in the warn tint, the pane wears the failed glyph as for any provider error, and — since nothing of the user's own is stored — the dialog in the `offerVoiceKey` shape offers "API keys…" and "Import from Warp". |
| `implementer-04-after-cancel-a-shell-command-still-runs.png` | After Cancel: `echo the shell still runs $((6*7))` runs in the terminal as usual ("Shell ready · exit 0"); the agent queue is paused as for any provider error. |
| `implementer-05-with-a-key-of-your-own-relay-free-is-not-chosen.png` | The same fresh profile with `RELAY_KIMI_API_KEY` set: the pane lands on `kimi-k3`, not Relay Free; no disclosure line, no Free chip. A user with any key of their own is untouched. |

Not shown: the `tests/modelsettings_test.cpp` cases that pin the modal's behaviour (Included group
first, disabled Add/Remove and enabled Test on the hosted row, status text from `quota` and from
`hosted_quota`, "About Relay Free…", the unavailable row saying "Needs python3-cryptography" and
being offered nowhere, and the roles modal offering Relay as a provider without "(no key)").
