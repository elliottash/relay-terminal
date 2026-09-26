---
id: ZQWY
type: work
status: planned
rank: zzzzzzzzzzzzzzzzzy
created: '2026-09-25'
source: 'remote: iOS Safari'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# I can’t create pains in the phone app. I think I should be able to do that.

## Issue
I can’t create pains in the phone app. I think I should be able to do that.

## Done means
A paired owner phone (`full` capability) has a **New pane** control in its inbox: tapping it creates a new terminal pane on the desktop and the phone lands in it, with no desktop interaction. The new pane also shows up in the phone's pane list like any other. Guests and `view`/`agent` devices never see the control, and the hub refuses the message if they send it anyway. Failure looks like: no control on the phone, a pane created on the desktop but absent from the phone's list, or the phone left on the old pane after tapping.

## Plan
**Goal.** Let a paired owner phone create a new terminal pane on the desktop from the inbox, mirroring how `conversation_new` already lets it start a conversation — the phone gets a New pane control, the desktop creates and publishes the pane, and the phone opens it.

**Findings.**
- The phone (`app/`, a PWA) can list, open and type into panes but nothing in the protocol creates one: `remote/wire.py`'s `CLIENT_TYPES` table has no pane-creation message. Its inbox is rendered by `renderInbox()` (`app/app.js:660`) from the `panes` event (`app/app.js:2009`).
- The existing path for a device action is device → hub handler (`remote/host.py`, `_on_*` family, dispatch at `handle`, line 1765) → GUI sidecar (`remote/gui_host.py`) → `src/RemoteShare.cpp`'s sidecar-line chain (lines 169-500), which emits a signal the window connects to — the `board_request` branch (line 483, connected in `src/RelayWindow.h` ~5226-5242) is the template for a window-level request; `conversation_new` (line 302) is the template for a pane-level one.
- Pane creation already exists on the desktop: `RelayWindow::createPane(spec)` (`src/RelayWindowCore.cpp:808`) plus the `insertBeside(...)` split the `pane.splitRight` action performs. A new leaf pane is published to the phone automatically.
- Gates to extend: capability table and `GUEST_NEVER` (`remote/wire.py:165`, pattern `conversation_new` line 245), the rate-limit table (`remote/host.py:132`), and the hub's audit log.

**Steps.**
1. `remote/wire.py`: add `pane_new` to `CLIENT_TYPES` as `FULL`-only and to `GUEST_NEVER` with a reason ("a shared device cannot add panes to the owner's desktop").
2. `remote/host.py`: `_on_pane_new` beside `_on_pane_focus` (line 2499) — forward `{"t":"pane_new","device":…,"name":…}` to the GUI channel the way `_on_board_request` does (line 3247), record an audit entry, and add a rate-limit entry (e.g. `(6, 60)`) near line 132.
3. `src/RemoteShare.{h,cpp}`: handle `pane_new` in the sidecar-line chain (near `conversation_new`, line 302), emitting a new `paneNewRequested(device, name)` signal, and log it with the other device lines.
4. Window side, connected beside the `boardRequest` connect (`src/RelayWindow.h` ~5242): in the active window, create a shell pane with `createPane` and `insertBeside` exactly as `pane.splitRight` does; with no active window use the first window. The publish path then puts it in the phone's inbox unaided.
5. Phone: a New pane button in `#inbox-top` (`app/index.html:150`, styled in `app/style.css`), wired in `app/app.js`: shown only when `capability === 'full'` (known from `welcome`, line 1987); on tap, send `pane_new`, remember the current pane-id set, and when the next `panes` event carries an id not in that set, open it via the existing `pendingOpen` path (line 2012). Keep the button disabled while a create is outstanding; re-enable on the next `panes` event or after ~5 s so an old desktop that ignores the message cannot wedge the UI.
6. Docs: a `pane_new` row in the device→hub message table in `docs/REMOTE-PROTOCOL.md` (section 6).

**Orchestration.** None: one seam, ~6 files, a single agent.

**Risks.**
- *Placement is a product choice*: the plan creates the pane in the active window, split right of the active pane (`pane.splitRight`'s behaviour). If remote panes should instead open a window of their own, say so on this card before Run.
- *Version skew*: a desktop older than this change ignores `pane_new`; the phone's timeout (step 5) is the whole mitigation, and it is enough.

**Verify.**
- `tests/test_remote_wire.py`: `pane_new` is `FULL` and in `GUEST_NEVER` (pattern line 111).
- `tests/test_remote_gui_host.py` + `tests/test_remote_security.py`: a `full` device's `pane_new` reaches the GUI line; a `view`/guest device's is refused at the gate (pattern `tests/test_remote_security.py:524`).
- `tests/remotepane_test.cpp`: a `pane_new` sidecar line emits the signal (pattern line 401).
- Phone UI: extend the inbox browser test (`tests/test_remote_browser.py`) — button visible for `full`, hidden for lesser capabilities, tap sends `pane_new` and a newly-listed pane is opened.
- Manual: pair a phone, tap New pane, watch the pane appear on the desktop and the phone land in it.
