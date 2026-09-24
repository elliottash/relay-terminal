# #SMDX — the Sharing pane's two pages, driven under Xvfb

`run.py` compiles `driver.cpp` against the built `librelay-sharing.a` (and the settings libraries it
links) and runs it under `xvfb-run` with a scratch XDG home and `RELAY_KEYRING=off`. The driver is the
real `relay::sharing::SharingView` with the real `Model`, fed the same JSON the sidecar sends
(`devices`, `addresses`, `participants`, `knock`, `pairing`, `pair_code`, `invite`, `code`, `ask`) and
with every hook recorded into `driver.log`. No sidecar, no window chrome, no network.

| Shot | What it shows | Done-means line |
|---|---|---|
| `01-people-empty.png` | People is the default page. Three panes are published to the owner's phones and nobody is visiting: one sentence and **Invite…**, no row per quiet pane | no row per quiet pane |
| `02-people-invite-form-on-build.png` | `startInvite(pane "build")` — what "Share this pane…" on the chip does: the form open with the scope picker on `build`, role, expiry, uses, Make a link / Make a code | the invite form with a scope picker |
| `03-people-link-made.png` | the `invite` line came back: the link, its QR, Copy link, email + Send | link + QR + email |
| `04-people-meeting-code.png` | the `code` line: meeting code, PIN, countdown, Copy | meeting code |
| `05-people-knock-guest-and-invite.png` | a knock under **Waiting for you** (Refuse first, the tab says "People · 1 waiting"), then **Shared now** headed by scope: `Pane “build”` with a live invite and its controls, `Tab “relay-terminal”` with alice | Shared now listing only panes with a guest or a live invite, headed by their scope |
| `06-devices-idle.png` | Devices: the status line and the Remote control switch, the address, one row per paired device (connected state, capability, Passwords toggle, Revoke), **Add a device…** | Devices page |
| `07-devices-pairing-offer.png` | **Add a device…** pressed: `driver.log` shows `onPairRequest` and `onPairCodeRequest` exactly once; the QR, the typed code `RPFU 9647`, its clock, Copy link, the three captions and Done, above the device list | pairing offer minted on press, not on opening |
| `08-devices-ask.png` | a phone's `ask`: the approval card at the top of the page with the five digits and Refuse / Allow viewing / Allow typing; the tab says "Devices · 1 asking" | the approval card with Refuse holding the focus |
| `09-people-again-offer-withdrawn.png` | switching to People: `driver.log` shows `onPairCodeRevoke RPFU` — leaving Devices withdraws the code | leaving Devices revokes the code |
| `10-chip-two-actions.png` | The whole Relay app in an isolated Xvfb profile: the share chip menu contains exactly **Share this pane…** and **Sharing…** | two menu actions in every state |
| `11-sharing-opened.png` | Selecting **Sharing…** opens the Sharing pane on People | Sharing… opens the Sharing pane |
| `12-top-right-sharing.png` | The whole app's top-right plug menu has **Sharing…** first, followed by Pair a phone…, the remote-control toggle and both join actions | top-right menu retains its connection actions and reaches Sharing |
| `13-top-right-sharing-opened.png` | Selecting that **Sharing…** entry while Models has focus opens the Sharing pane beside the terminal | top-right Sharing works from a tool pane |

`driver.log` records the hook calls in order. "focus after ask: (none)" is Xvfb reporting no active
window for `QApplication::focusWidget()`; `tests/sharingpane_test.cpp` (offscreen) asserts that Refuse
has the focus after `focusView()` with an ask showing.

Shots 10–11 run the built `relay` under Xvfb with separate config, data and workspace directories.
The chip was clicked with Xdotool, then **Sharing…** was clicked. Shots 12–13 repeat that path
from the top-right plug, using the exact committed-tree binary from `land.py`'s build gate.
The shared-state menu, live pairing and `RemoteShare::attach()` against a sidecar still need a
verifier's live check.
