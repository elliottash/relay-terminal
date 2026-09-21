# Cleanup terminal evidence

Built relay and relay-boardpane-tests through scripts/relay-build. Eight Qt cases pass under Xvfb with isolated XDG_CONFIG_HOME, including cleanup start/preview/apply, card and board-wide operations, completion, cancellation, error, refusals, and changelog formatting. Tool/prose events are left to their existing renderers.

The live Xvfb fixture uses a temporary workspace and fake worker, no provider or owner board writes. Clicks Clean up, then Apply, and captures persistent notes in the Switchboard console. Worker operations are synthetic to verify the GUI event route, not backend mutation semantics.

Run: copy worker.py to /tmp/cleanup-worker.py, then xvfb-run -a -s '-screen 0 1500x1100x24' python3 drive.py. The screenshot is /tmp/cleanup-live.png; the driver prints the isolated fixture directory.
