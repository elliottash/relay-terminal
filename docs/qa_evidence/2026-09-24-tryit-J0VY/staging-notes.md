# Staging notes — #J0VY Try it

`stage.sh` launches a **disposable Relay** from this checkout's `build/relay` (the fixed
binary, commit `fc6c5cce`) with its own `HOME`, `XDG_*` and runtime dir under
`/tmp/claude-1000/tryit/j0vy`, on your display. It opens seven splits (eight shell panes),
lets the app idle for 60 seconds, and prints the count of `app_catalog_updated` events in
that sandbox's `relay.log`, then leaves the window open.

How this differs from your real instance:

- It is a fresh profile: no sessions, no keys (`RELAY_KEYRING=off`), one empty workspace —
  your own windows, panes and logs are untouched; closing the staged window deletes nothing
  you care about (`rm -rf /tmp/claude-1000/tryit/j0vy` clears it entirely).
- The panes are plain shells, not agent sessions, so there is no model traffic — the loop
  this card fixes was driven by worker `presets` events, which shell workers also exchange
  (that is what the count measures).
- Your *running* Relay still has the old code until it is restarted, so comparing the two
  windows side by side is comparing old (yours) against new (staged).

Mechanical pass (mine, headless under Xvfb): 8 panes opened, 60 idle seconds →
**0 `app_catalog_updated` events**; screenshot `01-eight-panes-idle.png`. The same script
on the pre-fix binary would log hundreds — see the Issue section: 50,185 in ~3.5 h on the
live instance.
