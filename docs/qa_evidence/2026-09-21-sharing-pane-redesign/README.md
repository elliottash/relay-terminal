<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #SHRP — the Sharing pane says what varies

2026-09-21, this machine (Ubuntu 24.04 aarch64, Qt 5.15.13), `build/relay` built through
`scripts/relay-build` from the tree that landed.

The pane that opens from a pane's share button (and the palette's "Sharing") used to draw the
full guest kit — "Pane …", "Nobody here yet", a note pointing at the share window, "This share",
three buttons, two checkboxes with a sentence each — once per shared pane. Since #PH0N every pane
with a screen is shared with the owner's phones, so that was the same block three times over.

Now it reads, top to bottom: the top line (remote control, the address, which of your phones are
connected) with **Pair a phone…**; **Waiting for you** when anything is; **Guests** for the panes
that have a participant or a live invite, with the two options as one row of checkboxes and their
sentences said once under the section; **Nobody is visiting**, one row per quiet pane with
**Invite…**. The note "Your own paired phones are not guests and are not listed here…" is gone.

## Headless tests

```
$ ctest --test-dir build -R '^sharing$'
1/1 Test #22: sharing ..........................   Passed    0.03 sec
100% tests passed, 0 tests failed out of 1
```

`relay-sharing-tests` is 23 cases (`tests/sharingpane_test.cpp`); the four this card adds are the
top line's states and device names (and the Pair a phone… button calling back), three quiet panes
as one section of three rows and three Invite… buttons with no checkbox and no "This share", a
Guests block per visited pane with exactly two checkboxes each and the options note once, and a
knock first, naming its pane. The #SHCK test (`togglingAnOptionSurvivesTheRebuildItCauses`) still
passes: the rebuild still hides, unparents and `deleteLater`s the old rows.

The sidecar's `devices` line now carries `online` per device, and is sent again whenever the hub's
device count moves, so the top line can name the phones instead of counting them:

```
$ RELAY_KEYRING=off python3 -m unittest tests.test_remote_gui_host.AlwaysOnTests.test_start_with_always_brings_the_service_up_at_the_hosted_rendezvous
Ran 1 test in 0.233s
OK
```

## Live, with a stub sidecar

`run-live.sh <scene>` is the run and `stub_gui_host.py` the sidecar it puts behind
`RELAY_REMOTE_DIR`. Xvfb on `:97`, an isolated `HOME`, `XDG_*` and `TMPDIR` under
`/tmp/shrp/<scene>`, `RELAY_KEYRING=off`, keys through xdotool. The profile starts with
`alwaysOn=true` at `relay-terminal.ai`; the stub answers `start` with remote control on and online,
`devices` with iPhone and iPad connected (and an "old Pixel" that is paired but not connected, which
the line leaves out), and plays one scene through `participants` and `knock`. Three tabs are opened
and `cd` into `build`, `logs` and `deploy` so the panes have titles of their own; each is published
as it appears. The Sharing pane is opened from the palette ("Sharing", Return) in the first two
scenes and by the knock itself in the third. 0 `gui_crash` lines in every run.

1. **`quiet.png`** — three panes, nobody visiting. The top line reads
   *Remote control on · relay-terminal.ai · iPhone, iPad connected*, then **Pair a phone…**; one
   heading *Nobody is visiting*, the sentence *Every pane is reachable from your phones. To let
   someone else in, invite them.*, and three rows — *deploy* (the pane it was opened from, first),
   *logs*, *build* — each with **Invite…**. No checkbox, no "This share", no per-pane heading.
2. **`guests.png`** — alice (editor) on *deploy*, a live viewer link on *build*, *logs* quiet.
   *Guests* → *Pane “deploy”* with alice's row (Make viewer, Remove) and the controls (Invite
   someone…, Pause guests, End sharing, then one row: ☐ Guest prompts run immediately ☐ Guests can
   act only while I'm here); *Pane “build”* with the link row (Revoke) and the same controls; one
   note under the section saying both option sentences; then *Nobody is visiting* with *logs*.
   The pane header's chip on *deploy* says *1 guest*.
3. **`knock.png`** — alice knocking on *deploy*: the pane opened by itself without taking the
   keyboard, *Waiting for you* first with *alice wants to join pane “deploy”*, the code 48213
   large, Refuse / Admit as viewer / Admit as editor and the countdown; then *Nobody is visiting*
   with all three panes.

The GUI asked the stub for `devices` once per Sharing pane opened (`sidecar.jsonl`,
`{"t":"devices"}`), which is the `requestDevices()` call the window makes so the `online` flags are
fresh.

## Strings on the pane

- Top line: `Remote control on · <address> · <names> connected`, `… · no phone connected`,
  `… · N phones connected` (a sidecar without the per-device flag), `… · offline: <reason>`,
  `Remote control off`. Button: `Pair a phone…`.
- `Waiting for you` — `<who> wants to join pane “<title>”`, `<who> asks to type in pane “<title>”`,
  `<who> wrote a prompt for the agent in pane “<title>”`.
- `Guests` — `Pane “<title>”`; `Invite someone…`, `Pause guests` / `Let guests act again`,
  `End sharing`; `Guest prompts run immediately`, `Guests can act only while I'm here`; the note:
  *Guest prompts run immediately — off, every prompt an editor writes waits here for you; on, it
  goes straight to that pane's agent, on your key, unread. Guests can act only while I'm here — on,
  whenever Relay's window is not the one you are looking at, guests are paused, as though you had
  pressed Pause.*
- `Nobody is visiting` — *Every pane is reachable from your phones. To let someone else in, invite
  them.* (remote control on) or *Remote control is off, so your phones cannot reach these panes. To
  let someone else in, invite them.*; one row per pane: `<title>` and `Invite…`.
- Nothing shared at all: `Nothing is shared right now.` and a note, under the same top line.
