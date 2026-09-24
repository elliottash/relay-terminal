# Add account helper handoff (#21KQ)

`drive.sh` starts Relay under Xvfb with an isolated home and the fake Claude Code and Codex
executables from the guest account tests. It opens Models › Sources, chooses **add account…** on
Claude Code, enters `personal`, and clicks **Set up with helper agent**. The driver checks that
the click did not add `personal` to the account registry. It reads no real login and makes no
model request.

- `01-add-account-dialog.png`: the new action beside OK and Cancel.
- `02-helper-open.png`: the Models helper opened on Sources with the selected CLI and entered
  name carried into `/skill guest-account-setup`. This isolated profile has no non-guest helper
  model, so Relay leaves the request in the composer and tells the user to choose one before
  sending it. With a usable non-guest helper model, the button submits the skill turn.

Checks run after the change:

```text
PYTHONPATH=backend python3 -m unittest tests.test_skills tests.test_agent_context
  52 tests passed
RELAY_SESSION=card21kq scripts/relay-build --target relay
  Built target relay
RELAY_SESSION=card21kq scripts/relay-build --target relay-modelspane-tests
xvfb-run -a ctest --test-dir build -R '^modelspane$' --output-on-failure
  1/1 test passed
```

The capture proves the dialog-to-helper route and its no-provider fallback. A live interview
with a configured non-guest model remains for independent verification.
