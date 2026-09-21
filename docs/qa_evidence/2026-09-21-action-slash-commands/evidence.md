# Actions slash-command labels

- `scripts/relay-build --target relay-settings-tests relay` passed.
- The settings-pane tests ran under Xvfb with an isolated XDG_CONFIG_HOME: 43 passed. The new test verifies slash-command labels alongside Alt+S, no invented command for a plain action, /swap and /clear searches, activation of the original action, and omission of hidden /todos.
- An isolated full Relay GUI was opened through Ctrl+Shift+A and searched by /swap, /clear and /compact. Screenshots show the matching rows, slash-command labels, and Alt+S retained beside Swap models. No model prompts were submitted.
- Existing saved aliases already display their /name in the action description; this change adds consistent discovery for built-in actions.

Screenshots: swap.png, clear.png, compact.png.
