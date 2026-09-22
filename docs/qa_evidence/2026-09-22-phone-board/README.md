# Phone Board refresh — PBXC

`RELAY_KEYRING=off RELAY_BOARD_SHOTS=/tmp/phone-board-shots python3 -m unittest tests.test_board_view tests.test_web_theme`

34 tests passed. Real Chromium rendering, with a fixture transport: checks existing Board actions, offline writes and draft restoration, phone/tablet/keyboard layouts, generated theme tokens, multiline growth, full-width prompt and keyboard actions. Inspected phone and landscape-keyboard screenshots. The transport contract is tested; this is not a live paired-device test.

Screenshots alongside this file show portrait, expanded prompt, landscape keyboard and iPad. The landscape image simulates Safari's smaller visual viewport, so the area below 185px is outside the visible keyboard strip.

The global `scripts/relay-board.py check` reports 12 errors and 749 warnings elsewhere in the shared board; none names PBXC. Hosted app deployment has not been performed.
