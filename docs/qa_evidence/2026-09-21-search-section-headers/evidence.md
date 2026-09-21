# Section-header search and navigation

- `scripts/relay-build --target relay-settings-tests relay`: passed.
- `xvfb-run -a env XDG_CONFIG_HOME=/tmp/section-search-test-config build/relay-settings-tests`: 45 passed, no failures.
- New GUI tests cover a mouse click on an Options section result from Actions, keyboard navigation to an Actions submenu/category from Options, filter clearing, unchanged action execution count, and scrolling to a subheading inside a collapsed Options section.
- The existing ranking test now expects the newly searchable Turn limits heading alongside the matching option. All other search and navigation tests pass unchanged.

Full-window Xvfb evidence uses isolated XDG configuration, data, cache and runtime directories, with no model prompt submitted. The screenshots record Actions category search and navigation, then an Options section found from Actions and the cleared filter after navigation.
