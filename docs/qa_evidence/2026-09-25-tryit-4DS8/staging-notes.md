# Staging notes — Try it for `#4DS8`

`stage.sh` (in this directory) puts the situation in front of you. It is rerunnable and builds
everything from scratch each time; it needs no model and no network.

## What it does

1. A **disposable Relay** (`build/relay`) on a private Xvfb display, with its own `HOME`,
   `XDG_RUNTIME_DIR` and workspace under `/tmp/rl-4ds8.*` — deleted when the script exits. The
   sandbox's only wires to this machine are two sockets symlinked into its runtime dir:
   - `bus` — the session bus, so the app and `scripts/relay-drive` work;
   - `pipewire-0` — **the live PipeWire socket**, so the Microphone dropdown enumerates the real
     sources. This machine has `pw-record` but no `pactl`, so enumeration goes via `pw-dump`,
     which speaks to that socket.
2. It opens Options with `drive action app.settings`, then walks the row with the keyboard only
   (settings tabs and rows have no named drive control): types `microphone` into the search,
   `Return` to activate the row (a Choice row's activate opens its dropdown), `Down` + `Return`
   to choose the first real source.
3. It leaves one screenshot per step and `settings-after.txt`, the `[voice]` section the dropdown
   wrote, plus `sandbox-relay.conf.txt` (the whole sandbox settings file).

## What each file shows

| File | Shows |
|---|---|
| `10-options-search.png` | Options open, search finds the row; the Microphone row reads "Desktop default" |
| `11-dropdown-open.png` | The dropdown open with exactly two entries: "Desktop default" and the C920 webcam source with its description |
| `12-chosen.png` | After choosing: the row shows the webcam source, and the row has changed from the shipped default |
| `settings-after.txt` | `device=alsa_input.usb-046d_HD_Pro_Webcam_C920_B38639BF-02.analog-stereo` written to the sandbox settings — the exact string `captureArguments()` passes as `pw-record --target=` |
| `relay.log` | The sandbox app's log |

## To try it live (your own app, not the sandbox)

Open your Relay's Options › Voice and look at the Microphone row — no staging needed, your app
already has the change once you run a build that includes commit `355c1c27`.
