# The helper's app tools stopped being deferred (#GMCF decision 9)

Decision 9 (`19f22e3d`) holds the `app`, `own_session` and `tests` schemas back until the model asks
for them with `load_tools`. That is a pane's bargain: the app tools are on every request and a
minority of a pane's turns use them. The tab helper is the other side of it — it is the agent
Options, Actions and Sessions ask, so its first action is an app call every time — and
`_deferred_groups` keyed "eager" on `board.card_scope`, which the helper only has *while a turn is
running and a project is attached*. Two things followed.

- **A tab with no project attached has no board** (30.7), so its helper took the ordinary pane
  branch of `Agent.tools()` and lost the nine app tools it exists for, behind a round trip.
  `tests/test_board_chat.BoardlessHelperTest::test_the_builder_makes_a_board_less_agent_that_keeps_the_app_tools`
  failed on `main` from `19f22e3d` until this fix.
- **The helper with a board** was built before any `ChatScope` existed, and `messages[0]` is written
  once: its prompt carried the one-line `load_tools` rule, and lost `app_tools.prompt_section`, while
  `ChatScope` handed its turn the app schemas anyway and offered no `load_tools` tool at all. The
  model was told to call a tool that was not in its list, to get schemas it already had.

The fix is one flag. `Agent.__init__` takes `helper=`, `board_protocol._build_page_agent` passes it,
and `_deferred_groups()` returns `()` for a helper in every scope. Ordinary panes are untouched.

## Numbers

`measure.py` builds the two helpers through `_build_page_agent` and an ordinary pane beside them,
measures each one's tool list and the prompt in `messages[0]`, and sends nothing anywhere.
"first action" is the bytes the model reads before it can call `app_action_run`: one request when the
schemas are there, two when they are not.

    PYTHONPATH=$PWD/backend python3 docs/qa_evidence/2026-09-20-perf-fixes/helpertools/measure.py

Before (`before.txt`, a clean `git archive` export of the tip this landed on):

    helper, no project           11 tools    7494 B  prompt   3222 B  app tools DEFERRED  prompt says load_tools: yes  tool offered: yes  first action ~18210 B
    helper, project attached     24 tools   21087 B  prompt   7286 B  app tools eager     prompt says load_tools: yes  tool offered: no   first action ~28373 B
    ordinary pane                12 tools    8946 B  prompt   4366 B  app tools DEFERRED  prompt says load_tools: yes  tool offered: yes  first action ~22258 B

After (`after.txt`):

    helper, no project           19 tools   12099 B  prompt   3746 B  app tools eager     prompt says load_tools: no   tool offered: no   first action ~15845 B
    helper, project attached     24 tools   21087 B  prompt   7697 B  app tools eager     prompt says load_tools: no   tool offered: no   first action ~28784 B
    ordinary pane                12 tools    8946 B  prompt   4366 B  app tools DEFERRED  prompt says load_tools: yes  tool offered: yes  first action ~22258 B

- Board-less helper: its first action costs ~15,845 B in **one** request instead of ~18,210 B in
  **two** — −13 % of bytes and a whole round trip, on the turn every Options and Actions question is.
- Helper with a board: the tool list does not move one byte (24 tools, 21,087 B — `helperkeys`'
  `+796 B` for `set_keybinding` is unchanged). Its prompt gains 411 B, which is
  `app_tools.prompt_section` coming back in place of the rule for a tool it was never offered.
- Ordinary pane: identical, line for line. Deferral is exactly as decision 9 left it.

## Tests

    PYTHONPATH=$PWD/backend python3 -m unittest tests.test_tool_groups tests.test_board_chat.BoardlessHelperTest tests.test_system_prompt

- `tests/test_tool_groups.HelperTests` (new): a helper is eager with a board and without one
  (`_deferred_groups() == ()`, the app and own_session schemas in the list, no `load_tools` tool and
  no rule line, `app_tools.prompt_section` back in the prompt); a board-attached helper's
  `messages[0]` no longer names `load_tools`; an ordinary pane still defers all three groups.
- `tests/test_board_chat.BoardlessHelperTest::test_the_builder_makes_a_board_less_agent_that_keeps_the_app_tools`
  passes again.
- `tests/test_system_prompt.py`: the byte-identity tests stay green.
  `SizeTests::test_the_board_policy_block_stays_tiered` fails on this tip and did before this change —
  it is #Z4HR's rule 10 (see `497eb591`), not this.
