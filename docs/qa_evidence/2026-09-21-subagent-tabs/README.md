# Implementer evidence — #T8SB

Cause: `SubagentTabsView::syncRows` only refreshed tabs that were opened individually. Once the pane opened, its owner's strip folded away, leaving other agents without visible tabs.

Fix: synchronize missing tabs from every listed agent, including agents arriving later. New tabs subscribe through the existing `onViewCreated` callback without changing the selected transcript, focus, or message draft. Explicitly closed running tabs stay closed through updates and can be reopened. Closed-id tracking clears when the row leaves the model, so reused ids after a worker restart appear normally.

- `scripts/relay-build --target relay-subagents-tests relay`
- `ctest --test-dir build -R '^subagents$' --output-on-failure`: 30 Qt cases pass.
- `tabsAutomaticallyIncludeEveryAgentWithoutStealingFocus` verifies both initial agents appear from one synchronization, a third arrives in the background, each tab selects its own view, subscriptions are not duplicated, and closed tabs stay closed until reopened.
- Live Xvfb widget check with isolated `XDG_CONFIG_HOME` and `QT_QPA_PLATFORM=xcb`: `build/relay-subagents-tests tabsAutomaticallyIncludeEveryAgentWithoutStealingFocus`. Set `RELAY_SUBAGENTS_ALL_TABS_SHOT` to capture `two-tabs.png`.

This is implementer evidence, not independent QA.
