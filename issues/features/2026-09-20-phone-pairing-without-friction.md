---
id: FR1C
type: work
status: executing
labels: [feature, remote]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: claude-code
rank: 6b
created: '2026-09-20'
source: 'owner, 2026-09-20, Claude Code session (after #PH0N landed)'
acceptance: on an iPhone or iPad that has never seen Relay, the owner gets from nothing to a paired, notifying Home Screen app in three acts — install once, type the code the desktop shows, compare five digits — with no switch to find, no QR to scan and nothing to paste; and on the desktop, "Pair a phone" is one click from the window chrome and turns remote control on by itself
links: {plans: [], commits: [], evidence: [], related: [PH0N, 97EG, W5N2, KBFT], github: null}
---
# Pairing a phone without friction: one entry point on the desktop, a typed code on the phone

## Issue

take a fresh look at the user steps needs to remote control with the phone. check for frictions that we can reduce

and even step 1 of enabling remote, that was not obvious to me.

go and plan and build this efficiently with subagents so i can try it soon on ipad and iphone

## Planning notes

The path after #PH0N, as measured on the desktop and in `app/index.html`, and what each step costs:

| Step today | Friction |
|---|---|
| Options › Remote, switch on | Not discoverable. Nothing in the window says remote control exists or is off; the plug menu at the top right is "Join a shared session". |
| Safari → join.relay-terminal.ai → Share → Add to Home Screen → open | Necessary on iOS (push only reaches an installed app, and the installed app has its own storage), but nobody is told until after they have paired in Safari. |
| Options › Remote › Pair… → QR + link | The natural act, scanning, opens Safari on an iPhone, pairs a tab that gets no push, and leaves the Home Screen app unpaired. The fallback is pasting a 140-character link that has no way of getting from a Linux desktop to a phone. |
| Compare five digits, Allow typing | Fine: one trip to the desktop, which is showing the code anyway. |
| "Notify me on this phone" at the bottom of the inbox, then two switches | A second deliberate act nobody knows to do. |
| Welcome screen text | Says "share icon beside the microphone" and "both devices have to be on the same network"; both false since always-on. |

What exists to build on: #97EG's meeting code + PIN (`remote/meetcode.py`, `app/meet.js`): four letters and four digits, CPace, the server never learns the PIN, three misses burn the code, ten minutes, one use. It delivers an invite fragment to a guest. Delivering a **pairing** fragment (`s=` rather than `i=`) to the owner's own phone is the same machinery.

Not taken: a native app (nothing here needs one); Universal Links into the installed app (iOS opens links in Safari, not in a Home Screen web app); sharing Safari's storage with the installed app (iOS does not allow it).

## Plan

**Goal.** The acceptance line: install once, type the code, compare digits; and on the desktop, one click that turns remote control on and shows the code.

**Steps.**

1. **Desktop, one entry point.** The plug menu at the top right gains "Pair a phone…" as its first item, and the palette gets `remote.pair` ("Pair a phone"). Choosing it turns remote control on (hosted address) if it is off, and opens the pairing dialog. The dialog shows a **pairing code** big beside the QR ("On your phone, enter ABCD 4829"), with the countdown and "New code" the invite code already has, and a Copy button for the link. The plug menu also carries "Remote control: on/off" so the switch is one click away, and the dialog says in one line that remote control is on and where to turn it off. `src/RelayWindow.h`, `src/RemoteShare.{h,cpp}`, `src/Keymap.h`, `src/RemoteSettings.cpp`.
2. **Sidecar, a code that pairs.** `pair_code` from the GUI mints a meeting code bound to a pairing room instead of an invite; the CPace phase delivers the pairing fragment; states (`used`, `burned`, `expired`) as for invites; #97EG's conditions kept (desktop counts failures, the rendezvous keeps only the code → room map for the ttl, audit lines). `remote/meetcode.py`, `remote/gui_host.py`, `remote/host.py`, `docs/REMOTE-PROTOCOL.md` §5.2.
3. **Phone, the welcome screen.** Leads with "Enter the code from your desktop" (four letters, four digits), then the pairing; the QR/paste path stays underneath for devices where scanning works. On an iPhone or iPad that is not installed, the welcome screen and the `/pair` landing both say "Add Relay to your Home Screen first, then open it and enter the code", with a Copy-link button, and do not pair a Safari tab. Android gets an in-page Install button (`beforeinstallprompt`). After the first pairing in an installed app, the inbox leads with one "Turn on notifications" button, and granting turns both switches on. `app/index.html`, `app/app.js`, `app/meet.js`, `app/style.css`.
4. **Drive and deploy.** The hosted drive gains the plug-menu path and the typed-code pairing; deploy; evidence.

**Risks.** `src/RelayWindow.h` and `src/Pane.h` are held by other sessions: small hunks, claimed late. The pairing code shares the rendezvous code-room path with invites; a code must not be usable for the other purpose (the fragment's own shape, `s=` vs `i=`, decides, and a test pins it).

**Verify.** `tests/test_remote_meetcode.py`, `tests/test_remote_gui_host.py`, `tests/test_web_meet_code.py`, `tests/test_remote_browser.py`, `ctest -R "remotesettings|sharing"`, then the hosted drive, then the owner's iPad and iPhone.

## Tasks

- [ ] 1 Desktop: "Pair a phone…" in the plug menu and the palette turns remote control on and shows the code beside the QR <!-- t:p1 -->
- [ ] 2 Sidecar: `pair_code`, a meeting code that delivers a pairing fragment <!-- t:p2 -->
- [ ] 3 Phone: welcome screen leads with the code; iOS install-first landing; Android install button; notifications offered on first arrival <!-- t:p3 -->
- [ ] 4 Drive, deploy, evidence <!-- t:p4 -->
