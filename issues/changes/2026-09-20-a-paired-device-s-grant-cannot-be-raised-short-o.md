---
id: RSJY
type: work
status: inbox
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
