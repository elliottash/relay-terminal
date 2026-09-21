<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #FR1C task 1 — one entry point, and a code to type on the phone

2026-09-20, this machine (Ubuntu 24.04 aarch64, Qt 5.15.13), build `2026-09-20.21H.08`.

What landed: `7fc3f58df277` (the pairing code, the Copy button, the words and the rows in
`relay::remotesettings`, the tests), `546bda03a4f7` (the plug menu, `remote.pair`, `pairPhone()`).

## Headless tests

```
$ ctest --test-dir build -R "remotesettings|sharing"
1/2 Test #20: sharing ..........................   Passed    0.01 sec
2/2 Test #21: remotesettings ...................   Passed    0.18 sec
100% tests passed, 0 tests failed out of 2
```

`relay-remotesettings-tests` is 22 cases now (`tests/remotesettings_test.cpp`); the six this card
added are the plug menu's rows and their order in both states, "Pair a phone" writing `alwaysOn`
and the hosted address (and keeping an address the owner had already picked), the two messages the
GUI sends, the two lines it parses, the four states of the code row, and the 3 s fallback sentence.

## Live, with a stub sidecar

`run-live.sh` is the run and `stub_gui_host.py` the sidecar it puts behind `RELAY_REMOTE_DIR`
(it logs every line the GUI sends and answers `start`, `pair`, `pair_code` and `devices`).
Xvfb on `:98`, an isolated `HOME`, `XDG_*` and `TMPDIR` under `/tmp/fr1c`, `RELAY_KEYRING=off`,
keys through xdotool. The profile starts with `alwaysOn=false`, and the only thing the drive does
is Ctrl+Shift+A → "Pair a phone" → Return.

1. **Remote control off** — 0 lines in the sidecar log at launch.
2. **"Pair a phone" from the palette** — the switch goes on by itself and the sidecar is told:

   ```json
   {"address":"relay-terminal.ai","always":true,"name":"this desktop","t":"start","tls":true}
   ```

   and the profile it wrote is `[remote] address=relay-terminal.ai, alwaysOn=true`.
3. **The pairing window opens and asks for a code** — `{"t":"pair"}`, `{"t":"devices"}` and
   `{"t":"pair_code"}`, in that order (`sidecar.jsonl`).
4. **The code is shown beside the QR** — `pairing-dialog.png`: "On your phone, open Relay and
   enter / **ABCD 4829** / Expires in 9:53", the line at the top saying remote control is on and
   where to turn it off, and "Copy link" beside "Phone connects to …".
5. **Closing the window withdraws the code** — `{"code":"ABCD","t":"pair_code_revoke"}`.
6. **Quit** — 0 `gui_crash` lines.

## The wire contract, for the sidecar half (task 2)

GUI → sidecar, when the pairing window opens (every time it opens):

```json
{"t":"pair_code"}
```

Sidecar → GUI:

```json
{"t":"pair_code","code":"ABCD","pin":"4829","expires":600}
{"t":"pair_code_state","code":"ABCD","state":"used","failures":0}
```

`state` is `used`, `burned` (three wrong PINs) or `expired`; `failures` is how many PINs were
tried. When the code is used, the five-digit `ask` arrives exactly as it does for a QR pairing —
nothing new there, and the dialog's Allow viewing / Allow typing answer it.

GUI → sidecar, when the window closes on a live code (or asks for another one):

```json
{"t":"pair_code_revoke","code":"ABCD"}
```

A sidecar that does not answer `pair_code` within 3 s is one from before this card: the dialog
then shows the QR alone and says "This desktop cannot make a code; scan the QR instead."
