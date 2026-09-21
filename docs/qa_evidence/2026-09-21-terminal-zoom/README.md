# #Z00M implementer evidence

`scripts/relay-build --target relay relay-engine-tests` passed (build 2026-09-21.08H.06).

`RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests ctrlZoomKeysAndWheel`: 3 passed, 0 failed. See tests.txt. Checks Ctrl-only, shifted plus, keypad, reset, geometry reflow, positive/negative and partial wheel deltas, transition to ordinary scrolling, and zoom in the alternate screen with mouse reporting enabled.

Live app under `xvfb-run -a -s '-screen 0 1280x900x24'`, with isolated XDG_CONFIG_HOME and XDG_DATA_HOME in /tmp/relay-zoom-live, `build/relay --clean-shell --workspace /tmp/relay-zoom-live`:
- before.png: baseline terminal prompt.
- keys.png: composer focused, three Ctrl+= presses; larger terminal text.
- reset.png: Ctrl+0 returns to baseline.
- wheel.png: pointer at terminal (400,300), hold Ctrl and send three wheel-up notches; same larger text as keyboard zoom.

Captured using ImageMagick `import -window`, inputs using xdotool. Images inspected by implementer. No real user settings or window altered.

Full board format check has pre-existing errors for CLNP and MDL1 (non-Crockford IDs); the Z00M card and thread pass.
