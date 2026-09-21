<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #PH0N wave 3 — the always-on phone remote control, live against join.relay-terminal.ai

2026-09-21, this machine (Ubuntu 24.04 aarch64, Qt 5.15). A fresh Relay profile, the real hosted
rendezvous at `https://join.relay-terminal.ai` (nothing stubbed there), a headless "phone"
(Chrome at 390×844) and a headless guest, driven end to end through the thirteen steps the card's
wave 3 asked for. Everything that failed and could be fixed here was fixed and landed; the run
recorded in this folder is of the final harness against the final code.

```sh
scripts/relay-build
docs/qa_evidence/2026-09-21-ph0n-hosted-drive/drive.sh          # one command, about six minutes
RELAY_QA_GDB=1 docs/qa_evidence/2026-09-21-ph0n-hosted-drive/drive.sh   # Relay under gdb: a crash leaves a backtrace
```

`drive.sh` is the sandbox: an isolated `HOME`/`XDG_*`/`XDG_RUNTIME_DIR`/`TMPDIR` under one short
`mktemp` directory, `RELAY_KEYRING=off` (the identity this run registers at the rendezvous is a
throwaway minted in the jail; the owner's real identity key is never read), the Xvfb display and
the fake model. `run.py` is the run: it owns the Relay process (step 12 quits and restarts it),
the clicks, the browsers and every screenshot. `fake-model.py` is a scripted OpenAI-compatible
endpoint keyed on a word in the prompt (`QUICK`, `SLOWTURN`, `ASKME`, `OFFLINEQ`, `GUESTQ`); it
logs every request to `requests.jsonl`, which is how "arrived exactly once" is proved.

Outputs: `NN-<side>-<what>.png` (side is `desktop`, `share`, `phone` or `guest`), `notes.txt`
(the PASS/FAIL/NOTE per claim — the table below quotes it), `run.log`, `relay-stderr.log` and
`relay-stderr-restart.log` (the two Relay processes; with gdb their thread noise too),
`requests.jsonl`, `timings.txt`, `provenance.txt` (the sha, the binary's build time, which files
under `app/ remote/ rendezvous/ src/` were dirty, and the served `app.js` hash against the repo's)
and `audit.txt`.

**Provenance.** The recorded run is against `main` at the sha in `provenance.txt`, with
`build/relay` built from the working tree by `scripts/relay-build` at the time shown there. This
checkout is shared by several sessions, so the tree had other sessions' uncommitted edits in
`src/` (listed in `provenance.txt`); none of them is in `remote/`, `app/` or `rendezvous/`. The
phone ran the app **as served by join.relay-terminal.ai**, which at the time was the deploy of
`8e9b2e05` (`served app.js 7c195862cf60`), not the repo's `app.js` — see step 12a.

## The thirteen steps

| # | Step | What was seen | Shots | Verdict |
|---|---|---|---|---|
| 1 | Relay starts with remote control off | A fresh profile: no `gui_host` sidecar among Relay's children 14 s after launch, no `[remote]` section in `relay.conf`, and the rendezvous's `/v1/health` counts 0 desktops before the run | `01-desktop-…` | PASS |
| 2 | The switch, address `relay-terminal.ai` | Options › Remote's "Remote control" toggled on (Options search → Return). The sidecar starts, `/v1/health` goes 0 → 1 desktops, `relay.conf` gains `remote/alwaysOn=true`; the Remote tab reads "Remote control on · relay-terminal.ai · no phone connected" with the base `https://join.relay-terminal.ai` under it, and the plug menu's first line is the same sentence | `02`, `03`, `03a-desktop-the-plug-menu-status-line`, `04` | PASS |
| 3 | Pair a phone through the hosted link; connect token | Ctrl+E for a second pane; the share window's QR link is `https://join.relay-terminal.ai/pair#…`; the five digits match on both screens (`06`, `07`); "Allow typing" pairs the phone as `full`. The device record in IndexedDB holds a 35-character connect token for the desktop id derived from the link's key. Two raw WebSockets to `wss://join.relay-terminal.ai/v1/connect?desktop=<id>&device=<id>` — one with no `ct`, one with `ct=nonsense` — are both closed `4404 "no such desktop or the pairing code expired."`, the same answer an unknown desktop gets | `05-share`, `06-phone`, `07-share`, `08-phone` | PASS |
| 4 | The inbox lists every pane | Both panes listed with `idle` chips, no share button pressed; a third pane opened on the desktop (Ctrl+E) appears on the phone by itself | `09`, `10`, `11` | PASS |
| 5 | A prompt from the phone | Sent from the pane view's box; the inbox chip reads `running · 0s` and then `finished`; the fake model's answer is in the terminal at both ends | `12`–`16` | PASS (with the `finished` fix, finding 2) |
| 6 | Stop | A two-minute turn started; Stop is in the strip with the clock "Relaying · thinking… · 3 s · step 1/500"; one tap ends it in 0.4 s | `17`, `18`, `19` | PASS |
| 7 | The agent's question | The fake model calls `ask_user` ("Which branch should the fix land on?", two options, one recommended); the row is drawn on the phone with a button per choice; tapping `main` closes it and the model's next answer is "You chose: main" | `20`–`23` | PASS |
| 8 | Recap from the pane menu | ⋯ → "Recap the conversation" prints "Recap · HH:MM → HH:MM · Nm" with a body and a Next line into the pane; no error on the phone; `requests.jsonl` shows the recap's side call reached the fake model once (finding 7) | `24`, `25`, `26` | PASS |
| 9 | The offline queue | The live WebSocket closed from inside the page and the tab taken offline over CDP; the link chip reads `offline`; a prompt typed then shows "Sends when back online · 1 message"; back online it reconnects, the prompt arrives, `requests.jsonl` shows exactly one model request for it, and the note clears | `27`, `28`, `29` | PASS |
| 10 | Notifications | "Notify me on this phone" → the desktop answers with the two switches ("When an agent finishes or fails" = agent_finished/failed/plan, "When something needs me" = waiting_input/password), both on; turning one off is kept | `30`, `31`, `32` | PASS — subscription only; delivery is Phase 3 on the real phone (`pushManager.subscribe` is stubbed in the browser, nothing else is) |
| 11 | Admit from the phone; a guest prompt | The share window opened now lists the paired phone ("Linux Chrome (Chrome) · full", finding 3). An editor invite made on the desktop (`https://join.relay-terminal.ai/join#…`); alice knocks from a second browser; the owner's phone shows "alice wants to join as editor · code NNNNN" with the guest's code; Admit lets her in; her prompt shows on the phone as "alice: GUESTQ …" with Run/Refuse; Run sends it to the agent and the model answers it | `33`–`44` | PASS |
| 12 | Restart Relay on the same profile | SIGTERM: Relay exits normally (0 `gui_crash` lines, the share window open), the rendezvous shows 0 desktops, the phone says `offline`. Started again with nothing pressed: the sidecar comes back, `/v1/health` is 1 again, and the phone opened at `/` is straight in the inbox, connected, no pairing | `45`, `46`, `47` | PASS |
| 12a | The tab the phone paired in, refreshed at `/pair` | The **served** app says "This pairing link arrived without its code … scan the QR code again" to a device that is paired. Fixed in `app/app.js` (`c6c2f72e`): after pairing the page sits at `/`, and `/pair` with no code on a paired device goes to the inbox; `tests/test_remote_browser.py` covers it. **The fix is not deployed** (`rendezvous/deploy.sh` is the orchestrator's), so the served app still shows the note | `47a` | FAIL until deployed |
| 13 | The switch off | `/v1/health` back to the count before the run, the phone says `offline`, `relay.conf` remembers `alwaysOn=false`. The sidecar process stays a child of Relay after `stop` (it answers `stopped` and idles; by design, `RemoteShare::setAlwaysOn`) | `48`, `49` | PASS |

`timings.txt` has the seconds per step; the whole run is about six minutes, of which the restart
(60 s of Relay coming up twice) and the guest flow are most.

## What it found, and what was fixed

Eight things, none of which the unit tests said. Each was fixed here except the last two.

1. **A paired phone refreshing its tab was told to pair again** (12a). After pairing, `app.js`
   left the page at `/pair` with the fragment cut; a refresh of that URL — Safari reopens a tab
   where it was — hit the "arrived without its code" branch. Now the page is put at the app's
   root after pairing, and `/pair` with no code on a device that already has a record connects
   the stored device. `c6c2f72e`, with a browser test. Needs a deploy to take effect on the
   phone.
2. **Every finished turn reached the inbox as `idle`.** `Pane::shareStatus()` answered only
   `password`, `waiting_input`, `running`, `failed` and `idle`, so the "finished" chip of Phase
   2.5 could never show from a GUI pane. It now answers `finished` after a turn that ended well,
   until the pane is used again (the same rule `failed` follows). `c6c2f72e`.
3. **The share window listed no phone.** With remote control on, the sidecar reports the device
   list once at launch, before any share window exists; a window opened later showed an empty
   list under a phone that was connected the whole time. `RemoteShare` keeps the last list, shows
   it when a window opens, and asks again. `c6c2f72e`.
4. **A SIGSEGV at quit with the share window open.** The dialog's `sharingChanged` slot captured
   the pane by raw pointer; the dialog is the window's child and the pane a splitter's, so at
   quit the pane died first and the next pane's share ending reached a freed `Pane` (backtrace in
   run 2's `relay-stderr.log`, under `~WindowManager`). Guarded with a `QPointer`. `c6c2f72e`.
5. **A line longer than 64 KiB from the GUI killed the sidecar.** Run 4 died a minute after the
   switch went on — `ValueError: Separator is not found, and chunk exceed the limit` from
   asyncio's default `StreamReader` limit — and the share window sat at "Starting…" for ever.
   The reader's limit is now 32 MiB and a line past even that is dropped with a warning while the
   link goes on. `4c0348a8`, with tests. This one is the most important find: with always-on it
   is the whole service going down on the first wide frame or big worker event — a desktop that
   shows "Remote control on · relay-terminal.ai · online" in the plug and is not reachable.
6. **No way to get a pairing link into an installed app on iOS.** The camera hands a scanned
   link to Safari, and a Home Screen web app has storage of its own, so a phone that installed
   Relay first (which it must, for Web Push) could not pair inside the installed app: the
   welcome screen had no place for a link. It now has "Or paste the pairing link", which takes
   the same path a scanned link takes; a link for another origin or a non-link is refused with a
   sentence. Browser test in `tests/test_remote_browser.py`. Needs the deploy too.
7. **The audit log records almost nothing of the run.** `audit.txt` holds `pair` and two
   `push_subscribe` rows; `invite_create`, `knock`, `admitted`, `guest_prompt`, `prompt_decided`
   are all in `remote/host.py` as `audit.record(...)` calls and none reached the file. Not
   chased here (not this drive's area; the hub's audit is #W5N2's) — noted for the orchestrator.
8. **Recap's side call goes to the Flash tier, and with no tier list that is Relay Free**, even
   with `RELAY_KEYRING=off` and the pane on a local model. Run 3 printed a recap written by a
   hosted model. The drive's profile now lists the fake model in the Flash and Lite tiers (and
   the recorded run's recap came from it); the product question (should a "keyring off" desktop
   spend hosted inference on a recap?) is the owner's, not this drive's.

Two things found on the way that were the harness's, not the product's, and are fixed in the
harness: the pane's agent worker exits in 40 ms under `systemd-run --user --scope` when the
jail's `XDG_RUNTIME_DIR` has no session bus (isolation is off in the profile, as in every other
Xvfb drive here); and the first-launch approvals chooser opens a pane at the first tool call
(`security/approvals_chosen=true` in the profile).

## What is real and what is not

Real: `build/relay` and its panes, the sidecar (`remote/gui_host.py`), the hosted rendezvous at
join.relay-terminal.ai and its registration, rooms, connect-token check and channel relay, the
Noise sessions, the pairing and invite links, the web app as served from the rendezvous, the
worker and its `ask_user`, the offline queue, the two Relay processes across a restart.

Stubbed, in the browser only: `pushManager.subscribe` (step 10) — there is no push service
reachable from this machine. What the page does with the answer, and everything on the desktop's
side (`push_subscribe` inside the Noise session, the stored choice, the two switches), is real.
No push was delivered in this run; that is Phase 3's job on the real phone.

The model is `fake-model.py` on loopback for Main, Flash and Lite. Nothing paid is reached — but
note finding 7: without the tier lists in the profile, a recap would have gone to Relay Free.

## The owner's steps on the real iPhone

What a real phone does differently from this run is in bold.

1. Deploy first: `rendezvous/deploy.sh` from `c6c2f72e` or later, so the served `app.js` has
   the `/pair` fix (12a). Check `provenance.txt`'s "served app.js" line equals the repo's.
2. On the desktop: Options › Remote → Remote control **on**, address `relay-terminal.ai`. The
   plug's menu should read "Remote control on · relay-terminal.ai · no phone connected".
3. **Install the app before pairing.** In Safari open `https://join.relay-terminal.ai/`, Share
   → Add to Home Screen, and open Relay from the Home Screen. On iOS an installed web app has
   **its own storage**, separate from Safari's: a device paired in a Safari tab is not paired in
   the Home Screen app, and Web Push is delivered only to the installed app. The app's
   Notifications row says as much on iOS when it is not installed.
4. Pair from inside the installed app. On the desktop, Options › Remote → "Pair…" (or a
   pane's share button) shows the QR and the link under it. **The camera opens a scanned link
   in Safari, not in the installed app, and the installed app has its own storage** — so pair
   inside the app instead: copy the link (click the link text under the QR on the desktop, or
   scan it with the camera and copy the link from Safari's address bar), open Relay from the
   Home Screen, and paste it into "Or paste the pairing link" on the welcome screen (needs the
   deploy from this commit or later). Pairing in a Safari tab also works, but a tab gets no push.
5. Compare the five digits, tap **Allow typing** on the desktop.
6. Leave the desk. From LTE: the inbox lists every pane; open one, send a prompt; see the chip
   go running → finished; Stop a turn; answer a question row; ⋯ → Recap; turn Notifications on
   (both switches) and wait for a lock-screen push from a turn longer than 30 s while the desktop
   window is not the active one; lock the phone for a minute and unlock it — the link should
   come back on its own (`pageshow`/`visibilitychange`) and a prompt typed while it says offline
   should show "Sends when back online" and go once; invite someone (or a second browser of your
   own) and Admit them from the phone.
7. Restart Relay on the desktop; the phone should say offline and then come back with no
   pairing.
8. Turn the switch off at the end of the day; the phone says offline; the plug menu shows off.

Things this run could not see that the phone will: iOS killing the socket seconds after
backgrounding (the reconnect is built for it, unproved on the device), Safari's certificate
handling (none needed: the origin has a real certificate), Face ID and the keyboard (#KBFT's
nine checks), and push delivery.
