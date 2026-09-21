<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# #SWPH task 4 — the Switchboard on the phone, live against join.relay-terminal.ai

2026-09-21, this machine (Ubuntu 24.04 aarch64, Qt 5.15). A throwaway Relay profile on a throwaway
project with a small real Switchboard, the real hosted rendezvous at
`https://join.relay-terminal.ai` (nothing stubbed there), a headless "phone" at 390×844 running the
web app **as deployed**, a second device paired for viewing only, a headless guest, and a fake
model on loopback. One command, one Relay, one paired phone, two halves:

* **#FR1C's steps 14-19** first, unchanged and on the same build: they are how this run gets a
  phone paired *by typed code* at `full`, notifications switched on, and a desktop that has been
  switched off and on again. They all still pass.
* then **this card's thirteen steps** on that same phone.

```sh
docs/qa_evidence/2026-09-21-swph-hosted-drive/drive.sh                       # build/ and this checkout
docs/qa_evidence/2026-09-21-swph-hosted-drive/drive.sh <build-dir> <source-dir>   # a clean export
```

`drive.sh` is the sandbox and `run.py` the run. #PH0N's and #FR1C's harnesses are **imported by
path, not copied** (`run.py` subclasses #FR1C's `Drive`, which subclasses #PH0N's `Run`), so the
isolation, the xdotool desktop and the headless browsers have one home. What this folder adds:

| File | What it is |
|---|---|
| `fixture.py` | the project: `.switchboard/` made by `relay_core.board.scaffold()`, ten cards through `new_card()` / `write_new_card()`, threads through `Board.append_thread()` — the worker's own calls — committed to git. `W8TQ` waits on the owner with the agent's three-option question; `P7AN` has a `## Plan`, an acceptance and `<!-- t:xx -->` task markers |
| `fake-model.py` | #PH0N's scripted model (loaded by path) plus `DISCUSSIT`, `SLOWCARD`, `PLANIT <ID>` (a real `board_read` → `board_update_card replace_section Plan` round trip) and the two hand-offs, `Execute #ID:` / `Verify #ID:` |
| `sidecar_tap.py` | the real sidecar, started through `RELAY_REMOTE_DIR`, with three methods wrapped so that they also write a line: what the hub handed each channel (`board_event`, `error`, `welcome` — never a text), each `board_request` it was sent, each push it handed the rendezvous, each request it made of the rendezvous. Everything between hub and phone is inside Noise; this and the page tap below are the only two places it can be read |
| `run.py` | the steps; a **frame tap** in every page (every message `app/rrp.js` decrypted, with a path scan); OCR of the desktop window for the status line and the desktop board's counts |
| `crash-after-pane-close/` | the crash this drive found, reproduced without a phone under gdb, before and after the fix |

Outputs: `swNN?-<side>-<what>.png` for this card's steps and `stepNN?-…png` for #FR1C's (side is
`desktop`, `share`, `phone` or `guest`), `notes.txt` (PASS/FAIL/NOTE per claim — the table quotes
it), `timings.txt`, `provenance.txt`, `audit.txt`, `hub-tap.jsonl`, `phone-board-frames.jsonl`
(every `board_event` the phone decrypted, whole), `phone-frames-summary.txt`, `push-body.json`,
`board-after/` (the cards and threads the run wrote, as they were left on disk), `requests.jsonl`,
`run.log`, `relay-stderr.log`.

## Provenance

The recorded run is the seventh of this drive, 2026-09-21 08:53:01 to 08:57:25 EDT (4 min 24 s;
`timings.txt` has each step), **39 notes, 0 failures**.

* **The binary is a clean export of `main` at `ca055cd1`** — `git archive ca055cd1 | tar -x`,
  `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo`, `--target relay`, in a scratch directory — so it is
  what `main` builds to, with nobody's uncommitted work in it, and it has all four of this drive's
  fixes. The sidecar (`remote/`) and the worker (`backend/`) are that export's too: the binary
  starts both from its own source tree. The shared checkout was not built.
* **The phone runs the app as deployed** (from `39c94b91`). `provenance.txt` has the hash of every
  served file against the export's: two differ, `board.js` and `pane-theme.css`, both this drive's
  own fixes — see "What needs a redeploy".
* `/v1/health` read `desktops: 0` before and after, and 1 while this run's desktop was up. (During
  the earlier attempts another desktop — not this drive's — was registered; every count in
  `notes.txt` is read against the count before the run.) Nothing of the owner's was touched: the
  profile, the data, the runtime directory and `TMPDIR` are one `mktemp` jail, `RELAY_KEYRING=off`,
  its own Xvfb display, and the jail is removed at the end.

## The thirteen steps

| # | Step | What was seen | Shots | Verdict |
|---|---|---|---|---|
| 1 | The inbox leads with the Switchboard; a view-only device gets no row | The phone paired by typed code (`full`) has the **Switchboard** row above its pane: "1 waiting on you", "harbour · 9 open cards"; the app badge was set to 1. The welcome's `features` has `board`. The second device, paired with **Allow viewing**, has the pane and no row; none of its four welcomes (it reconnected through step 19) has `board`, and it was sent no `board_event` | `sw00a-b`, `sw01a`, `sw01c` | PASS |
| 2 | Open it | Tabs All, features, bugs, design, marketing, planning, deferred, done (board.yaml's order). **Waiting on you** first with #W8TQ, then Inbox 2, Discussing 2, Planning 2, Planned 1, Executing 1, Needs verification 1, Needs QA 0, Verified 0, Done 1 — equal to the files counted by the desktop's own column rules, and to the desktop's Switchboard pane read off the window by OCR | `sw02a`, `sw02b` | PASS |
| 3 | The waiting card; Answer | Body by section (Issue, Thread), no `<!--` and no `t:xx` anywhere in the text, the thread with the question set apart (3 px amber edge against a comment's 1 px), three option buttons, the send button reading **Answer**. Option 1 prefills "1. "; Answer puts a `decision` in the thread on the phone and in `.switchboard/threads/W8TQ.md`: `owner, from Linux Chrome: “1. Above the map, and keep it to three lines.”`. The desktop's status line: "Comment on #W8TQ from Linux Chrome" | `sw03a-d` | PASS |
| 4 | A note; a move with a reason | The note is in `threads/N2BX.md` once as `kind=note`. **Move…** → "agreed on the quay this morning" → Planned: the file's front matter went `planning` → `planned`, the reason is in the thread, the phone said "Moved to Planned." and its list has #D4MV under Planned with no reload; the desktop said "Card #D4MV moved to Planned from Linux Chrome", and its own board pane counts Planned 2, with the toast "#D4MV · Planning → Planned" | `sw04a-e` | PASS |
| 5 | New card from the phone | One new file, `status: inbox`, the words verbatim under `## Issue` — a double space and a line break included; the phone opened it (#FPHK in this run) | `sw05a-b` | PASS (the status line was not caught by the OCR this time; the second attempt read "New card #P46A from Linux Chrome") |
| 6 | Search | "zeppelin" is in no title and in #S3ZP's body: the list is #S3ZP alone | `sw06a` | PASS |
| 7 | Discuss, Stop, Plan | Discuss: Stop and a working line on the card, the lamp on its row, then the agent's reply as a thread entry on the phone and in `threads/Q2HK.md` (once). Stop on a two-minute turn: "Stopped." 0.4 s after the tap, the busy mark gone (finding 2). Plan on #P7AN: the `## Plan` changed on the phone and in the file, through a real `board_read` → `board_update_card` round trip; the card went Planning and back to Planned | `sw07a-g` | PASS |
| 8 | Execute; Verify | Execute: "#P7AN is executing in a new pane." with **Open pane**; the desktop said "Execute on #P7AN from Linux Chrome"; the file is `status: executing` with `session` set and "Execute pressed on Linux Chrome." in its thread; the inbox went 1 → 2 panes; the link opened that pane on the phone, where its agent had been handed the card and answered for it. Verify on #V6RF: "#V6RF is being verified on Fake in a new pane.", and that pane's agent was handed the card | `sw08a-e` | PASS — the verifier is the fake local endpoint (see "What is real") |
| 9 | `card_waiting` | With another window focused and **no Switchboard pane open on the desktop** (so the bridge's own file watch is what noticed), #R4SK was made to wait on the owner on disk the way an agent does it. The hub handed the rendezvous one push; opened, it is `{"v":1,"kind":"card_waiting","pane":"","card":"R4SK","title":"A card is waiting on you","body":"#R4SK"}` — no title, none of the question. The row went "1 waiting on you" → "2 waiting on you" and the badge 1 → 2. The tap opens that card, both ways in: posted to the open page, and cold at `?card=R4SK` (the query dropped again) | `sw09a-c`, `push-body.json` | PASS — the subscription and the tap are the browser's stubs |
| 10 | Offline | One socket cut and the page offline: the comment reads "Comment · sends when back online", the strip "Offline · the board as of 08:56 AM · 1 card change · sends when back online", the entry waits dashed in the thread, nothing on disk. Back online it is in `threads/N2BX.md` **exactly once** and once on the phone, no longer pending, the strip cleared | `sw10a-b` | PASS |
| 11 | A guest and a `view` device | A guest admitted to one pane as an *editor*, and the view-only device, each sent `board_request {board_comment}` from inside its own session. Each got an error and nothing else ("a guest never gets that: the Switchboard is the owner's cards and threads, not the shared pane." / "this device is paired for view."); while the owner's phone then wrote a note, the guest's page decrypted 0 `board_event`s and the view device's 0; of the 90-odd `board_event`s the hub sent in the run, none went to anything but a `full` device (`hub-tap.jsonl`). The audit log has `board_refused` for both, with the type and `not_permitted` and neither the text nor the card (finding 3) | `sw11a-d`, `audit.txt` | PASS |
| 12 | No machine path | The phone's page decrypted 303 messages, 94 of them `board_event`s (131 KB, all nine kinds, kept whole in `phone-board-frames.jsonl`): none holds the project's path, the jail's, `/home/` or `/tmp/`, none has a path-named key, and neither do the errors, the welcomes or `push_state` | `phone-board-frames.jsonl`, `phone-frames-summary.txt` | PASS for the Switchboard; the pane protocol does carry paths, as before — see "What stays open" |
| 13 | iPad; landscape with the keyboard | 1180×820: laid out for a tablet, the list at x 0-425 and the open card at x 425-1180, nothing wider than the screen. 844×185: the reply box and Comment, Discuss and Plan wholly on screen | `sw13a-b` | PASS |
| — | Rooms | The run asked the hosted rendezvous for 17 of the 20 rooms an hour it allows a desktop, none refused | `hub-tap.jsonl` | PASS — and see "What stays open" |

## #FR1C's steps 14-19, on the same build

| # | Step | Verdict |
|---|---|---|
| 14 | Fresh profile, remote control off; plug menu → Pair a phone… turns the service on | PASS |
| 15 | The phone opens `/` and types the code; five digits on both sides; Allow typing → `full` | PASS |
| 16 | Three wrong PINs burn a code; the dialog's own New code pairs a second device | PASS |
| 17 | iOS Safari, not installed: the install card, nothing paired; the same link pairs in the installed app | PASS |
| 18 | First arrival: "Turn on notifications", one tap, both switches on | PASS |
| 19 | The plug menu's switch off and on: the phone reconnects by itself, no pairing | PASS |
| — | One code per look at the dialog (no `code_expired`) | PASS |

Between 18 and 19 this run pairs its second device (**Allow viewing**) with the code the dialog was
still showing, so that device too goes through the off and on. `notes.txt` has each sentence.

## What it found, and what was fixed

1. **Relay crashed when the phone touched the board after the desktop's Switchboard pane had been
   closed.** Bus error, no `gui_crash` line (the jump is into freed memory). `m_consoles` kept each
   agent console's `TabConsoleContext`, whose destructor writes to the *host's* context; the list
   was pruned lazily, inside `deliverToConsoles`, so a closed pane's wrapper lived until the tab's
   worker next spoke and then wrote into a freed `BoardView`. Any event of that worker does it; a
   phone's request is simply what makes it speak while no pane is open — which is the ordinary
   state of a desktop being driven from a phone. **Fixed in `7fce9ef8`** (`src/RelayWindow.h`, one
   hunk): the entry goes with its console, on `destroyed`, by address as well as by null QPointer
   (a widget emits `destroyed` from `~QWidget`, before its guards are cleared — the first attempt
   found that out). `crash-after-pane-close/repro.sh` reproduces it with a stub sidecar:
   `gdb-before.txt` is the backtrace, `after.txt` the same script answered with `board_written`.
   Desktop only — nothing to deploy.
2. **Stop from the phone ended the turn in 7 ms and left Stop and the working line up for good.**
   `board_cancelled.cards` is "what is still running" (sessions protocol 19.16, whose own example
   leaves the stopped card out), but the worker read it while the stopped turn's thread was still
   unwinding, so it named the card it had just stopped. The desktop never noticed — its pane gets
   the turn's own `cancelled` a moment later — but a device is sent none of a card turn's events
   (remote protocol 17.4), so that list is all that clears its lamp. **Fixed in `7b5e00c8`**, both
   halves: the worker leaves the stopped card out (desktop side, so the app *as deployed* is
   fixed — this run shows it: "Stopped." 0.4 s after the tap), and `app/board.js` takes the stopped
   card's lamp out whatever the list says (**needs the redeploy**). Both tests updated.
3. **A `board_request` refused at the gate left no audit line.** A guest's, and a `view` or `agent`
   device's, is refused in `Channel._dispatch` and never reaches `Host._on_board_request`, the only
   place `board_refused` was written — so somebody let into a pane who tried the board was the one
   asker the log did not show. **Fixed in `271849fe`** (`remote/host.py`, §17.1, a test): the gate
   records `board_refused {device | participant, type, code}`, no text, at most once a minute per
   channel. Desktop only.
4. **`tests.test_web_theme.test_committed_file_is_current`** was red since #AGNT changed
   `src/Theme.cpp`. **Fixed in `0fda2acb`**: `scripts/gen-web-theme.py` run as it is (the agent
   colour, the four priority tokens, the console rules). **Needs the redeploy.**
5. The harness's own reds, for the record: the expected section list forgot that the desktop
   always ends with Verified and Done; the fake model picked `PLANIT` out of an attached card's
   thread for the Execute pane (a hand-off now wins over a keyword); the offline strip is above the
   list, which a phone with a card open has slid away; and `import` waits for ever on a window that
   has gone, which is how finding 1 cost twenty minutes (`run.py` now checks Relay is alive).

## What needs a redeploy

`rendezvous/deploy.sh` (the orchestrator runs it, not this drive): **`app/board.js`** from
`7b5e00c8` and **`app/pane-theme.css`** from `0fda2acb`. `provenance.txt` shows exactly those two
files differing between what is served and the source tree, and nothing else. Neither changes a
verdict here: finding 2 is fixed on the desktop side as well, and the theme is colours.

## What stays open, and why

* **The hosted rendezvous allows a desktop 20 rooms an hour, and one look at the pairing dialog
  costs 2-4** (a pairing link and a pairing code are a room each, and the dialog asks again on
  every `started`). #FR1C's six steps ask for 16; this whole run for 17. The second attempt
  of this drive, which reopened the dialog twice more, was refused its invite with "/v1/rooms
  failed: 429 too many pairing rooms this hour" printed at the top of the dialog. **For the
  owner's pairing session: do not open "Pair a phone…" more than five times in an hour** — pairing
  an iPhone and an iPad is two. Not fixed here: the dialog and its codes are #FR1C's
  (`remote/gui_host.py`, `src/RemoteShare.cpp`), the obvious fix (hand a reopened dialog the
  unspent room it already has) changes what #FR1C's step 17 pins, and the limit itself is a
  security setting on the server.
* **After the plug menu's switch goes off and on with the dialog open, the dialog still shows a
  code the rendezvous no longer resolves** (the third attempt typed it for the view-only device and
  got no five digits). Reopening the dialog mints a good one. #FR1C's, same files.
* **Answering a question does not clear `waiting_on: owner`.** The phone keeps saying "1 waiting on
  you" after **Answer**, and so does the desktop: nothing in the worker clears the field, only an
  agent that picks the answer up does. Whether an owner's `decision` should hand the card back
  (`waiting_on: agent`, or cleared) is a rule of the board — `board_policy` and the generated
  `issues/POLICY.md`, which is not this drive's to touch — so it is the owner's call.
* **A card turn that ends without saying anything leaves the phone's lamp on.** The lamp goes out
  on the agent's thread entry, an `error`, or `board_cancelled`; a turn that ends `done` with no
  text sends a device none of them. Not seen here (the fake model always answers, as models do).
  The clean fix is one more allow-listed event from the bridge when `agent_finished` arrives —
  bridge, hub allow-list, phone and §17.3 together — which is a change to the contract the three
  halves were built against, so it is left for the card's owner to schedule.
* **No `board_key` from the desktop bridge** (it strips `root` before the hub can hash it). One
  window, one board here, so it did not matter: the `card_waiting` bookkeeping keys every board as
  `""`. It will matter with two windows on two projects. Recorded, as asked.
* **No busy strip on the desktop's card page for a turn started from the phone** —
  `src/BoardPane.cpp`, held by other sessions. `sw07b` is the desktop while the turn runs.
* **The pane a Verify opens is headed "Switchboard · Execute #V6RF"** (`src/Pane.h:17241` writes
  "Execute" for both). The desktop's own Verify does the same; `src/Pane.h` is held by others.
* **The pane protocol names this machine's paths**, as it did before this card: `panes[].cwd`
  (§6.3, the inbox's right-hand label), the terminal's own picture, and — the one that looks like
  an oversight — the pane agent's `conversations` event, which carries each conversation's
  `session_dir`. `full` devices only, inside Noise. `phone-frames-summary.txt` has a sample of
  each. No `board_event` has one.
* Main did not work for twenty minutes of this drive, and not because of this card: `cda3defe`
  (#Z00M) bound `Ctrl++`, the worker's key parser refused it, and **every `configure` failed** —
  no pane, helper or console could build an agent (`4d540c0e` fixed it, another session's). The
  run of this drive that fell in that window failed steps 7 and 8 with "Configure a provider and
  workspace first."; the recorded run is on a tip after the fix.

## What was odd, and is left as it is

* `step17a`, `sw00a` and every QR in these shots show a whole pairing link or code. Each is a
  one-time secret that was spent or expired (five and ten minutes) before the run ended, for a
  desktop identity minted in the jail and deleted with it.
* Every page load logs a CSP refusal of Cloudflare's analytics beacon (`notes.txt`, the console
  lines). The refusal is the right outcome; #FR1C's README says where to turn the beacon off.
* The toast in `sw08e` of the attempt that fell in #Z00M's window read "key 'Ctrl++': empty part":
  that was the whole outage, in one line, on the desktop. The recorded run has no such toast.

## What is real and what is not

Real: the `relay` binary and its panes, the plug menu and pairing dialog, the desktop bridge
(`src/BoardRemote.cpp`), the window's `BoardWorker` and its card turns, the Execute and Verify
hooks and the panes they open, the sidecar and hub (`remote/`), the hosted rendezvous and its
rooms, codes, connect tokens and channel relay, the Noise sessions, the web app **as served by the
rendezvous**, the offline outbox, the card files and threads on disk.

Stubbed, in the browser only: `pushManager.subscribe` (no push service is reachable from here, and
the hosted rendezvous will post to nothing that is not one — it answered the stub endpoint with
400, which is right); the notification's *tap* (a headless page has none: the page is posted the
card id as `app/sw.js` posts it, and opened cold at `?card=`); and the iPad and landscape sizes are
device metrics, not devices. The push **body** is the real one: what the hub sealed and encrypted
and handed the rendezvous, opened with the stub subscription's private key and the page's own seal
key (`push-body.json`). The model is `fake-model.py` for Main, Flash and Lite.

`PATH` is cut down to the system's, so neither `claude` nor `codex` can be offered as a verifier:
step 8's Verify ran on the fake local endpoint, which the recommendation ranks last and takes when
nothing else is there. A Verify on a real second provider is the owner's to see, with his keys.

## The owner's steps on the iPhone and the iPad

Do the redeploy first. Each step is one the drive walked (the number in brackets), except what
only a real device can do: Add to Home Screen, the system's permission sheet, a notification on a
locked screen, and dictation.

**Pair** (once per device; #FR1C's README has the pictures). Safari → `join.relay-terminal.ai` →
Share → **Add to Home Screen**; open **Relay** from the Home Screen; on the desktop, plug →
**Pair a phone…**; type the eight characters; compare the five digits; **Allow typing** [14, 15].
Tap **Turn on notifications** and allow it on the system's sheet [18]. *Open the pairing dialog
once per device: it costs rooms (above).* The iPad is the same; iPadOS Safari calls itself a Mac
and the app tells it apart by its touch screen.

**Use the Switchboard.** The desktop needs Relay running with remote control on, and a tab standing
in a project that has a Switchboard. It does **not** need a Switchboard pane open [9].

1. The inbox leads with **Switchboard**, and says how many cards wait on you; the app icon's badge
   counts them with the panes that need you [1].
2. Tap it. Tabs across the top; **Waiting on you** first, then the stages in the desktop's order;
   Done starts folded. Pull down, or ↻, to refresh [2]. On the iPad the list and the card sit side
   by side [13].
3. Tap a waiting card. The question is the amber entry; its options are buttons. Tap one — the
   reply starts "1. " — add words if you like, then **Answer**. It is on the card as your decision,
   quoted, and the desktop says "Comment on #ID from <device>" [3]. (The card keeps saying
   "waiting on you" until an agent picks the answer up — see what stays open.)
4. On any card: type and **Comment**; **Move…** → a reason → the stage [4].
5. **+** → a title and what you want → **Create**. It is filed in Inbox with your words verbatim
   and opens [5]. To dictate, use the keyboard's microphone; the 🎤 in the sheet records a clip and
   has the desktop transcribe it (not driven here: a headless page has no microphone worth the
   name — `tests/test_board_view.py` covers the round trip).
6. **Search cards…** finds words in titles at once and in card bodies a moment later [6].
7. **Discuss** sends your words to the card's agent; the card shows a working line and **Stop**
   until the answer lands in the thread. **Plan** needs no words and rewrites the card's `## Plan`
   [7].
8. **Execute** opens a terminal pane on the desktop with the card handed to its agent; the phone
   says "#ID is executing in a new pane." with **Open pane**, which opens that pane in the inbox.
   On a card in Needs verification the button is **Verify** [8]. A card with no plan and no
   acceptance asks you to tap Execute twice.
9. Lock the phone. When an agent makes a card wait on you — and you are not looking at Relay on
   the desktop — the phone rings "A card is waiting on you · #ID": the id and nothing else. Tapping
   it opens that card [9]. One ring per card per minute, and ten seconds between any two.
10. No signal: a comment, a move or a new card says "sends when back online" and lands once when
    the phone is back; Discuss, Plan, Execute and Verify say they need your desktop [10].

A guest you invite to a pane, and a device you paired with **Allow viewing**, never see the row,
are never sent a card, and are refused if they ask — and now it is in the audit log when they do
[11].
