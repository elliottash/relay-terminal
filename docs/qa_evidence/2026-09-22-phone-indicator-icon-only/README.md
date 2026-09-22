# Card #62M4 implementer evidence

The live screenshot [`icon-only.png`](icon-only.png) was captured from the built Relay application
under Xvfb with an isolated `HOME` and XDG profile. The only terminal pane is being shared: its
header shows the outlined phone glyph beside `work`, with no `phone` label.

`drive.sh` reproduces the scene by opening a fresh Relay workspace, starting sharing from the
prompt strip, closing the pairing dialog, and capturing the Relay window.

Automated verification run from the repository root:

- `scripts/relay-build --target relay-sharing-tests`
- `ctest --test-dir build -R '^sharing$' --output-on-failure` — 1/1 passed
- `scripts/relay-build --target relay` — passed

The focused test also verifies that the icon-only state retains the explanatory tooltip and that
guest counts and active driver names remain textual.
