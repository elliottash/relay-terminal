# Stale tests found while verifying models cards (2026-09-25)

## A. #E8V1 rename (e6febb9f) broke four Qt test expectations
Commit e6febb9f ("#E8V1 Label the one pane model and show console kind") renamed the helper-agent button "Helper Agent (Alt+Q)" -> "Agent (Alt+Q)" and the jobs role "helper agent" -> "system-pane agent" without updating tests. At working-tree HEAD 54018502 (binaries built by scripts/relay-build):
```
$ xvfb-run -a build/relay-modelspane-tests -silent
FAIL! ModelsPaneTests::theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole  Actual "Agent (Alt+Q)" Expected "Helper Agent (Alt+Q)"  Loc: ../tests/modelspane_test.cpp(782)  Totals: 25 passed, 1 failed
$ xvfb-run -a build/relay-settings-tests -silent
FAIL! SettingsPaneTests::theHelperConsoleFollowsTheModeAndAsksAsTheOptionsPane  Actual "Agent (Alt+Q)" Expected "Helper Agent (Alt+Q)"  Loc: ../tests/settingspane_test.cpp(1607)  Totals: 51 passed, 1 failed
$ xvfb-run -a build/relay-jobstab-tests -silent
FAIL! (jobsUnder(tab.list(), "main")) Actual "system-pane agent" Expected ["agent turns","subagents","helper agent"]  Loc: ../tests/jobstab_test.cpp(181)  Totals: 23 passed, 1 failed
$ ctest --test-dir build -R "^conversations$" --output-on-failure
FAIL! ConversationsTest::theHelperConsoleSitsAtTheBottomCollapsedAndAsksAsTheSessionsPane  Loc: ../tests/conversations_test.cpp(2439)  Totals: 59 passed, 1 failed
```
Rename source of truth: src/ModelsPane.cpp:610-614, src/JobsTab.cpp:208.

## B. Python suites named by #4BPE fail at clean HEAD 9a13cfe2 (worktree /tmp/relay-verify-head)
```
$ PYTHONPATH=backend python3 -m pytest tests/test_presets.py tests/test_roles.py tests/test_keybindings.py tests/test_model_switch.py tests/test_conv_index.py tests/test_web_model_name.py -q
4 failed, 326 passed, 6 errors
FAILED tests/test_presets.py::GuiMirrorTests::test_one_serving_model_state_feeds_the_picker
FAILED tests/test_presets.py::GuiMirrorTests::test_the_gui_uses_worker_presets_without_a_duplicate_table
FAILED tests/test_keybindings.py::GuiDefaultsTests::test_qwas_defaults
FAILED tests/test_keybindings.py::GuiDefaultsTests::test_the_presets_doc_mirrors_the_shipped_tables
ERROR tests/test_web_model_name.py (all 6): setUpClass runs node tests/model_name_peer.mjs -> SyntaxError: Named export 'modelName' not found ... app/modelname.js is a CommonJS module (last touched by 1aa4d54a 'Rename models')
```
Causes: src/Pane.h mirror drift (GuiMirror), default-key drift (GuiDefaults), app/modelname.js module-format change (web_model_name). All postdate #4BPE's green run of 2026-09-21.

## C. test_openrouter_catalog and test_guest_harness_provider fail at HEAD but passed at #CDP7's commit
At clean HEAD 2db96643 (worktree):
```
$ PYTHONPATH=backend python3 -m unittest tests.test_openrouter_catalog
FAIL: test_catalog_rows_puts_the_built_in_tier_rows_first_then_the_live_ones  (tests/test_openrouter_catalog.py:154)  AssertionError: 'flash' != 'main'
$ PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_provider
FAIL: test_preset_rows  (tests/test_guest_harness_provider.py:151)  ['guest:claude','guest:codex','guest:codex:ashe-ethz-ch'] != ['guest:claude','guest:codex']
```
Both suites run fully green at CDP7's last commit 78639301 (verified by checkout). Culprits: b759f23d (#Y4PJ openrouter defaults moved main onto glm-5.3-flash, changing which built-in tier row leads) and the later-added guest harness guest:codex:ashe-ethz-ch (preset list grew without updating the test).

## D. The consolemode suite fails at HEAD; green at #PH9G's commit (and #PBKR's)
At clean HEAD 2db96643:
```
$ XDG_CONFIG_HOME=$(mktemp -d) xvfb-run -a ctest --test-dir build -R '^consolemode$'
0% tests passed, 1 tests failed out of 1
FAIL ../tests/consolemode_test.cpp:1917  text.contains("▸ ran pytest\n\nThe tests pass.")
FAIL ../tests/consolemode_test.cpp:1920  text.contains("▸ ran ctest\n✦ 2 tool calls · 2 s")
FAIL ../tests/consolemode_test.cpp:633  edited != path
FAIL ../tests/consolemode_test.cpp:634  line != 2
FAIL ../tests/consolemode_test.cpp:646  context.seen != QStringList({home->path()})
```
At commit 1b05786b (#PH9G, 2026-09-22): "consolemode: 20 cases, all passed" (checkout-verified). Suspects landed after: bf5aa57c (#2M26 WARP.md→RELAY.md; the path/`edited` expectations), b70c33bf (#PBZ4 artifact consoles on file editors; the tool-call/editor cases), 0c51dc54/504d7629 (prompt/recall wording). The plan-mode cases still pass at HEAD (`--plan-click-only` exits 0).
