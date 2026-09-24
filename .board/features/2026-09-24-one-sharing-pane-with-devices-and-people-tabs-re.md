---
id: SMDX
type: work
status: needs-verification
labels: [feature, remote, gui]
component: [gui, remote]
milestone: beta
workstream: remote
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 0504c59c-0055-4610-bdc9-6b96e78cdee9
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [ai-visual, person], human: optional, criteria: 'The pane share chip offers exactly Share this pane… and Sharing… in both states; the top-right plug menu also opens the Sharing pane via Sharing… while retaining Pair a phone…, remote control and join actions.', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: owner, 2026-09-24, Claude Code session in Relay
links: {plans: [], commits: [0dd9c980, e4b67044, 9390fa3a, 4dba06bb, ff15ddbe], evidence: [docs/qa_evidence/2026-09-24-smdx-sharing-pane/], related: [SHRP, A11T, PRM2, FR1C, W5N2], github: null}
---
# One Sharing pane with Devices and People tabs replaces the share window; share chip offers Share this pane and Sharing

## Issue
help me think through the below design change.

can we also integrate the "Sharing" pane with the window that opens when you click pair a phone.

move the material from the phone pairing page into the sharing pane.

think about whether we need multiple subtabs in the pane.

dont list all the panes like it does now.

how about, when you click the share icon in a pane, it shows two options, share this pane or share more

share this pane takes you directly to a share pane tab with that one pre-selected.

share more takes you to the sharing page.
there is a share pane button or share workspace button

then help me think, does it need to decide between
app sharing with another computer running relay
browser sharing on a PC
browser sharing on a phone

for share pane, it can then list the panes.

for share workspace, it can be this project or all projects.

the current "share all tabs" window is not well designed, kind of disorganized and confusing, and redundant and mixes things up. try to organize the sharing feature material better across sub-tabs or sub-pages. you can look at how other software does it if appropriate.

[after the analysis] i agree, put this design on a card and implement it with subagents

## Decisions
- Owner, 2026-09-24: "i agree, put this design on a card and implement it with subagents" — the who-first split (Devices / People), the scope picker inside the invite form instead of scope checkboxes, the share chip's two-row menu, no listing of quiet panes, Join stays in the plug menu, and no third tab.
- Pairing is not in the share chip's menu: pairing has nothing to do with a pane. The plug menu, Options › Remote, the palette and the Devices tab reach it.
- "This project" means this tab (one project per tab today). A project spanning two tabs would need a new scope token in the protocol and is out of scope here.
- No choice between another Relay, a PC browser and a phone: the same pairing link and the same invite link serve all three. The Devices page shows the code, the QR and Copy link with one caption per delivery.

## Done means
- The "Share this pane" window (`RemoteShareDialog`) no longer exists. Every entry point (share chip, pane menu, plug menu, palette, Options › Remote) lands on the Sharing pane.
- The Sharing pane has two pages. **Devices**: the remote-control switch, the address, one row per paired device (connected state, capability, Passwords toggle, Revoke), "Add a device…" showing the QR, the typed code, Copy link and one caption per way of using it (phone, another Relay, any browser), and the approval card with Refuse holding the focus. **People**: Waiting for you; Shared now listing only panes with a guest or a live invite, headed by their scope (pane, tab, everything); the invite form with a scope picker (any pane, this tab, everything), role, expiry, uses, link + QR + email, meeting code. No row per quiet pane.
- A pane's share chip menu has exactly two actions in every state: "Share this pane…" (People, form open on that pane) and "Sharing…" (open the Sharing pane). The chip itself still shows share status. "Pair a phone…" opens Devices with a pairing offer started; leaving Devices or pressing Done revokes the code.
- Failure looks like: an extra menu row; a dialog still opening from any entry point; quiet panes listed on People; a pairing code minted on opening the pane rather than on "Add a device…"; a knock or a device ask not surfacing on the right page.
- The top-right plug menu includes “Sharing…” as a direct route to the same pane, alongside Pair a phone…, the remote-control toggle and the two join actions. It works when a terminal or a tool pane has focus.

## Plan
**Goal.** One surface for sharing: the Sharing pane, split by *who* (Devices / People), with scope as a property of each invite. The share window goes.

**Findings.** `src/RemoteShare.cpp` ~1012–2028 is `RemoteShareDialog`: pairing QR + typed code (#FR1C, #PRM2 gate), address picker, approval box, the two scope checkboxes (#A11T), invite form, meeting code (#97EG), device list. `src/SharingPane.{h,cpp}` is the pane (#W5N2, #SHRP): top line + Pair a phone…, waiting, guests, and a row per quiet pane. Entry points: `Pane::shareChipPressed/toggleShare` (src/Pane.h ~7255), the pane menu (src/RelayWindow.cpp ~245), the plug menu and Options › Remote (src/RemoteSettings.cpp), `RelayWindow::pairPhone/openSharingPane` (src/RelayWindow.h ~4216, ~6296). Since #PH0N every pane is published to paired devices, so per-pane "start sharing" is legacy and the quiet-pane list says nothing.

**Contract** (written first, in `src/SharingPane.h` §"what the view is handed and what it asks for (#SMDX)" and `RemoteShare::attach()`): `Scope{Pane|Tab|All}`, `Service`, `DeviceAsk`, `SharedPane.tab`, `Device.capability/passwords`; `SharingView::{showPage, startInvite, startPairing, stopPairing, set*/show*}` and hooks. `RemoteShare::attach(view)` wires every sidecar-only hook and signal and replays known state; the window wires `onScopes`, `onRemoteSwitch`, `onCreateInvite`, `onCreateCode`.

**Steps** (four subagents by file area, one landing):
1. A — `SharingPane.{h,cpp}`, `tests/sharingpane_test.cpp`: tab bar, Devices page (switch, address, device rows, Add a device… with the #PRM2 gate, approval card), People page (waiting, shared-now by scope, invite form with picker, link/email, meeting code), port of the dialog's state machines and sentences; `qrPixmap`, `pairingIntro`, `codeIntro` move here.
2. B — `RemoteShare.{h,cpp}`: delete the dialog; `attach()`, `service()`, `pendingAsk` (an `ask` also emits `needsOwner` with an empty pane id); `SharedPane.tab` filled.
3. C — new `src/RelayWindowSharing.cpp` (openSharingPane, pairPhone, shareThisPane, shareMore, sharingScopes, inviteToScope, codeForScope), share chip menu in `Pane.h`, pane menu, Keymap descriptions, Options Pair row, RemotePane wording, tests.
4. D — docs: ARCHITECTURE, REMOTE-PROTOCOL, VALIDATION.
5. Parent — integration build, tests, land via land.py (Pane.h and RelayWindow.h are contested by other live sessions: confirm hunks by hand), Xvfb screenshots as evidence.

**Risks.** Pane.h/RelayWindow.h hunks contested with #SCN9 and #7QSK sessions — land only our hunks. A tab that shares a project with another tab is one scope per tab (decision). `docs/qa_evidence/2026-09-22-remote-delivery/driver.cpp` compiled against the dialog; it is evidence, not built by CMake, and is left alone.

**Verify.** `ctest -R '^(sharing|remotesettings|remotepane)$'`; `scripts/relay-build` clean; screenshots of Devices (idle, offer live, ask), People (empty, form open on a pane, a knock, a tab-scoped block) and the chip menu under Xvfb.

## Tasks
- [x] A: SharingView with Devices and People pages, tests <!-- t:0f -->
- [x] B: RemoteShare loses the dialog, gains attach()/service()/pendingAsk, SharedPane.tab <!-- t:qa -->
- [x] C: window side — chip menu, RelayWindowSharing.cpp, pane menu, Options, RemotePane wording <!-- t:vy -->
- [x] D: docs <!-- t:7a -->
- [x] Integrate: build, tests, land, screenshots, needs-verification <!-- t:vn -->


## Tests
`ctest:sharing` (28 cases: model + both pages)
`ctest:remotesettings`
`ctest:remotepane`
`manual: docs/qa_evidence/2026-09-24-smdx-sharing-pane/10-chip-two-actions.png`
`manual: docs/qa_evidence/2026-09-24-smdx-sharing-pane/11-sharing-opened.png`
`manual: docs/qa_evidence/2026-09-24-smdx-sharing-pane/12-top-right-sharing.png`
`manual: docs/qa_evidence/2026-09-24-smdx-sharing-pane/13-top-right-sharing-opened.png`

## Execution Summary
Landed in `0dd9c980` (the redesign) and a follow-up (Devices page order + evidence). Four subagents by file area against a header contract written first (`src/SharingPane.h` §#SMDX, `RemoteShare::attach()`).

- **`src/SharingPane.{h,cpp}`** — `SharingView` is two pages under a tab bar. *Devices*: status line + Remote control switch, the approval card (Refuse first, holds the focus, scrolls into view), "Add a device…" → the pairing card (QR, typed code from `remotesettings::pairCodeRow`, Copy link, one caption per way of using the link, Done), the address picker, one row per paired device (connected, capability, Passwords toggle, Revoke). The #PRM2 gate is ported: nothing is minted until the sidecar is online at its base, once per offer, revoked on Done / leaving the page / destruction. *People*: Waiting for you; Shared now only for panes with a participant or a live invite, headed `Pane “…”` / `Tab “…”` / `Everything`; the invite form with a scope picker built from `onScopes()` (panes grouped by tab, tabs, Everything), role/expiry/uses, Make a link / Make a code, link + QR + Copy + email, the meeting-code state machine. No quiet-pane rows. `qrPixmap`, `pairingIntro`, `codeIntro` live here now.
- **`src/RemoteShare.{h,cpp}`** — `RemoteShareDialog` deleted (−1078 lines). `attach(view)` connects every sidecar signal to the view and sets every sidecar-only hook, replaying service, addresses, devices and a pending `ask`. `service()`, `pendingAsk()`; an `ask` also emits `needsOwner` with an empty pane id; `SharedPane.tab` filled.
- **`src/RelayWindowSharing.cpp`** (new) — `openSharingPane` (attach + the window's four hooks), `pairPhone` → Devices with `startPairing()`, `shareThisPane`, `shareMore`, `sharingScopes`, `publishScope` (a pane with the switch off, a tab, all tabs are published before `createInvite`/`createCode`). `needsOwner` with an empty pane id opens Devices in the window that answers device asks.
- **`src/Pane.h`** — the share chip opens a two-row menu ("Share this pane…", "Share more…"; a state row and "Sharing" once the pane is shared). `toggleShare` gone. **`src/RelayWindow.cpp`** — the pane menu keeps "Share this pane…" and "Sharing". Keymap descriptions, Options › Remote's Pair row, RemotePane's pairing-link wording updated.
- **Docs** — ARCHITECTURE §"Your own devices, other people, and the Sharing pane", REMOTE-PROTOCOL's UI mentions, VALIDATION's row.

Left in the tree on purpose: the #J0VY session's uncommitted hunks in `Pane.h`, `RelayWindow.h`, `AppCommands.cpp` (excluded at landing). `docs/qa_evidence/2026-09-22-remote-delivery/driver.cpp` compiled against the old dialog; it is evidence and was left alone.

**Owner's menu correction (2026-09-24, `9390fa3a`).** The share chip now shows exactly two actions in every state: "Share this pane…" and "Sharing…". The disabled status row and "Share more…" action are removed; share status remains on the chip. The pairing guidance, accessibility name and docs use the new wording. The exact committed tree built, and `sharing` plus `remotepane` tests passed. Xvfb captures of the real app show the menu and that selecting Sharing… opens the Sharing pane: ![Two-action share menu](docs/qa_evidence/2026-09-24-smdx-sharing-pane/10-chip-two-actions.png) ![Sharing pane opened](docs/qa_evidence/2026-09-24-smdx-sharing-pane/11-sharing-opened.png)

**Top-right menu check (2026-09-24, `4dba06bb`, `ff15ddbe`).** The plug dropdown lacked a route to Sharing. It now shows “Sharing…” first, then Pair a phone…, remote control and both join actions. The new action opens the Sharing pane from a terminal or tool pane. `ctest:remotesettings` passed, and the exact committed tree built. Xvfb captures of that binary show the menu and the Sharing pane opened while Models had focus: ![Top-right Sharing menu](docs/qa_evidence/2026-09-24-smdx-sharing-pane/12-top-right-sharing.png) ![Sharing opened from top right](docs/qa_evidence/2026-09-24-smdx-sharing-pane/13-top-right-sharing-opened.png)

## Try it
1. Restart Relay so the new binary runs. Click the share button on an unshared pane and a shared pane: both menus show exactly two actions, "Share this pane…" and "Sharing…". The chip itself carries the share status.
2. "Share this pane…" opens the Sharing pane on People with the invite form already on that pane. "Sharing…" opens the Sharing pane, where the People invite form's scope picker offers panes by tab, the tab, and Everything.
3. Plug menu › Pair a phone… opens Devices with the QR, typed code and Copy link above your paired devices. Press Done to withdraw the code.
4. Pair a phone through the code: the approval card appears at the top of Devices, Refuse has the focus, and the tab reads "Devices · 1 asking".
5. Make a link for a pane, open it in a browser, knock: People shows "Waiting for you" and the tab reads "People · 1 waiting"; after admitting, "Shared now" lists that pane only.
6. No window titled "Share this pane" opens from anywhere (chip, pane menu, plug menu, palette `pane.share` / `remote.pair`, Options › Remote › Pair…).
7. Click the top-right plug while a terminal or tool pane is active: “Sharing…” is the first enabled entry, above Pair a phone…, remote control and the join actions. Select it: the Sharing pane opens beside a terminal without starting pairing.
