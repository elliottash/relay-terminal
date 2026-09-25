# #JX8Z implementation evidence — 2026-09-25

## Checks

- `PYTHONPATH="$PWD/backend" python3 -m unittest tests.test_guest_usage_poll tests.test_logs tests.test_roles`: 99 passed.
- `QT_QPA_PLATFORM=offscreen build/relay-modelcatalog-tests`: 73 passed, including `firstDrawReadsRecentUsageState` (fresh score appears, stale score does not).
- `RELAY_SESSION=jx8z scripts/relay-build`: full build passed, `relay` linked.

## Read-only live probe

`guest_usage_poll.poll_once` fetched the default signed-in accounts with GET only; it did not
invoke a reset-consumption endpoint. `logs.usage_state` appended two real records to
`~/.local/share/relay/logs/usage-states.jsonl` (mode `0600`):

| Preset | Windows reported | Reset count | Source |
| --- | --- | ---: | --- |
| `guest:claude` | `5h`, `weekly` | 1 | `subscription_poll` |
| `guest:codex` | `weekly` | 0 | `subscription_poll` |

The Codex endpoint returned no 5h window or reset expiry for this default login, so those fields
are absent rather than invented. Other registered accounts are polled when configured in Relay.
The currently running Relay process predates the new worker code; its automatic 15-minute polls
will begin on restart. The Qt draw test exercises the recorded-state bootstrap without launching
a second desktop process against the live profile.

## Scope

The file stores whitelisted usage fields, preset/account IDs and timestamps. It stores no token,
request headers, prompt, account directory or raw HTTP body. `routing-draws.jsonl` remains the
choice/probability history; its candidate key's prefix before `|` joins to `preset` here, with
the latest snapshot at or before the draw timestamp supplying the raw quota state.
