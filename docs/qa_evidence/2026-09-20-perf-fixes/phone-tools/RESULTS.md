<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Tool text stops crossing the air to a phone that draws the screen — #3H5T item 2, #PPR4

The fix for finding 2 of `docs/qa_evidence/2026-09-20-perf-fixes/phone/RESULTS.md`, landed in
`13772bdf`, measured on the owner's Pixel 8 over the same LAN share, with the same stub provider
and the same `ztools` scene as the profile it is compared against.

## What changed

* **Hub.** `may_forward_with_screen` / `SCREEN_REDUNDANT_EVENTS` (`remote/wire.py`), applied in
  `Host._agent_event` *before* the ring, so `tool_output` and `tool_result` are neither forwarded
  nor replayed after a gap while the desktop advertises the `screen` feature. A share with no
  screen — a source with no `on_screen`, the headless/agent-companion shape — still gets both.
  `tool_output_get` is untouched on either.
* **Desktop.** `Pane::needsToolOutputText()` loses its `|| sharedWithPhone()` arm and the
  `set_agent_options` flips that went with it on share start and end, so the worker stops
  streaming the text while a pane is shared.

## What was running

The **landed tree** exported by `scripts/land.py`'s build gate
(`/tmp/claude-1000/land/pf-phonetools/verify/src`, tree `d6f0ae6511a4` = commit `13772bdf`), built
by the same gate, run with `RELAY_REMOTE_DIR` pointed at that same export so the sidecar is the
landed `remote/` too. Under Xvfb on a private display `:210`, jailed `HOME` / every `XDG_*` /
`TMPDIR`, `RELAY_KEYRING=off`, `--clean-shell`, **no provider key**: the model is the loopback stub
`.../transcript/stub.py` on `127.0.0.1:8941`, visible as `stub · local (main)`. The GUI↔worker
channel is copied by `drive.sh`'s `IPC=1` shim, so the byte tables are the real newline-JSON wire.

**Share:** the **LAN address only**, `https://192.168.1.192:39389`, self-signed dev cert (SHA-256
begins `B447 C05E EB11 CE6F`), picked from the address list. No public link, no meeting code, no
invite, nothing through `relay-terminal.ai`. The phone was paired with **Allow typing**
(capability `full`) after comparing the five-digit code (`17210`).

**Scene:** `ztools20x2` — 20 `run_command` calls of `cat big.txt` in ten batches of two, the file
2 000 lines / 28 000 B, so the tool text per call matches the profile's scenario (b) (~27.5 KB).

**Phone instrumentation:** `adb forward tcp:9222` and the Chrome DevTools Protocol; the RRP
counters of `../phone/scripts/rrpcount.js`, extended here to record the inner **worker event name**
of every `agent` message, so "no tool text reached the phone" is a count and not an inference; and
the `requestAnimationFrame` marker watcher of `../phone/scripts/watch.js` against `echo
ZG$(date +%s%3N)` once a second in the shared pane's shell. The phone's clock was calibrated
against spark's over adb at **−151 ms** (three samples agreeing to 7 ms, RTT 115 ms; the profile's
run measured −229 ms at RTT 65 ms, so treat the absolute latencies as ±60 ms and the *shape* of
the distribution as the finding).

---

## 1. Bytes on the air, one tool-heavy turn

| | before (`../phone/RESULTS.md` scenario b) | after (this run) |
| --- | --- | --- |
| `agent` messages to the phone | **90 × 1 360 576 B** | **74 × 23 882 B** |
| of which `tool_output` + `tool_result` | 1.33 MB | **0 B, 0 messages** |
| everything to the phone, no marker loop running | 1 460 642 B | **138 451 B** |

−98.2 % of the `agent` traffic; −1.34 MB per tool-heavy turn. Every worker event the phone received
in that turn, counted inside the live page:

```
tool_started    20 ×  8 871 B      status           11 × 1 542 B
turn_summary     1 ×  5 437 B      queue_changed     3 ×   613 B
delta           21 ×  2 585 B      queued            1 ×   216 B
context         11 ×  2 253 B      agent_started     1 ×   188 B
requests         3 ×  1 861 B      agent_finished    1 ×   161 B
                                   done              1 ×   155 B
```

`tool_started` is the control: the line the phone draws for a running call is not tool *text* and
it still arrives, 20 of them for 20 calls. There is no `tool_output` and no `tool_result` row.

**The phone still shows the call.** Read out of the live page at the end of that turn: the screen
carries all twenty of the desktop's own lines, `▸ ran cat · 2,000 lines · exit 0`, then
`All batches done. ✦ 20 tool calls · 1 s`, while `thread-body.hidden == true` and
`thread-body.children.length == 0` — the transcript renderer is off, as it always was.

## 2. Screen-marker lateness, the same turn

Desktop → phone paint for a token echoed into the shared pane's shell once a second, over the
tool-heavy turn:

| | before | after |
| --- | --- | --- |
| markers | 45 | 127 |
| median | 100 ms | **72 ms** |
| p90 | 149 ms | **123 ms** |
| max | **3.1 s** (3 markers 1.1–3.1 s late) | **319 ms** |
| over 1 s | 3 | **0** |

The tail is what the fix was for: RRP is one ordered Noise stream, so a screen frame waited behind
whatever was queued in front of it, and 1.33 MB was queued in front of it. With nothing there, the
worst marker of 127 is 319 ms.

## 3. Bytes on the IPC wire, the same turn

`worker → GUI`, the desktop's own channel, for the whole 20-call turn:

| | before (scenario b, shared) | after (shared) |
| --- | --- | --- |
| whole turn | 1 356 185 B (81 % tool text) | **30 875 B** |
| `tool_output` | ~0.6 MB of text | **20 × 1 820 B**, counted |
| `tool_result` | ~0.5 MB of text | **20 × 8 971 B**, counted |

−97.7 %. And the option is no longer flipped by a share at all: over the whole session the
GUI→worker channel carries exactly one `"stream_tool_output": false` — the `configure` — and no
`set_agent_options` on share start or end, where the profile's run recorded one of each.

## 4. What this does **not** cover

`screen_snapshot` is untouched here and is finding 1 of the card, which another session
(`pf-scroll`) is fixing. It dominates whenever the screen scrolls: the turn in section 2 ran a
marker loop that scrolls the pane once a second, and its 159 snapshots came to 1 186 639 B — the
same whole-screen-per-scroll behaviour, with nothing to do with tool text. The 138 451 B in section
1 is the turn run *without* the marker loop, which is what makes it comparable with the profile's
scenario (b).

## Reproduce

```
RELAY_REMOTE_DIR=<tree> RELAY_BIN=<tree's relay> KEEP=1 IPC=1 PORT=8941 \
    ./drive.sh after 2                       # boot under Xvfb with the stub
# share the pane from the pane chrome, pick the LAN address, pair the phone (Allow typing)
adb -s 192.168.1.184:46849 forward tcp:9222 localabstract:chrome_devtools_remote
python3 cdp.py eval @evcount.js  <port>      # counters, including the inner event name
python3 cdp.py eval @watch_zg.js <port>      # the rAF marker watcher
# type `please run ztools20x2 now` into the pane
python3 cdp.py eval @evread.js   <port>
python3 cdp.py eval @read_zg.js  <port>
python3 seg.py <ipc>/worker2gui.jsonl <w0> <w1>
```

`evcount.js` / `evread.js` are in `scripts/` beside this file; everything else is the phone
harness in `../phone/scripts/`, unchanged. Install the counters on a page that has the **pane**
open, not the pane list, and do not reload the page: the pairing secret is single use, so a reload
past `/pair#…` drops the device record (the share panel says so, and it cost this run one attempt).

## What was changed on the phone, and put back

| Change | Restored? |
| --- | --- |
| Opened two Chrome tabs on `https://192.168.1.192:39389` (the `/pair#…` link twice) | **Yes** — both closed over CDP; the tabs that were open before are untouched |
| Accepted the self-signed certificate for that origin once (proceeded through the interstitial) | Chrome keeps that decision per origin; the origin is an ephemeral port on a machine that is no longer serving |
| Paired the phone with the test desktop (capability `full`) | The desktop's pairing record lived in a jailed `HOME` that has been deleted. The phone keeps a device key in that origin's `localStorage`, which is unreachable and harmless |
| `adb forward tcp:9222` | Left in place **on purpose**: another #PF4K session has a tab of its own open on this phone and is using it. It was verified present, not added by this run's teardown |
| `stay_on_while_plugged_in` | Not touched; read back as `0`, as the earlier profile left it |
| Nothing installed, no `adb root`, no reboot, no data outside Relay's page touched | — |
