# VPR7 implementation evidence

Provider settings could use m_active while Priorities continued serving another pane. Also Providers used helper presets before the served worker answered, while Priorities read only the empty served catalog. modelsSection now resolves the served target; applyModelsTarget falls back to the same helper catalog on open and refresh; the provider models link preserves that target.

![Staged priorities](priorities.png)

Validation:
- `scripts/relay-build --target relay-modelspane-tests relay-settings-tests` passed.
- Isolated XDG_CONFIG_HOME and `xvfb-run -a build/relay-modelspane-tests`: 21 passed.
- `QT_QPA_PLATFORM=offscreen build/relay-settings-tests providersAndPrioritiesReadTheSameServedPane everyProviderRowHasAModelsLinkIntoTheAvailableTab`: 4 passed including setup/cleanup.
- `python3 -m unittest discover -s tests -p test_openrouter_catalog.py`: 13 passed.
- `python3 -m unittest discover -s tests -p test_guest_harness_provider.py`: 68 passed.
- `python3 scripts/relay-board.py check`: existing board-wide 12 errors / 754 warnings, unrelated to these cards.

Screenshots are real Qt widgets under Xvfb with synthetic provider payloads, not a live provider call or a reproduction on sphinxpad. `ssh -o BatchMode=yes -o ConnectTimeout=5 sphinxpad` timed out. The initial build invocation named a nonexistent relay-settingspane-tests target; the corrected target passed. An initial Codex test forgot that adding clears search; correcting the test to search again passed without changing eligibility.
