# #RCPF — `RELAY_HOSTED=off`: a profile that cannot reach Relay's hosted gateway

`drive.py` starts a loopback stub that logs every request, points `RELAY_HOSTED_URL` at it and
exercises each Relay Free entry point: role resolution for a harness pane (the case that falls
through to Relay Free), a hosted chat turn (the recap/title/summary transport), `token()`, Relay
Pro validation, an image call and `image_roles()`. `drive-output.txt` is two runs:

- `scripts/relay-qa-run python3 drive.py` — the launcher now defaults `RELAY_HOSTED=off`:
  `available()` False, `status()` unavailable, summaries/chores/terminal_use stay on the pane's
  own model, every call refuses naming the switch, **the stub saw no request**.
- `RELAY_HOSTED= scripts/relay-qa-run python3 drive.py` — the control: roles fall through to
  `relay-free` as before and the stub sees `/v1/challenge` and `/v1/health`.

Not driven here: a GUI away-recap after idling. The recap path's only hosted touch is
`side_provider(role="summaries")` → the resolver and the hosted transport, both covered above.
