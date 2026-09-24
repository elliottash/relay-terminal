---
id: RSJY
type: work
status: planned
labels: [bug, remote]
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: 'owner, 2026-09-21, #PH0N Phase 3 (first iPhone pairing)'
links: {plans: [], commits: [], evidence: [], related: [PH0N], github: null}
---
# A paired device's grant cannot be raised short of revoke-and-re-pair

## Issue
ok im connected, but i coudlnt do anything

## Discussion points
Found on #PH0N's first iPhone pairing (2026-09-21): the owner paired, connected, and could not act — the phone was granted `view` (the desktop dialog's "Allow viewing" button; `RemoteShareDialog::answer` in `src/RemoteShare.cpp`). A `view` device gets no prompt box, Stop or Recap (`app/app.js:805`), and the phone UI gives no hint *why* it is read-only or what to do about it.

Two gaps:

1. **No upgrade path.** `DeviceStore.set_capability` exists (`remote/identity.py:306`, used only by tests); nothing in the GUI or the wire protocol sends it. The devices list in the share dialog shows each device's grant but offers only Revoke. Raising view → full means revoke the device and pair again.
2. **The phone doesn't say so.** A view-only device just has no composer; nothing on the phone reads "you're paired for viewing — re-pair with Allow typing to steer".

A fix could be: a per-device grant picker in the share dialog's device list (new sidecar line, capability read live already so it takes effect on the next message), plus a read-only banner in the app naming the grant.

## Done means
From the desktop sharing dialog the owner can change a paired device's grant (view / agent / full) without revoking it, and the change takes effect on that device's next message — a phone raised from `view` to `full` gets its composer, Stop and Recap without re-pairing. A view-only phone also says on screen that it is paired for viewing and what to ask the owner for, instead of just showing no composer. Failure looks like: the dialog still offers only Revoke (raising a grant means revoke-and-re-pair), a connected phone's UI does not unlock after its grant is raised, or a view-only phone shows no explanation of why it is read-only.

## Plan
**Goal.** Give the owner a per-device grant picker in the sharing dialog (a new GUI↔sidecar message that calls the already-existing `DeviceStore.set_capability`), push the new grant to a live phone so its UI unlocks without re-pairing, and make a view-only phone say why it is read-only.

**Findings.**
- `DeviceStore.set_capability` (`remote/identity.py:306`) is written, validates against `VALID_CAPABILITIES`, and calls `_notify(device_id)` — used only by tests. Enforcement reads capability live at every point (`docs/REMOTE-PROTOCOL.md` section 7), so a change needs no reconnect to take effect.
- The GUI↔sidecar stdio protocol (`remote/gui_host.py`) has no grant message: `_handle` (line 639) knows `devices`, `revoke` (708), `password`, `share`, etc. The `password` handler is the exact template: validate, mutate the store, `_push_devices()`.
- `DeviceStore._notify` feeds `Host._device_changed` (`remote/host.py:1630`, registered as `on_revoke` at line 599). That hook revokes connect tokens when the device is revoked; for a pure capability change it does nothing visible, so a **connected** phone is never re-sent its grant. The client learns its capability only from `welcome`/`admitted` (`app/app.js:1939-1945` updates `capability` and re-renders on the `welcome` event).
- The dialog's device row is built in `RemoteShareDialog::showDevices` (`src/RemoteShare.cpp:1994`): text only, stores id (`Qt::UserRole`) and password_entry (`Qt::UserRole+1`). Buttons row (~lines 1380-1402) has Revoke and a Passwords toggle; `RemoteShare::setPasswordEntry` is the template for a new `setCapability` sender.
- The phone already writes `"This device is paired for viewing only."` in `updateDriveUi` (`app/app.js:1201`), but only as the per-pane `termNote` when the agent row is hidden — it does not say what to do about it, and nothing names the grant as the reason.

**Steps.**
1. **Sidecar (`remote/gui_host.py`).** Add a `"grant"` branch to `_handle`, mirroring `"password"`: validate `msg["capability"]` against `VALID_CAPABILITIES` (import from `identity.py`), call `self.host.devices.set_capability(msg["device"], cap)`, catch `ValueError`/unknown device into `{"t":"error","op":"grant","message":...}`, then `_push_devices()`. Add `{"t":"grant","device":"...","capability":"agent"}` to the protocol docstring at line 36.
2. **Live push (`remote/host.py`).** In `_device_changed`, when the device exists and is *not* revoked, re-send `welcome` (the existing `_welcome` payload already carries `capability`) to that device's live channels, so the phone's existing `welcome` listener unlocks its UI in place. If channels are not indexed by device, add the minimal lookup; do not touch the guest/participant paths.
3. **Dialog (`src/RemoteShare.cpp`).** Store each device's capability on its item (`Qt::UserRole+2`) in `showDevices`, and add a grant control next to Revoke/Passwords — a small combo or cycling button offering view / agent / full for the selected device — wired to a new `RemoteShare::setCapability(device, capability)` that sends the `grant` line, mirroring `setPasswordEntry`. The sidecar's `devices` push refreshes the row text, confirming the change.
4. **Phone copy (`app/app.js`).** In `updateDriveUi`, replace/extend the bare `"This device is paired for viewing only."` note so it names the grant and the remedy, e.g. `"Paired for viewing only — ask the owner to raise this device's grant in the desktop sharing dialog."` Keep it hidden once `canCompose` is true.

**Risks.**
- `grant` must stay a GUI-sidecar message only: never add it to `CLIENT_TYPES` in `remote/wire.py` — a phone that could raise its own grant is a second key to the share (the `OWNER_ONLY` comment, wire.py:107). A test should assert it stays refused from clients.
- `_device_changed` is registered under the name `on_revoke`; make sure the re-welcome fires only for capability changes on live, non-revoked devices, and that revoke behaviour (connect-token revocation) is unchanged.
- `RemoteShare.cpp` is worked by several sessions; keep the dialog edit small and local to `showDevices` + the buttons row.

**Verify.**
- New pytest in `tests/remote/test_gui_host.py`: stub-GUI `grant` with each valid capability updates the store and is followed by a `devices` push; an invalid capability gets an `error` and leaves the store unchanged. Assert `"grant"` is not in `CLIENT_TYPES` (add to the existing wire allow-list test if one exists).
- Host-level test (extend `tests/remote/test_host_remote.py` or the file covering `_device_changed`): a capability change on a live device re-sends `welcome` with the new capability; a revoke still revokes connect tokens.
- `scripts/relay-build` after the C++ edit.
- Manual: pair a phone with Allow viewing, raise it to full in the dialog, and watch the composer appear on the phone without re-pairing; confirm a view-only phone shows the new explanation.
