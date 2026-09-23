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
