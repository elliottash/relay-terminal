<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #PH0N phase 1.1 / 1.2 — remote control as a service, desktop side

2026-09-20, this machine (Ubuntu 24.04 aarch64, Qt 5.15.13), build `2026-09-20.18H.06`.

What landed: `2c59a720` (the settings half, `RemoteShare`, the chrome badge, the unit tests),
`f39cb626` (Options › Remote, the auto-publish sweep, the plug's indicator, the teardown fix),
`8fc8d60b` (#WMXN: the phone's Recap sends `manual`).

## Headless tests

```
$ ctest --test-dir build -R "remotesettings|sharing|remotepane|panestate"
1/4 Test #20: sharing ..........................   Passed    0.03 sec
2/4 Test #21: remotesettings ...................   Passed    0.14 sec
3/4 Test #22: remotepane .......................   Passed    1.03 sec
4/4 Test #38: panestate ........................   Passed    0.30 sec
100% tests passed, 0 tests failed out of 4
```

`relay-remotesettings-tests` is 15 cases (`tests/remotesettings_test.cpp`): the two remembered
keys and their defaults, `always`/`address` on the `start` line both ways round, `remote_state`
parsed, the status line's four shapes, the picker with and without a sidecar and with an
unavailable entry, and Options › Remote's rows writing through their hooks and resetting.

## Live, with a stub sidecar

`run-live.sh` is the run, `stub_gui_host.py` the sidecar it puts behind `RELAY_REMOTE_DIR`
(it logs every line the GUI sends and answers `start` with `started` + `remote_state`), and
`sidecar-switch-on-off.jsonl` the log it wrote. Xvfb on `:97`, an isolated `HOME`,
`XDG_*` and `TMPDIR` under `/tmp/ph0n`, `RELAY_KEYRING=off`, keys through XTEST.

1. **Switch off** — nothing starts: 0 lines in the sidecar log at launch, and 0 after a second
   pane is opened. (A desktop with no phone starts no python.)
2. **Switch on, from Options › Remote** — typed "remote control" into the Options search and
   pressed Enter. The sidecar comes up and is told:

   ```json
   {"address":"relay-terminal.ai","always":true,"name":"this desktop","t":"start","tls":true}
   ```

   and **both panes that were already open** are published with no button pressed:

   ```json
   {"cols":59,"cwd":"/tmp/ph0n/ws","id":"65475911-…","rows":32,"status":"idle","t":"pane","title":"ws"}
   {"cols":26,"cwd":"/tmp/ph0n/ws","id":"24f0312f-…","rows":29,"status":"idle","t":"pane","title":"ws"}
   ```

3. **A pane opened while it is on** publishes itself (an earlier run of the same script with the
   switch already on: one `pane` line at launch, a second after `Ctrl+E`), and closing it with
   `Ctrl+Shift+W` sends `{"id":"…","t":"unpane"}`.
4. **Switch off again** — `unpane` for each pane, then `{"t":"stop"}`.
5. **Quit** — 0 `gui_crash` lines. The first run of this script crashed at quit
   (`SIGSEGV`, `QStackedLayout::widget` under `RelayWindow::refreshSharingPanes`, reached from
   `RemoteShare::stopSharing` as a shared pane's view was destroyed under a half-destroyed
   window). It was reachable before whenever a pane was shared at quit; with remote control on it
   is every quit. `~RelayWindow` now disconnects from `RemoteShare`, and the run above is clean.

## The wire contract, for the sidecar half

GUI → sidecar, on `start` (and again, verbatim, when the switch is turned on while the sidecar is
already up for an ordinary share):

```json
{"t":"start","tls":true,"name":"this desktop","always":true,"address":"relay-terminal.ai"}
```

`address` is the picker's value: `"relay-terminal.ai"` for the hosted rendezvous, `"tailscale"`
for the tailnet (the *word*, not this machine's tailnet name — the name changes with the machine
and "use the tailnet" is what the owner picked), or an IP from the sidecar's own `addresses`
list. The public tunnel (`"cloudflare"`) is never offered as an always-on address.

Sidecar → GUI, whenever any of it changes:

```json
{"t":"remote_state","on":true,"address":"relay-terminal.ai",
 "base":"https://join.relay-terminal.ai/d/…","online":true,"devices":2,"reason":""}
```

Switching the service off sends `unpane` for every pane and then `{"t":"stop"}`.
