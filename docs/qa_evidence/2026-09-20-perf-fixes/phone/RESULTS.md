<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The phone client, measured — #PPR4 QA item 5 and the phone's half of the #PF4K profile

Measured on 2026-09-20 against a real Google Pixel 8 (Android 17, Chrome 153.0.8010.52) watching a
real Relay pane shared over the LAN. The September profile
(`docs/qa_evidence/2026-09-20-perf-profile/`) never covered the phone; this is that column.

## What was running

**Desktop.** A clean export of the landed tip (`EXPORTED_SHA` `0beeadfc`) at
`/tmp/claude-1000/pf4k/src3`, built `RelWithDebInfo` with Ninja and Qt5 into
`/tmp/claude-1000/pf4k/build3/relay`. Run under Xvfb on a private display `:210` with a jailed
`HOME` / every `XDG_*` / `TMPDIR`, `RELAY_KEYRING=off`, `--clean-shell`, **no provider key**: the
model is the loopback stub `scripts/…/transcript/stub.py` on `127.0.0.1:8931`, configured as
`local:stub` and visible as `stub · local (main)` in the pane's model box in every screenshot here.
The GUI↔worker channel is copied by `drive.sh`'s `IPC=1` shim, so every byte table below is the
real newline-JSON wire. spark's load average was 2.4–10 throughout (five other #PF4K agents);
byte counts and CPU-time counts are unaffected by that, wall clocks are noted where they are not.

**Share.** The **LAN address only** — `https://192.168.1.192:42061`, the self-signed dev cert
(SHA-256 begins `70DE 832D 03DA 5245`). The share pane offers the tailnet name first and that is
what it preselects (`remote/gui_host.py:718`); the LAN entry was picked by hand from the address
list. No public link, no meeting code, no invite, nothing through `relay-terminal.ai`. The phone
was paired with **Allow typing** (capability `full`) after comparing the five-digit code.
One outbound internet request happens on any share: the sidecar probes
`GET https://join.relay-terminal.ai/v1/health` once at start (`remote/gui_host.py:164-181`). It is
a reachability check, not a link and not a join, and nothing was emailed.

**Phone instrumentation.** `adb forward tcp:9222 localabstract:chrome_devtools_remote` and the
Chrome DevTools Protocol: `Network.webSocketFrame*` for bytes on the air, `Profiler` for the JS
sampling profile, `Runtime.evaluate` for DOM state, a `requestAnimationFrame` interval sampler for
jank, and wrappers on `JSON.parse` / `JSON.stringify` **installed in the live page only** to count
RRP messages by type. Chrome's own pages were never modified. CPU was read as
`utime+stime` summed over every `com.android.chrome` process from `/proc`, which is a count, not a
sampled percentage.

---

## 1. #PPR4 QA item 5 — verdict: **the desktop half passes, the phone half does not, and cannot**

> *"Share a pane to a paired phone or browser and run a noisy command with Show tool output off:
> the phone's transcript must still show the output under the running call."*

**The option plumbing works exactly as the card says.** The pane's `configure` carried
`stream_tool_output: false`; the instant the share opened it sent
`{"stream_tool_output":true,"type":"set_agent_options"}`; the instant **Stop sharing** was clicked
it sent `{"stream_tool_output":false,…}`. Both transitions are in
[`evidence/qa5-ipc.txt`](evidence/qa5-ipc.txt), and there is exactly one of each — no chatter.

**The bytes come back and go away again.** One `ztools1x1` turn — a single `run_command` whose
output is 200 lines / 13 600 B — run twice on the same pane with the same stub:

| worker → GUI, one 200-line tool call | shared with the phone | share ended |
| --- | --- | --- |
| `tool_output` | 13 837 B | **90 B** (`counted`) |
| `tool_result` | 14 197 B | **445 B** (`counted`) |
| the two together | 28 034 B | **535 B** — −98.1 % |
| the whole turn | 32 997 B | **5 282 B** — −84.0 % |

**The pane is unaffected.** Both runs drew the same line, `▸ ran cat · 200 lines · exit 0`, followed
by `All batches done.` ([`evidence/desktop-shared-tool-turn.png`](evidence/desktop-shared-tool-turn.png)
is the shared one).

**The phone never shows the text, shared or not.** The 28 KB really does cross the air — the tab's
WebSocket took two binary frames of 13 927 B and 14 253 B in that turn's five seconds
([`evidence/qa5-onair.txt`](evidence/qa5-onair.txt); Noise adds a 16-byte tag and the encoding
marker to each plaintext). The phone then dropped both. Read out of the live page at that moment:

```
thread-body.hidden == true      terminal-pane.hidden == false      thread-body.children.length == 0
```

and what the phone displayed was the desktop's own line, `ran cat · 200 lines · exit 0`
([`evidence/phone-shared-tool-turn.png`](evidence/phone-shared-tool-turn.png)).

**Why it cannot work as written.** `transcribe()` is the only place `app/app.js` reads
`event.text` of a `tool_output` (`app/app.js:1023-1026`), and it is called only when the desktop
does **not** advertise the `screen` feature (`app/app.js:728`); `openTerminal()` hides the whole
transcript in that case (`app/app.js:480-482`). A Relay **GUI** pane share always advertises
`screen`: the hub sets `self.screens = hasattr(self.source, "on_screen")` (`remote/host.py:542`),
`GuiPaneSource` has `on_screen` (`remote/gui_host.py:216`), and `welcome` then carries
`"screen"` (`remote/host.py:1412-1415`). The transcript renderer is the *agent-companion*
fallback for a desktop with no terminal; the phone watching a pane sees the pane's own screen.
So the `|| sharedWithPhone()` arm of `Pane::needsToolOutputText()` (`src/Pane.h:4564`) buys nothing
on any surface — with **Show tool output** off the desktop prints a counted line, so the mirror
shows a counted line, whether the worker sent the text or not.

**What to do (an owner decision on the wording, a clear fix in the code).** The QA item should read
"the phone shows the same line the desktop shows"; the code should drop the `sharedWithPhone()`
arm, i.e. `bool needsToolOutputText(bool show) const { return show; }`, and remove
`sendToolStreamOption()` from the share-changed connection at `src/Pane.h:504`. It is worth a
sentence in `src/Pane.h`'s comment saying *why*: the phone reads the screen, not the events.
Measured gain, on the numbers above and finding 2 below: **−28 034 B per tool call on the IPC wire
and −1.33 MB per tool-heavy turn on the air while a pane is shared.** The risk is a future phone
build that mounts the transcript beside the screen; that build would have to ask for the text (a
`stream_tool_output` of its own over RRP), which is the right place for the decision anyway.

---

## 2. The profile table

Scenarios run against the same shared pane, one turn each. "worker → GUI" is the desktop's own IPC
wire; "to the phone" is RRP plaintext counted inside the page, and it is the traffic that actually
goes over wifi (or mobile data).

| | (a) 20 000-char prose reply | (b) tool-heavy: 20 calls × 2 000 lines | (c) 50 MB through the terminal |
| --- | --- | --- | --- |
| worker → GUI | 55 036 B (1 000 `delta`) | 1 356 185 B (81 % tool text) | n/a (no agent turn) |
| **to the phone** | **1 502 138 B** | **1 460 642 B** | **707 515 B** |
| biggest share | `screen_snapshot` ×166 = 1 286 042 B (86 %) | `agent` ×90 = 1 360 576 B (93 %) | `screen_snapshot` ×81 = 582 949 B (82 %) |
| next | `agent` ×1 013 = 130 144 B | `screen_snapshot` ×9 = 71 785 B | `history` ×13 = 122 167 B |
| `screen_diff` | ×86 = 32 251 B | — | ×2 = 691 B |
| `history` / `history_get` | 67 pages, 188 asked, **121 refused** | 4 pages | 13 pages |
| phone JS, non-idle self time | 2.50 s of 30 s wall (8 %) | 0.18 s of 60 s (0.3 %) | 1.21 s of 70 s (1.7 %) |
| top JS function | `fit` `app/screen.js:119` — 543 ms | `decrypt`, all < 10 ms | `fit` — 381 ms |
| rAF frame intervals | median 16.6 ms, p99 16.8 ms, max 17.5 ms, **0 of 1 053 over 33 ms** | — | — |
| desktop → phone paint | — | median 100 ms, p90 149 ms, max 3.1 s (45 markers) | — |

Idle control for the latency figure, same method with no turn running: median **82 ms**, p90 247 ms
over 26 markers. Method: `echo ZG$(date +%s%3N)` once a second in the shared pane's shell; a
`requestAnimationFrame` loop in the page records `Date.now()` the first time each token appears in
`#screen-wrap`; the phone's clock was calibrated against spark's over adb at **−229 ms** (three
consecutive minimum-RTT samples agreed to 0 ms, RTT 65 ms).

**Frames and jank.** `dumpsys gfxinfo com.android.chrome` reports **`Total frames rendered: 0`** in
every window, before and after a reset, with the screen on and Chrome in the foreground — Chrome
composites through Viz, not HWUI, so gfxinfo has nothing to say about a web page. The in-page rAF
sampler is the number that means something, and it says the phone does not drop a frame: 1 053
intervals during a full 20 000-character stream, median 16.6 ms, **none** over 33 ms.

**Battery proxy** (`evidence/idle2min.battery.txt`, `evidence/stream2min.battery.txt`), two-minute
windows, phone on charger, screen on, Relay tab in front:

| | Chrome CPU | on the wire |
| --- | --- | --- |
| idle shared pane, 120.6 s | **0.47 s = 0.4 % of one core** | **0 frames, 0 bytes** |
| streaming (1 000 chars/s) , 120.5 s | **99.29 s = 82.4 % of one core** | ~5.4 screen snapshots/s |

`dumpsys cpuinfo` over the streaming window puts 44 % on the renderer process, 15 % on
`privileged_process0` (the GPU process) and 4.3 % on the browser process. An idle shared pane costs
the phone nothing at all — no keepalive, no poll, no traffic. A *modest* stream costs it most of a
core, and finding 1 is why.

---

## 3. Findings, ranked

### 1. A terminal scroll is sent to the phone as a whole-screen snapshot — 86 % of the bytes of a streamed reply, and 82 % of a core

**What the user feels.** A phone watching an agent type burns most of a core and, on mobile data,
1.5 MB per 20 000-character reply — about 75× the text. A long session on a phone is a battery and
a data-plan problem, not a rendering one.

**Measurement.** Scenario (a): 166 `screen_snapshot` of 7 747 B against 86 `screen_diff` of 375 B,
for a reply whose own text is 20 KB. Scenario (c): 81 snapshots, 2 diffs. Chrome at 82.4 % of one
core while streaming against 0.4 % idle.

**Reproduce.** With a pane shared and the phone attached:
`scripts/cdp.py eval @scripts/rrpcount.js 42061`, then
`scripts/scene.sh prose 30 'please stream zprose20000x20r200 now'`, then
`scripts/cdp.py eval @scripts/rrpread.js 42061`.

**Root cause.** `frameOf()` sends every row whenever `frame.full` is set
(`engine/tools/ScreenJson.h:77-85`), and `ViewportFrame::full` means "every row changed (scroll,
resize, colours)" (`engine/core/CellTypes.h:158`). A line of streamed output at the bottom of the
screen scrolls the viewport, so nearly every frame is `full`. `RemoteShare::sendFrame`
(`src/RemoteShare.cpp:562-574`) passes it straight through, and `Sidecar.set_frame`
(`remote/gui_host.py:256-285`) turns it into `screen_snapshot`. On the phone, `ScreenView.apply`
then clears `this.lines`, empties `rowNodes`, calls `grid.replaceChildren(...)` and re-`fit()`s —
a whole-DOM rebuild — for each one (`app/screen.js:158-176`).

**Fix.** Give the wire a scroll primitive. The cheapest version needs no new message: on a frame
whose only reason for `full` is a scroll of *n* rows, send a `screen_diff` carrying `scroll: n`
plus the *n* rows that entered at the bottom; `apply()` shifts its row nodes by *n* instead of
rebuilding, which it must already be able to do to keep the seam honest. `ViewportFrame` would
carry `scrolledBy` beside `full` so `frameOf()` can tell a scroll from a resize or a colour change;
`full` stays true for those. That is `engine/core/CellTypes.h`, the libvterm and Ghostty cores'
frame builders, `engine/tools/ScreenJson.h`, `remote/gui_host.py`, `app/screen.js` and section 6.4
of `docs/REMOTE-PROTOCOL.md`; the client must keep accepting a plain snapshot, so an old desktop
and a new phone still agree.

**Expected gain.** The 166 snapshots become 166 diffs of a row or two: 1 286 042 B → roughly
60 KB, i.e. the turn's 1.50 MB → about **0.14 MB, −91 %**. The phone-side saving is the same
ratio of `fit` + `replaceChildren` + `paintRow` + `nearSeam`, which is 754 ms of the 2.50 s of
non-idle JS in that scenario — about half of it once V8's own `(program)` and GC time are set
aside. **Risk:** a scroll delta that disagrees with the desktop leaves the phone showing rows
that are not there; the seam arithmetic in `screen.js` (`historyBottom`/`gapRows`) is the test that
catches it, and `tests/test_remote_browser.py` already drives a real browser against a real host.

### 2. Tool output is pushed over the air to a phone that discards it — 93 % of a tool-heavy turn's bytes

**What the user feels.** An agent that runs a noisy build sends the phone a megabyte and a half it
never displays. Three of 45 screen markers arrived 1.1–3.1 s late during that turn (median was
still 100 ms), which is the head-of-line cost of it: RRP is one ordered Noise stream, so a screen
frame waits behind whatever is queued in front of it.

**Measurement.** Scenario (b): `agent` messages ×90 = **1 360 576 B** of the 1.46 MB the phone
received; the screen itself needed 71 785 B. The same turn's worker→GUI table is 81 %
`tool_output` + `tool_result`.

**Root cause.** `tool_output` and `tool_result` are in `FORWARDED_EVENTS`
(`remote/wire.py:243`) and go to any device at `view` or above (`remote/host.py:818-827`), while
the only client that renders them is the transcript fallback, which is off whenever there is a
screen (finding in section 1). Note guests are already excluded for privacy reasons and the comment
at `remote/wire.py:430-435` gives exactly this reason — *"those already reach a guest on the
screen, because Relay prints them into the terminal"*. The same is true of the owner's own phone.

**Fix.** Two halves, both small. Desktop: drop the `sharedWithPhone()` arm as section 1 says, so
the worker sends counts and there is nothing to forward. Hub: make `tool_output` / `tool_result`
forwarding conditional on the client having asked for it — a `wants` list in `hello`, defaulting to
off for a client that advertises `screen` support — so a desktop whose GUI still sends the text
(Show tool output on) does not push it to a phone that will not draw it.

**Expected gain.** −1.33 MB per tool-heavy turn on the air, −1.33 MB on the IPC wire, and the 1–3 s
screen stalls with it. **Risk:** none on today's client; a future phone transcript has to opt in.

### 3. The phone asks for scrollback faster than the desktop will answer, and shows the refusal to the user

**What the user feels.** A red line under the composer reading **"too many of those; slow down."**
while the agent is simply streaming — visible in
[`evidence/phone-rate-limited-toast.png`](evidence/phone-rate-limited-toast.png) — plus 36 KB of
scrollback pages nobody scrolled to.

**Measurement.** Scenario (a): **188 `history_get` in about five seconds**, 67 answered with a
page, **121 answered `rate_limited`** (10 769 B of error messages). The budget is
`"history_get": (120, 60)` (`remote/host.py:58`), refused at `remote/host.py:391`. In scenario (c)
the same loop fetched 13 pages, 122 167 B — 17 % of everything the phone received.

**Root cause.** `ScreenView.requestIfNeeded()` (`app/screen.js:270-289`) fires whenever a seam is
open, and a seam is open on every frame while output is scrolling: rows leave the live screen into
the scrollback, `gapRows()` becomes non-zero, and `nearSeam()` returns true unconditionally for a
gap under 160 rows (`app/screen.js:262-265`). It is called from `apply()` (`app/screen.js:191`) —
so once per screen frame — **and** from `applyHistory()` (`app/screen.js:305`), so each answer
starts the next request. `this.pending` serialises them but does not slow them down.

**Fix.** Do not chase the seam while the live end is moving: skip the seam branch when the view is
at the bottom (`atBottom()`), because a reader at the live end cannot see the hole — close it when
they scroll up, which is what `scrolled()` already calls. Add a floor of, say, 250 ms between
`history_get`s and coalesce a gap larger than one page into one request for the whole gap rather
than `HISTORY_PAGE` at a time. And surface a `rate_limited` on `history_get` as nothing at all: it
is the client's own bug, not something to put under the person's composer
(`app/app.js:499-508` sets `screenView.pending = false` on failure; the message goes to
`term-note` elsewhere).

**Expected gain.** 188 requests → single digits; −36 KB of `history` and −11 KB of errors per
streamed reply, and the error line stops appearing. **Risk:** a reader who scrolls up during heavy
output waits up to 250 ms longer for the rows above to fill in.

### 4. `ScreenView.fit()` forces a style recalculation on every frame — the phone's top JS function

**What the user feels.** Nothing on a Pixel 8 today; it is 543 ms of a 30-second window. It matters
because it is 22 % of the phone's non-idle JS and it scales with frame count, so it is the thing
that will hurt first on a cheap phone.

**Measurement.** `fit` at `app/screen.js:119` is the top non-idle frame in the CPU profile of both
scenario (a) (543 ms) and scenario (c) (381 ms).

**Root cause.** `fit()` reads `this.root.clientWidth` and then `window.getComputedStyle(this.root)`
before it can decide the font size has not changed (`app/screen.js:123-134`). Both force layout and
style resolution. It is called from `apply()` on every snapshot (`app/screen.js:175`) and from
`applyHistory()` (`app/screen.js:304`).

**Fix.** Cache the padding inset and the measured advance, and invalidate them from the
`ResizeObserver`/`resize` handler that already exists (`app/screen.js:98-99`) rather than
re-reading them per frame; return early when `clientWidth` and `cols` are both unchanged since the
last fit. **Expected gain:** `fit` to near zero — about 22 % of the phone's non-idle JS in a
streaming turn.
**Risk:** a font or padding change that arrives without a resize event would be missed; keying the
cache on `clientWidth` and the computed `font-family` covers it.

---

## Measured and fine — do not re-profile these

- **The phone's rendering.** Zero dropped frames in 1 053 rAF intervals during a full 20 000-char
  stream; p99 16.8 ms. The phone is not render-bound and no amount of DOM tuning will show up.
- **An idle shared pane.** 0 bytes and 0 frames over 15 s; 0.4 % of a core over two minutes. There
  is no keepalive, no poll, nothing to switch off.
- **Noise/WebCrypto.** `decrypt` is 50–61 ms per 1.5 MB turn, 4 % of the phone's JS. AES-GCM on
  WebCrypto is not a cost worth thinking about.
- **50 MB through the terminal.** The desktop's frame coalescing holds: 50 000 000 B of `cat` in
  73 s produced 81 snapshots and 709 KB to the phone, and Chrome stayed under 2 % of a core. The
  phone is protected from a firehose; it is the *slow* stream that costs, because every scroll is
  a snapshot (finding 1).
- **Desktop → phone latency.** Median 82 ms idle, 100 ms under a tool-heavy load. That is wifi plus
  a frame; there is nothing to win.

## Not measured, and why

- **Mobile data rather than wifi.** Everything here is one LAN hop, RTT 9 ms. The byte counts carry
  over unchanged and they are the finding; the latency numbers do not.
- **A phone on the tailnet or behind the hosted rendezvous.** Out of scope by the brief — a
  rendezvous join emails the owner. Bytes would be the same; latency would not.
- **A cheap Android phone.** One device was available. The JS numbers are a Pixel 8's; findings 1
  and 4 are the ones that would scale badly, which is why they are ranked where they are.
- **`dumpsys gfxinfo` jank.** Chrome reports no frames to HWUI at all (see above). Replaced with an
  in-page rAF sampler, which is the better instrument here.
- **A second `history_get` storm reading.** It reproduced at 188 requests in scenario (a) and at 53
  in a 6 000-char reply, but the count depends on how far the phone's scrollback buffer is from the
  live block when the turn starts, so treat 188 as an upper bound on a long-running pane and 53 as
  the routine case. Both are over the desktop's budget for the burst.

## What was changed on the phone, and put back

| Change | Restored? |
| --- | --- |
| `svc power stayon true`, i.e. `stay_on_while_plugged_in 15` (the screen slept mid-measurement at the 30 s timeout) | **Yes** — back to `0`, confirmed stable, and the phone locked itself as it did before. The pre-existing value was not read cleanly before the change; `0` is what its behaviour showed it to be (it slept at the 30 s `screen_off_timeout`, which is untouched) |
| Opened three Chrome tabs on `https://192.168.1.192:42061` (base, and the `/pair#…` link twice) | **Yes** — all three closed over CDP; the one tab that was open before is untouched |
| Accepted the self-signed certificate for `192.168.1.192:42061` once (Advanced → Proceed) | Chrome keeps that decision per origin; the origin is an ephemeral port on a machine that is no longer serving |
| Paired the phone with the test desktop (capability `full`) | The desktop's pairing record lived in a jailed `HOME` that has been deleted. The phone keeps a device key in that origin's `localStorage`, which is unreachable and harmless |
| `adb forward tcp:9222` | **Yes** — removed |
| Nothing installed, no `adb root`, no reboot, no `batterystats --reset`, no data outside Relay's page touched | — |

## The harness

`scripts/` holds everything, and each file says at the top how to run it.

| file | what it does |
| --- | --- |
| `cdp.py` | minimal Chrome DevTools client over `adb forward`: `list`, `profile`, `eval` (`@file` for a script), `console` |
| `wsbytes.py` | WebSocket frames in/out with byte counts, plus a JS sampling profile reduced to self time per function |
| `seg.py` | per-message byte table for one slice of the GUI↔worker log, the way `2026-09-20-perf-fixes/toolout/reduce.py` does for a whole file |
| `scene.sh` | one profiled scenario: types the prompt, brackets it with gfxinfo, `wsbytes.py` and `seg.py` |
| `chromecpu.sh` | Chrome's CPU over a window, summed from `/proc` across its processes, plus `cpuinfo` and `batterystats` |
| `rrpcount.js` / `rrpread.js` | count RRP messages by type inside the live page, by wrapping the `JSON.parse` / `JSON.stringify` the RRP layer already uses |
| `watch.js` / `read.js` | the rAF marker watcher used for desktop→phone paint latency |

The desktop side reuses `docs/qa_evidence/2026-09-20-perf-profile/transcript/`'s `stub.py` and
`drive.sh` unchanged (`IPC=1 KEEP=1`), which is what makes the byte tables comparable with the
`toolout/` ones.
