<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #FR1C task 4 — pairing a phone without friction, live against join.relay-terminal.ai

2026-09-20, this machine (Ubuntu 24.04 aarch64, Qt 5.15). A fresh Relay profile, the real hosted
rendezvous at `https://join.relay-terminal.ai` (nothing stubbed there), headless "phones" and a
headless guest, and a fake model on loopback. Two halves in one command:

* **steps 14-19** — this card's path: the plug menu's "Pair a phone…", the code *typed* on the
  phone, a burned code, iOS install-first, the notifications offer on first arrival, and the plug
  menu's own switch off and on again;
* then the profile is wiped and **#PH0N's thirteen steps** run again on the same build, because
  this card changed the desktop entry point their third step uses and the harness is shared.

```sh
scripts/relay-build
docs/qa_evidence/2026-09-21-fr1c-hosted-drive/drive.sh          # one command, about five minutes
RELAY_QA_GDB=1 docs/qa_evidence/2026-09-21-fr1c-hosted-drive/drive.sh   # Relay under gdb
```

`drive.sh [build-dir]` is the sandbox and `run.py` the run, exactly as in
`docs/qa_evidence/2026-09-21-ph0n-hosted-drive/`: **that harness is imported, not copied**
(`run.py` loads its `run.py` by path and subclasses its `Run`), so the isolation, the fake model,
the xdotool desktop, the headless browsers and the thirteen steps all have one home. What this
folder adds is the six new steps, the profile wipe between the halves, and one more QA-only
variable — `RELAY_REMOTE_PAIR_CODE_FILE`, which is how the run learns the eight characters the
dialog is showing (the PIN is a secret, so like the pairing link it is written only when a
variable names a file: `RemoteShare::handle`).

Outputs: `stepNN-<side>-<what>.png` for steps 14-19 and `NN-<side>-<what>.png` for the thirteen
(side is `desktop`, `share`, `phone` or `guest`), `notes.txt` (the PASS/FAIL/NOTE per claim — the
tables below quote it), `run.log`, `relay-stderr-steps-14-19.log` and `relay-stderr.log`,
`requests.jsonl`, `timings.txt`, `provenance.txt` and `audit-steps-14-19.txt` / `audit.txt`.

**Provenance.** The recorded run is the fifth full run of this drive (2026-09-20, ended 22:23 local), after the
two fixes below. `provenance.txt` has the sha of the working tree (`494a750a`), the other sessions'
uncommitted files in `src/` (this checkout is shared), and the hash of every file the rendezvous
serves against the repo's.

* **The binary** is `build/relay` as `scripts/relay-build` left it at 22:04:03, at `ad8beded`, run
  from a private copy (`drive.sh <dir>`) so that another session's rebuild could not swap it under
  a five-minute run — which is what happened to this drive's second run, whose restart step came
  up on a binary built seven minutes after the first half's. `scripts/relay-build` at `494a750a`
  does not compile: another session's unfinished `exportProfiles` in `src/RelayWindow.h`. Nothing
  that landed between the two shas touches remote control or pairing.
* **The sidecar** (`remote/`) is a Python process Relay starts from the checkout, so it is the
  working tree at `494a750a` — with the first fix below in it.
* **The phone runs the app as deployed.** Two served files differ from the repo: `pane.css`
  (`831470e8`, the 185 px prompt box) and `meet.js` (`494a750a`, the second fix below). Neither
  changes a verdict here; both need the redeploy — see "What needs a redeploy".
* `/v1/health` counted **one desktop that is not this run's** the whole way through (the owner's
  own Relay, remote control on, on this machine's real display). Every count in `notes.txt` is
  therefore read against the count before the run: 1 → 2 → 1.

## Steps 14-19 — the new path

| # | Step | What was seen | Shots | Verdict |
|---|---|---|---|---|
| 14 | Fresh profile, remote control OFF; plug menu → Pair a phone… | No sidecar among Relay's children and no `[remote]` section in `relay.conf`. The plug menu reads "Remote control off", then **Pair a phone…** as its first choosable row (one Down from the top reaches it) with "Remote control: off" under it. Return on it opens the dialog, and that one act turns the service on: the sidecar starts, `/v1/health` goes 1 → 2, `relay.conf` gains `alwaysOn=true` and `address=relay-terminal.ai`. The dialog has the always-on line ("Remote control is on: your paired phones see every pane. Turn it off in the plug menu or Options › Remote."), the QR, **Copy link**, and the code `ABCD 1234` with "Expires in 9:58" counting down. The plug menu now reads "Remote control on · relay-terminal.ai · no phone connected" with "Remote control: on" ticked | `step14a`–`step14e` | PASS |
| 15 | The phone opens `/` (not `/pair`) and types the code | The welcome screen leads with "Enter the code from your desktop". The eight characters, typed a key at a time into the two fields, bring up five digits on the phone (`step15b`) and the same five in the dialog's approval row (`step15c`); **Allow typing** lands the phone in the inbox as `full`, `connected`, one screen drawn. The dialog's code row reads "Used" with a "New code" button (`step15e`); the desktop's audit has `code_used` for those letters once, with no failed try. Neither the PIN nor the link's `s=` secret is in `/v1/health`, in the desktop's audit log, or in any console line of the page | `step15a`–`step15e` | PASS |
| 16 | Three wrong PINs burn a code; a new one works | A fresh code, three wrong PINs from a second phone: the desktop's audit has three `code_attempt` and `code_burned failures=3`, and the dialog's row strikes the code through and reads "Closed — Three wrong PINs were tried, so the code was closed. Nothing was paired; make a new one." with **New code** (`step16b`). The *right* PIN on the burned code is refused and the phone stays on the welcome screen (`step16c`). The dialog's own **New code** button — pressed, not a reopened window — mints the next code, which pairs a second device (`step16d`, `step16e`) | `step16a`–`step16e` | PASS (the third sentence on the phone: fix 2) |
| 17 | iOS Safari, not installed | iPhone user agent, `navigator.standalone === false`, the dialog's `/pair#…` link: the install card ("Add Relay to your Home Screen first…", Share → Add to Home Screen) with **Copy pairing link** and the link under it; the only screen drawn is the welcome screen, and ten seconds later the desktop's audit has the same number of pairings as before the tab opened and the dialog has no approval row (`step17b`). The same link with `navigator.standalone === true` pairs — the proof the Safari tab never spent it | `step17a`–`step17c`, `step17z` | PASS |
| 18 | First arrival: "Turn on notifications" | The inbox's first element, above the pane list, is one button, "Turn on notifications", with `Notification.permission` still `default`. One tap: both switches on ("When an agent finishes or fails", "When something needs me"), the offer collapses, and the settings row becomes "Stop notifying this phone"; the desktop's audit has `push_subscribe` for that device | `step18a`, `step18b` | PASS — subscription only; `pushManager.subscribe` is the browser's one stub, and delivery to a lock screen is the real phone's to show |
| 19 | The plug menu's "Remote control: on", off and on again | Off: `/v1/health` 2 → 1, the phone's chip says `offline`, `alwaysOn=false` written. On again from the same row: 1 → 2, `alwaysOn=true`, and the phone reconnects **by itself** straight into the inbox with its pane — no pairing screen, no code | `step19a`–`step19e` | PASS |
| — | One code per look at the dialog | Five codes were minted across steps 14-19 and none was ended by anything but its own use or burn: no `code_expired` in `audit-steps-14-19.txt`. This row was red in the first three runs (fix 1) | `audit-steps-14-19.txt` | PASS |

## The thirteen, re-run on the same build

All thirteen pass, 30 checks, on a wiped profile with the same binary, jail and fake model; the
claims and shots are the ones `docs/qa_evidence/2026-09-21-ph0n-hosted-drive/README.md` tabulates,
and `notes.txt` has this run's sentences for each.

| # | Step | Shots | Verdict |
|---|---|---|---|
| 1 | Relay starts with remote control off | `01` | PASS |
| 2 | Options › Remote's switch, address `relay-terminal.ai` | `02`, `03`, `03a`, `04` | PASS |
| 3 | Pair through the hosted link; the connect token; two forged connects refused `4404` | `05`–`08` | PASS |
| 4 | The inbox lists every pane; a third appears by itself | `09`–`11` | PASS |
| 5 | A prompt from the phone: `running · 0s` → `finished` | `12`–`16` | PASS |
| 6 | Stop ends a two-minute turn in 0.4 s | `17`–`19` | PASS |
| 7 | The agent's question, answered from the phone | `20`–`23` | PASS |
| 8 | Recap from the pane menu (the side call reached the fake model once) | `24`–`26` | PASS |
| 9 | The offline queue: delivered exactly once | `27`–`29` | PASS |
| 10 | Notifications: the two switches, one turned off and kept | `30`–`32` | PASS |
| 11 | Admit a guest from the phone; run a guest's prompt | `33`–`44` | PASS |
| 12 | Quit and restart on the same profile: back by itself, no pairing; `/pair` refreshed | `45`–`47a` | PASS |
| 13 | The switch off | `48`, `49` | PASS |

`timings.txt` has the seconds per step: 110 s for the six new steps, 161 s for the thirteen —
four and a half minutes on the clock (22:19:07 to 22:23:41).

## What it found, and what was fixed

1. **The first code the dialog showed was replaced a second and a half later.** The audit of the
   first three runs reads `code_create XZPD`, `code_expired XZPD`, `code_create PVTT`. "Pair a
   phone…" on an off desktop sends `start` and, on the first `started`, `pair_code` — before the
   sidecar has moved from its loopback rendezvous to join.relay-terminal.ai. The code was minted
   where the service still was, `Hub.rehome` ended it, the move's second `started` made the dialog
   ask again, and the eight characters changed under the eyes of somebody who had already read them
   and looked down at the phone — on the first pairing a new owner ever does. The same happened
   when the plug menu's switch came back on with the dialog open. **Fixed in `15b0340f`**
   (`remote/gui_host.py`): `pair_code` waits up to 2.5 s for a move under way, and the re-ask that
   move causes is handed the same code back. Three tests; the drive's "one code per look" row is
   the live check. Runs on the desktop — nothing to deploy.
2. **The third wrong PIN said "check the four digits, then try again"** about a code it had just
   burned; the next try then said "There is no pairing code with those letters". A wrong PIN is
   found out on the page (the desktop's tag does not check out and the page hangs up), so the
   desktop never gets to tell that attempt `burned`. **Fixed in `494a750a`** (`app/meet.js`): the
   page counts its own wrong PINs per code and reports the third as `burned`, which both the
   pairing screen and the guest's join screen already have a sentence for. **Needs the redeploy**;
   the recorded run still shows the old sentence and says so in a NOTE.
3. **The prompt box no longer fitted a phone in landscape with the keyboard up** (185 px):
   `tests/test_web_viewport.py` was red on main since #PH0N 2.6 put Stop and the question row in
   the strip. **Fixed in `831470e8`** (`app/pane.css`): under 260 px the strip is one row and a
   question scrolls inside itself. **Needs the redeploy.**
4. The harness's own first red (run 1, step 16) was its expectation, not the product: a burned
   code and a code that never existed are answered alike on purpose, so "the right PIN no longer
   pairs" is the check, not a particular sentence.

## What needs a redeploy

`rendezvous/deploy.sh` (the orchestrator runs it, not this drive): `app/pane.css` from `831470e8`
and `app/meet.js` from `494a750a`. After it, `provenance.txt`'s served and repo columns match on
every row, and step 16's NOTE flips to "the served meet.js has the fix".

## What was odd, and is left as it is

* Every page load logs a CSP refusal of `static.cloudflareinsights.com/beacon.min.js`: Cloudflare
  injects its analytics beacon into the HTML and the app's `script-src 'self'` refuses it. The
  refusal is the right outcome; the noise goes away by turning Web Analytics off for the
  `join.relay-terminal.ai` hostname in the Cloudflare dashboard, which is the owner's account.
* `step17a` and every QR in these shots show a whole pairing link. Each is a one-time secret that
  was spent or expired (five minutes) before the run ended, for a desktop identity minted in the
  jail and deleted with it.
* On first arrival the inbox shows "Turn on notifications" at the top *and* "Notify me on this
  phone" at the bottom (`step18a`): the same act twice on one screen until the first is tapped.
  `tests/test_remote_browser.py` pins the pair as deliberate (#FR1C task 3), so it is left.
* The dialog's top line counts the QR's life in seconds ("The QR lasts 299 s.") beside a code that
  counts in minutes ("Expires in 9:58"). Two clocks in two units in one window.
* What the rendezvous *logs* was not read from here (no shell on that host was used). By
  construction there is nothing for it to log: the PIN never leaves the phone (CPace), a URL
  fragment is never sent to a server, and `rendezvous/server.py` logs only desktop-id prefixes,
  push failures and bad envelopes. What was checked live is `/v1/health` and the desktop's audit.

## What is real and what is not

Real: `build/relay` and its panes, the plug menu and the pairing dialog, the sidecar
(`remote/gui_host.py`), the pairing code and its CPace phase, the hosted rendezvous at
join.relay-terminal.ai and its registration, rooms, code rooms, connect-token check and channel
relay, the Noise sessions, the web app **as served by the rendezvous**, the worker and its
`ask_user`, the offline queue, two Relay processes across a restart.

Stubbed, in the browser only: `pushManager.subscribe` (steps 18 and 10) — there is no push service
reachable from this machine — and, for step 17, the iPhone's user agent and `navigator.standalone`,
which is how a Home Screen web app identifies itself and which headless Chrome cannot set itself.
What the page does with either answer, and everything on the desktop's side, is the real thing.

The model is `fake-model.py` on loopback for Main, Flash and Lite (the profile lists it in the
tier rows, so a recap cannot fall through to Relay Free — #PH0N's finding 8).

## The owner's steps on the real iPad and iPhone

Each step below is one the drive walked (the number in brackets), except the three only a real
device can do: Add to Home Screen, the system's permission sheet, and a notification on a locked
screen. Do the redeploy first, so the phone gets the fixed `meet.js` and `pane.css`.

On the iPhone, then the same on the iPad (iPadOS Safari calls itself a Mac; the app tells it apart
by its touch screen, so the same card shows):

1. Safari → `join.relay-terminal.ai`. The page says "Add Relay to your Home Screen first" [17].
2. Share → **Add to Home Screen** → Add.
3. Open **Relay** from the Home Screen. It leads with "Enter the code from your desktop" [15].
4. On the desktop: the plug at the top right → **Pair a phone…** [14]. If remote control was off,
   that turns it on; the dialog shows four letters and four digits, and ten minutes.
5. Type the eight characters on the phone → **Pair** [15].
6. Five digits appear on the phone and in the dialog. If they match → **Allow typing** [15].
7. The phone is in the inbox, with **Turn on notifications** at the top. Tap it, then **Allow** on
   the system's sheet; both switches come on [18].
8. Lock the phone, run something in a pane, and wait for the notification — the one thing no step
   here could show.

If a PIN is mistyped three times the dialog says "Closed": press **New code** [16]. To stop being
reachable: plug menu → **Remote control: on** (it unticks); the phone says offline, and ticking it
again brings the phone back with no pairing [19]. Scanning the QR with the Camera opens Safari,
not the installed app, so on iOS the typed code is the path; the QR's link can still be carried
over with **Copy pairing link** and pasted in the app [17].
