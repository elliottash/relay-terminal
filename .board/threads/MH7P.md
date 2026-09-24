<!-- relay:entry 20260923T024000Z-c1 author=claude-code kind=progress -->
### Claude Code · 2026-09-22 22:40
Filed and claimed from the owner's request "there needs to be a helper agent on the model page". Plan: copy the Options/Sessions helper shape into `relay::ModelsPane` (a `ModelsContext`, the collapsed row, lazy console, fold, height rules), wire it with `wireConsoleHost`, route Alt+Q to it, and add a `models` brief in `backend/relay_core/agent_context.py`.

<!-- relay:entry 20260923T030000Z-e1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 23:00
Landed and moved to needs-verification.
- `src/ModelsPane.{h,cpp}`: `ModelsContext` (name/surface/brief `models`, "Models helper", "Ask the Models helper…", scope console, no shell, routing agent, persist `helper` keyed on the tab id). Its `screen()` gives the tab, "Serving: <title>", the filter, and the class in focus (priorities), the job in focus (jobs) or the provider row ids (providers). The helper row sits under all four tabs; `showEvent` hides it when there is no factory.
- `src/RelayWindow.h`: `createModelsPane` calls `wireConsoleHost(view, tool, "models.ask")`. `focusHelperOfActiveLeaf` routes Alt+Q to the Models pane, and `refreshConsoleHosts` keeps its tab id and workspace current.
- `backend/relay_core/agent_context.py`: `models` added to `NAMES` (without it an unknown name is refused), plus a `models` brief.
- Tests: modelspane 22/22 (new `theHelperIsOneRowUnderAllFourTabsAndBuildsOneConsole`), test_agent_context 24/24.
- Live drive: docs/qa_evidence/2026-09-22-models-helper. The row shows on first run and on a restored pane. Alt+Q opens the console, and a real glm-5.3-flash turn named the tab, the served pane and the class in focus.
- Found on the way, not caused by this change (see the evidence README): a helper console's model-box pick does not rescue a tab worker configured on a guest. The next turn still gets the #4NXH refusal, in every helper.

<!-- relay:entry 20260923T030100Z-e2 author=claude-code kind=evidence -->
### Claude Code · 2026-09-22 23:01
Landed as 3c6edc69 through `scripts/land.py`. The build gate built the exact committed tree (`verify: the exact tree builds`). Only this card's hunks of `src/RelayWindow.h` and `docs/ARCHITECTURE.md` were taken; other sessions' edits in those files were left in the working tree.

<!-- relay:entry 20260923T153214Z-0x author=agent kind=evidence model=claude-opus-5-5 pane=bb8afbbb turn=745d1bd91afb4e8e9008c7a957647135/f79b52d8b8ac400bb7e62551d68dabb9 -->
Owner: "fix the separate bug". Fixed in 7fa72190: with Main on Claude Code and nothing else usable, the helper worker is built on `guest_harness_provider.UnavailableProvider`; a model picked in the helper's box was applied but `Agent.set_model` kept any injected provider, so turns still refused. A stand-in (`stand_in = True`) is now replaced by a real provider on a pick; a real injected guest harness is still kept. Regression: tests/test_guest_harness_provider.py `AgentWiringTests.test_a_model_picked_after_the_stand_in_replaces_it` — fails without the fix, passes with it. `python3 -m unittest tests.test_guest_harness_provider tests.test_board_chat tests.test_agent`: all OK. Also fixed on the way (3ecf81f3): worker.py's error handler read an unbound `kind` for a non-object message (from 552982fa), which failed tests.test_agent `test_malformed_request_does_not_crash`. Not re-driven live in the GUI.
