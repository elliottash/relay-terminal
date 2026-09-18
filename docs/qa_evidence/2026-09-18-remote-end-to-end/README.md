# #W5N2 end to end, live — evidence, 2026-09-18

The whole feature in **one Relay session**: a phone, a guest, a second device of the owner's, and
the desktop that answers all three. Driven under Xvfb against a real `build/relay`, its real
sidecar, the rendezvous the sidecar runs in process, and three real headless Chromes.

`drive.sh` is the sandbox — the isolation, the X server, the certificates, the local model and the
Relay process. `run.py` is the run: the clicks, the browsers, the fake push service and every
screenshot. It is one coroutine rather than a shell script with flag files, because the desktop and
the browsers take turns all the way through.

```sh
systemctl --user start llama-bonsai        # the pane's agent; nothing here spends a provider key
cmake --build build
docs/qa_evidence/2026-09-18-remote-end-to-end/drive.sh
```

Outputs: `NN-<side>-<what>.png` (side is `desktop`, `share`, `phone`, `guest` or `viewer`),
`notes.txt` (a PASS/FAIL/NOTE per claim, which is what the table below quotes), `run.log`,
`relay-stderr.log`, `audit.txt`, `push-body.json` and `provenance.txt`.

**Read `provenance.txt` first.** This is a checkout several sessions commit to at once, and the
Python half of the product is read live off disk while `build/relay` is whatever was last built,
so that file records the commit the run was against, when the binary was built, and which files
under `app/`, `remote/`, `rendezvous/` and `src/` were dirty at the time. The pane view the phone
shots show was another session's uncommitted work while this ran.

## The run

`notes.txt` is the verdict list and every line of it points at a shot below. Nothing here is a
QA sign-off: it is the implementer's own live drive, and what it found is in **What it
found**.

A shot's number is its **name**, not its place in the run: the password step moved to the
end (a pane left sitting at a password prompt takes the keyboard from the palette and the
share window, so nothing else is allowed to depend on it), and renumbering would have
renamed every file the notes point at. The sections below are in the order the run does
them.

### 1. A pane worth sharing

| Shot | What it shows |
|---|---|
| `01-desktop-a-pane-with-output-and-a-local-agent.png` | An engine pane with output, and `/local` having pointed its agent at llama-server |

### 2. The phone

| Shot | What it shows |
|---|---|
| `02-share-the-share-window-offers-the-phone-first.png` | The share window from the palette's "Share this pane": the address picker, the QR, and "Invite someone to this pane" under it |
| `03-phone-the-code-on-the-phone.png`, `04-share-the-same-code-on-the-desktop.png` | The five digits on both screens, derived independently at each end from the Noise handshake |
| `05-phone-paired-inbox.png` | Paired with **Allow typing**: the capability chip reads `full`, and the pane list is the desktop's |
| `06-phone-the-shared-pane.png` | The shared pane on the phone, with the new pane view mounted: the desktop's own terminal, strip, model and prompt box |
| `07-phone-a-command-through-the-box-on-top.png` | A shell command typed into the box a thumb reaches for — **the pane view's** — and where it actually went |
| `07a-phone-the-command-the-phone-ran.png`, `08-desktop-the-phones-command-in-the-real-pane.png` | The same command through the client's own box, which sends `agent: false`: it runs in the desktop's shell |
| `09-phone-the-agent-answering-on-the-phone.png`, `10-desktop-the-agent-answering-in-the-pane.png` | A prompt that is not a command reaches the pane's agent, and the answer prints into the same terminal at both ends |
| `11-phone-the-phone-scrolled-back.png` | 400 numbered lines printed, then the terminal dragged to the top: `notes.txt` says how many rows of history came back and whether the whole column is one unbroken run |
| `12-phone-the-phone-took-over.png`, `13-desktop-the-line-the-phone-typed.png` | Take over, and a line typed from the phone reaching the program |
| `13a-phone-after-the-owner-typed-in-the-pane.png`, `13b-desktop-the-owner-typing-while-the-phone-held-the-keyboard.png` | What happens when the owner types while the phone holds the keyboard |
| `14-phone-the-notify-row-before-the-tap.png`, `15-phone-what-the-tap-says-with-no-push-service.png` | "Notify me on this phone" before and after a real tap, with no push service reachable |
| `16-phone-subscribed-with-a-box-per-kind.png` | Subscribed: a checkbox per kind, from the `push_state` the desktop sent back |

### 3. Voice

| Shot | What it shows |
|---|---|
| `17-phone-the-phone-is-recording.png` | Recording, through Chrome's fake capture device: the microphone says "Stop recording and transcribe" |
| `18-phone-transcribing.png`, `19-phone-what-came-back-from-the-clip.png` | The clip carried to the desktop and what came back — here a readable error, because this run has no provider key |

### 4. A guest

| Shot | What it shows |
|---|---|
| `26-share-…`, `27-share-an-editor-invite.png`, `28-share-the-link-and-its-qr.png` | An editor invite made from the share window: role, expiry, uses, the link as a field and its QR |
| `29-desktop-typing-before-the-knock.png` | The keyboard is in the prompt box before anybody knocks |
| `30-guest-the-invitation.png`, `31-guest-waiting-to-be-let-in.png` | The invite link's join screen and the knock, with the five digits and a countdown |
| `32-desktop-the-knock-opened-the-sharing-pane.png` | The Sharing pane opened **by itself**, Refuse first, and the same five digits as shot 31 |
| `33-desktop-the-keyboard-never-left-the-prompt-box.png` | Still typing into the prompt box with the knock row up: opening the pane never costs the keyboard |
| `34-desktop-alice-on-the-people-list.png`, `35-guest-admitted-as-an-editor.png` | Admitted as editor, with her platform, key fingerprint and when her access ends |
| `36-desktop-alice-asks-to-type.png`, `37-guest-asked-to-type.png` | "alice asks to type in this pane", Refuse first, and the guest's side of the wait |
| `38-desktop-alice-is-typing.png`, `39-guest-alice-has-the-keyboard.png` | The handoff: the pane header reads **alice is typing** |
| `40-guest-the-command-alice-ran.png`, `41-desktop-alices-command-in-the-owners-shell.png` | A line from the guest running in the owner's shell |
| `42-desktop-typing-took-the-keyboard-back.png`, `43-guest-the-guest-was-told-and-kept-the-half-line.png` | The owner types: control comes straight back, the guest's UI flips, and their half-typed line is still theirs |
| `44-desktop-a-guest-prompt-waiting-with-its-whole-text.png`, `45-guest-waiting-for-the-owner.png` | A guest prompt waiting with the **whole** text, Refuse and Approve once, and no "Approve always" |
| `46-desktop-the-approved-prompt-running-on-the-owners-key.png`, `47-guest-approved.png` | Approved once: it reaches the agent, and the row names its author |
| `48-desktop-a-second-prompt-waiting.png`, `49-guest-the-second-prompt-was-refused.png` | A second prompt refused, and the guest told why |

### 5. A second device of the owner's

| Shot | What it shows |
|---|---|
| `50-share-a-second-device-asking.png`, `51-viewer-a-viewing-device-cannot-type.png` | Paired with **Allow viewing**: no prompt box, no Take over |

### 6. A password prompt — run last, after the guests have gone

| Shot | What it shows |
|---|---|
| `20-phone-typing-refused-and-no-password-field.png` | A real `getpass` started at the desktop, with the terminal's echo off — not a tool call printing the word. **No password field is offered** to a device that has not been allowed one, which is what the shot shows. The *wording* of the refusal is not in this run: the hub's message lands in the terminal's note line and the next pane update (about one a second) writes the pane's own state over it, and this drive reads that line rather than recording every value it takes. `SecretInputTests` covers the refusal itself |

| `21-share-the-device-selected-passwords-off.png`, `22-share-passwords-on-for-this-device.png` | The per-device password switch, off by default, turned on — **but on the wrong row**: by this point the list holds two devices and the drive clicked its middle, so the switch went to the *viewing* device (`· view · passwords` in shot 22) and not to the phone. Shot `23-phone-whether-the-password-field-appeared.png` is the phone with no field on it forty seconds later, and there is no `24` or `25`: **password entry from a phone is not proved here**. `SecretInputTests` covers the nonce, the termios check and the redaction; `device_list_y()` now aims at the first row so a re-run reaches the phone |

### 7. Pause, demote and remove (before 6), then the notification and Stop sharing

| Shot | What it shows |
|---|---|
| `52-desktop-guests-are-paused.png`, `53-guest-the-guest-is-told-why.png` | Pause, and the reason the guest is given |
| `54-desktop-demoted-to-viewer.png`, `55-guest-the-guests-typing-ui-is-gone.png` | Demoted: the guest's typing UI is not hidden, it is gone |
| `56-desktop-nobody-here-now.png`, `57-guest-this-invitation-has-ended.png` | Removed, and what the guest is told |
| the notification | Last in the run: a second X client takes the focus (the note says which window has it, so the presence rule of section 9 is satisfied and not assumed), a password prompt starts, and the local push service is watched for what the hub sends. **Nothing arrived in this run**, so there is no `push-body.json` here. The subscription is real and the desktop stored it (shot 16); `tests/test_remote_push.py` delivers one to a local service end to end. What is still unshown is a GUI pane's own prompt reaching it |
| `58-share-the-share-window-before-stopping.png`, `59-desktop-the-desktop-after-stopping.png`, `60-phone-…`, `61-viewer-…` | Stop sharing, and where each browser ends up |
| `audit.txt` | The whole story in order, from `invite_create` to `leave` (section 10.6) |

## What it found

Seven things, none of which a unit test was going to say. The first three belong to the pane view
the phone grew the same day (`app/pane.js`, the `pane_state` parts of `app/app.js`); the rest span
the client, the hub and the desktop. `notes.txt` has the verdict line for each, and the card
`issues/features/2026-09-17-remote-phone-and-multiplayer.md` carries them under "What is left".

1. **A command typed into the box on the phone goes to the agent, not the shell** (shot 07). The
   pane view draws the pane's own prompt box and `app/app.js` hides the client's older one under
   it, but `compose()` in `app/pane.js` emits `{t: 'compose', pane, text, when}` with no `agent`
   field, and `Host._on_compose` reads a missing `agent` as `true`. `printf "…"` from the box a
   thumb reaches for is handed to the agent, which runs it as a tool call on the owner's key; the
   run counts the `remote:` attribution lines before and after to tell that apart from the output
   merely appearing. The client's own box sends `agent: false` for a `full` device and routes
   properly (shot 07a).
2. **Two prompt boxes.** `ensurePaneView` sets `$('composer').hidden = true`, and `updateDriveUi`
   sets it back from its own rule on the next pane update, so the client's box reappears under the
   pane view's — visible in most of the phone shots from 11 on.
3. **A voice transcript can land out of sight.** `voiceDone` appends the words to
   `#composer-text`, the box the pane view hides. The microphone moves into the pane's strip and
   records (shots 17-19); what comes back has nowhere to be read.
4. **Two of the five notification triggers cannot fire from a GUI pane.** `Pane::shareStatus()`
   returns only `password`, `running` or `idle`, and it is the only source of a pane's status on
   the sidecar line, so `remote/notify.py`'s `waiting_input` and `failed` transitions are
   unreachable from the app. The password trigger does fire, and `push-body.json` is the body it
   produced.
5. **The owner's own phone is not in the one control book.** Section 10.3 lists "a `full` device
   typing" among the changes that go through `remote/control.py`, but `Host._on_control_request`
   sends an empty keystroke for a device and never calls `ControlBook.claim_device` (which exists
   and is documented for exactly this). So `panes[].control` never reads `remote:<device>` while a
   phone drives, `Pane::takeBackFromGuest()` sends no `control_take` because no guest is named as
   the driver, and `app/app.js` follows no `control` at all: after the owner types, the phone goes
   on saying "You have the keyboard" and its next line still lands (shots 13a, 13b). Whether one
   driver per pane should include the owner's own phone is a product decision; the three files it
   would touch are named so it is one change and not three.
6. **A guest's name does not reach the turn's attribution line.** `submitRemote` puts the display
   name on `QueueEntry::author`, which is drawn for a *queued* row; a prompt that starts at once
   goes through `startAgentEntry`, which copies `entry.why` into the prompt and not `entry.author`,
   so the pane prints `guest:<hex>` under the text (shot 46). Section 10.4 asks for the name.

7. **Scrollback paging on the phone is slow enough under load to look broken.** Four runs of this
   drive, the same code and the same 150 s of dragging the terminal to the top: two paged 383 rows
   back inside the window and joined the whole column 1..400 with no gap and no repeat, two paged
   nothing inside it. In one of those two, shot 13a — taken a minute later — has `scrollback-1`
   onward on the phone, so the pages did arrive, just not while anybody was still asking. On a
   busy machine the page-ahead is slower than a person dragging, and the phone shows an empty
   space where the history will be. `notes.txt` says which way this run went.

## What is real and what is not

Real: the desktop and its panes, the sidecar, the rendezvous (in the sidecar's process), the Noise
sessions, the invite and pairing links, the web app as it is served out of `app/`, the agent (a
local llama-server, chosen with `/local` before anything else happens), the push crypto, the VAPID
signature and the HTTPS delivery.

**`RELAY_KEYRING=off` no longer means "this run cannot reach a provider".** Relay Free landed the
same day, and shot 10 has its banner in the pane: *"this pane's prompts and tool context go to
Relay's hosted service and on to the model provider"*. Every turn in this run went to
`bonsai-2-27b` on loopback — the transcript names it, and the run picks the local model with
`/local` in its first step — but a drive that leaves the model alone would now reach a hosted
service rather than refusing. Anything written later that assumes an unkeyed profile is an offline
one should set the local model first, as this one does.

Two things are stood in for, both because this machine cannot reach the thing itself:

* **`pushManager.subscribe`**, which talks to Google's push service. The stub is installed in the
  browser and returns a subscription whose endpoint is a local HTTPS service this run stands up
  with the development certificate, and which the sidecar trusts through `SSL_CERT_FILE`.
  Everything after the stub is the product: the page generates its own seal key, sends a real
  `push_subscribe` inside the Noise session, and the body that arrives at the service is opened
  here with the page's own key. Shot 15 is what the row says when nothing is stubbed at all.
* **transcription**, which needs a provider key this run deliberately cannot reach.

## One thing this run does *not* isolate: `tailscale serve`

The share window's address picker now offers the tailnet name first, and taking it means Relay
runs `tailscale serve --bg` (`remote/tailnet.py`, landed the same day) — which is **one
configuration for the whole machine**, not for this sandbox. This run took it, so the invite in
shot 28 is a `…ts.net` link with a real certificate, and when the run was over the configuration
was still there, pointing at the sandbox's dead port. Two consequences worth knowing before
running this again:

* it has to be put back afterwards (`tailscale serve status --json` should print `{}`), and only
  when the proxy target is the port this run used — never a configuration somebody else made;
* the owner's own Relay cannot use tailnet serving while a stale one is in place, because
  `tailnet.py` correctly refuses to take over a configuration it did not write.

## Isolation

`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, **`XDG_RUNTIME_DIR` and `TMPDIR`** are all
fresh temp directories: without the last two a Relay already running on this machine shares
sockets and temp files with the run and its saves look broken. `TMPDIR` also has to be *short* —
Chrome's singleton socket path has a hard length limit, and a deep temp directory kills every
browser in the run at startup, which is worth knowing before spending an hour on it.

The X root is 2600×1300 while the Relay window is 1600×1000. The share window grows past 1000 px
once a device is paired and a link is made, and X keeps no pixels for the part of a window hanging
off the screen, so an `import` of one comes back cropped. Both windows sit wholly on the root.

`RELAY_REMOTE_PAIR_FILE` and `RELAY_REMOTE_INVITE_FILE` are the QA-only hooks that write the
pairing and invite URLs out; a link holds a one-time secret and is never logged.
