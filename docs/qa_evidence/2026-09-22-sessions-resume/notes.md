# Sessions Resume — #RSME

Implemented one Resume control. Enter in the list or search and row activation take the same path. The window closes Sessions before finding an already-open Relay/guest session across windows (including the initiating pane), or opening a new pane. Original conversations are retained. Mouse Resume teaches the Enter shortcut with normal hint limits.

Validation:
- Landed `607ed1c0`; the exact committed tree built and passed the conversations test through land.py.
- `scripts/relay-build --target relay relay-conversations-tests` passed.
- `ctest --test-dir build -R '^conversations$' --output-on-failure` passed (3.40 seconds). Updated regressions cover button text/removal, mouse Resume, Enter from list/search, Shift+Enter compatibility, guest rows and preserved fork behavior.
- `drive.py` runs Relay under Xvfb with isolated configuration/data and a local test provider, without sending prompts. It seeds a saved conversation and exercises new-pane Enter, existing-pane Enter, and mouse Resume from the other pane. Layout assertions confirm exactly two terminal panes and unchanged session IDs; screenshots show the closed Sessions panel and focused session.
- Board tests_check: no blocks or failing tests; only a pre-existing timing notice. Full board validation reports 12 existing errors on unrelated cards/threads; none names RSME.

Screenshots: 01 single Resume control; 02 new pane with loaded transcript; 03 already-open session selected; 04 Enter returns to that same pane; 05 mouse Resume from the other pane returns to the saved session.

Guest matching is built and reviewed, with guest action tests; live guest process activation was not exercised. Independent UI verification remains pending.
