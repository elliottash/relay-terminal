<!-- relay:entry 20260922T020000Z-b1 author=codex kind=progress -->
Claimed the focused model mismatch fix following owner instruction "fix that+". Board MCP tools unavailable; file fallback. Related #CTRN and #MDL1 work is preserved.

<!-- relay:entry 20260922T025400Z-b2 author=codex kind=evidence -->
204 targeted Python tests pass and relay builds. Live isolated Xvfb card answered on stub, then /model codex was explicitly refused and the picker returned to stub. Evidence: docs/qa_evidence/2026-09-22-board-model-selection/. tests_check has no blocks/findings; repository board format failures are pre-existing and do not name this card. Landed in needs-verification for independent review. Codex guest execution in card turns remains unsupported, now explicitly reported instead of a false selection.

<!-- relay:entry 20260922T025700Z-b3 author=codex kind=note -->
Implementation committed as 86252f3ec00045d5437e708ca79870c0254a2948 after the exact proposed tree built successfully through land.py. Protocol documentation records the helper refusal exception.
<!-- relay:entry 20260925T224649Z-0l author=agent kind=note -->
Verification (rev 2db96643, clean worktree): PASSED by test evidence — test_card_model_selection and test_model_switch green; test_board_protocol's 2 failures are pre-existing scaffold drift (#42G1, second instance noted there today), its model-selection cases pass. Live agent turns not driven (keyless rig). Evidence: docs/qa_evidence/2026-09-25-verify-BMS1/. Moved needs-verification → needs-qa-llm.

<!-- relay:entry 20260926T012110Z-gv author=agent kind=event mention=ZPSG model=gpt-6-sol pane=97e9802a turn=6fdd14d97d384fdf8c839e64e9916e0c/ec6eebbe47cd42049bf248613267f817 -->
mentioned in #ZPSG · 2026-09-26 · agent
