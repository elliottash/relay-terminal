# #0C0V implementer evidence

The screenshots are from the real Qt `SessionManager` and `InfoView` widgets under Xvfb with an isolated `XDG_CONFIG_HOME`. Their worker events are staged test data, so the pictures check layout and labels without opening the owner's sessions.

- `usage-info.png`: ⓘ renders a turn's input, cached input, latest prompt, guest handover, prefix change and estimated cost, plus a task total including a child.
- `sessions-tokens.png`: the Sessions pane shows a Tokens column and the Most tokens sort.

Checks run in the shared checkout on 2026-09-23:

```
PYTHONPATH=backend python3 -m unittest tests.test_usage_records tests.test_conv_index tests.test_compact_over_tokens tests.test_tool_output_bounds tests.test_subagents
# 206 tests, OK
PYTHONPATH=backend python3 -m unittest tests.test_system_prompt.StabilityTests tests.test_system_prompt.PrefixCheckTests tests.test_system_prompt.ContextBreakdownTests
# 22 tests, OK
scripts/relay-build --target relay-conversations-tests
xvfb-run -a build/relay-conversations-tests infoShowsTaskAndTurnUsage headerClickSortsByThatColumn dateFilters groupRowsSpanTheWidth sessionsTableShowsRecapAndPreviewsOnDemand
# 7 tests, OK
```

`land.py` also configured and built the exact merged source tree at commit `78ab3532` before landing it. A broader `test_system_prompt` run had one known prompt-size budget failure: 9,800 bytes versus its 9,728-byte ceiling. The prior implementer observed the same failure before this GUI work; the 22 stability and breakdown tests above pass.

No live paid-model A/B replay was run for this implementer check. The independent verifier should compare outcomes and token counts on representative tasks before any default threshold or pruning policy is tightened.
