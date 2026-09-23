# P7CK implementation evidence

The PLAN chip is now a compact QToolButton. Clicking it requests build mode through
Pane::setAgentMode, focuses the prompt, and teaches the live plan-toggle shortcut.
Its theme selector follows the widget type, preserving the chip styling.

Validation:
- `scripts/relay-build --target relay-consolemode-tests relay` passed.
- Live real-Pane checks under Xvfb/xcb with isolated XDG_CONFIG_HOME exercised a
  mouse press/release on PLAN: one set_mode/build message, chip hidden after the
  worker mode event, draft preserved, prompt focused. All new assertions passed.
- The broader console executable failed an unrelated existing queue assertion in
  repeatedEnterKeepsTheFirstQueuedPrompt (`sent.size() != 1`); recorded on #QFF1.
- Board format check has no P7CK findings; 12 existing errors concern other cards.
- The focused three-case `--plan-click-only` run passed under Xvfb/xcb with isolated
  settings (exit 0). `TestsCommands.check_card('P7CK')` reports no findings.
- Commit `5bbbc029` also passed the exact-tree console build gate and the same focused
  Xvfb check against its isolated build output.

Reproduce the focused three-case plan check after building:
```sh
plan_test_config=$(mktemp -d)
XDG_CONFIG_HOME="$plan_test_config" xvfb-run -a env QT_QPA_PLATFORM=xcb build/relay-consolemode-tests --plan-click-only
```

This exercises the actual Pane and its worker protocol boundary using synthetic
worker events; no live model call or provider credentials are required.
