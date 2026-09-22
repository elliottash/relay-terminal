# Board model consistency (#BMS1)

`RELAY_KEYRING=off PYTHONPATH=backend:tests python3 -m unittest tests.test_card_model_selection tests.test_board_protocol tests.test_model_switch`: 204 tests passed.
`scripts/relay-build --target relay`: passed.

The regression runs two actual Agent turns through BoardCommands and its per-card supervisor with a recording provider. The first request serves kimi-k3; after changing the main provider, the second serves selected-model on the same card agent with the first question still in history. The tests also check guest and guest-role refusal, busy/queued selection refusal, and unaffected terminal guest selections.

The existing board runner cannot execute guest harnesses (#GH5T). This fix does not enable Codex on cards: it refuses that unsupported selection explicitly, restoring the actual model, instead of showing Astra while the cached card runs Kimi. Native/API models synchronize before the next card turn. A model change while card turns are running or queued is refused, so no old queued turn runs under a newly advertised selection.

Repository-wide board format check: 14 pre-existing errors and 751 warnings; none names BMS1 or its files.

Live Xvfb check: isolated XDG configuration, two fixture cards, loopback provider from the existing card-turns-console driver. Opened a card, asked it to say hello (thread and transcript identify `stub`), then entered `/model codex`. `refused.png` / `refused.txt` show the explicit Codex refusal and the picker restored to `stub`; the thread's answer remains intact. The prerequisite switch to Main also refused its guest default (Claude Code), correctly leaving the native helper selected.

`TestsCommands.check_card('BMS1')`: no blocks or findings (the combined unittest command is not resolved into registered IDs by discovery). The direct test execution above is the verification evidence.
