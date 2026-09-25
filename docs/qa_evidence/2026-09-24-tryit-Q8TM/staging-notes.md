# Staging notes — Q8TM Try it

This stages the **backend behaviour** the card changed, not the app window: what Relay actually
sends to a guest when a pane switches back to it. `stage.sh` runs the real
`guest_harness_provider`/`Agent` code from this checkout against the scripted `FakeHarness`
(the same fake the test suite uses) and a scripted native provider standing in for the GLM
endpoint — so no model, no network, no real Codex or Claude login, and the "model answers" are
one fixed line each. Everything above the prompts (the status lines, the harness start
parameters, the exact prompt payload) is the real code path a real switch takes: the only
replaced parts are the two model endpoints. Run twice; the second run overwrites nothing.
