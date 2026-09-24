---
id: SHRP
type: work
status: needs-verification
labels: [feature, remote]
component: [gui]
milestone: beta
workstream: remote
assignee: claude-code
rank: 6b
created: '2026-09-21'
source: 'owner, 2026-09-21, Claude Code session, with a screenshot of the Sharing pane listing three panes with identical blocks'
links: {plans: [], commits: [2b102129, 964f2f5f], evidence: [docs/qa_evidence/2026-09-21-sharing-pane-redesign/], related: [PH0N, SWPH, FR1C, W5N2, SHCK], github: null}
---
# The Sharing pane says what varies: your phones once at the top, guests where there are any, one row per quiet pane

## Issue

can you look at the sharing pane that opens from the share button? its repetitive and not intuitive

## Planning notes

Since #PH0N every pane with a screen is published to the owner's own phones, so `sharedPanes()`
lists every pane, and `SharingView::build()` (`src/SharingPane.cpp` ~833) draws the full guest kit
for each: "Pane …", "Nobody here yet", a note saying the owner's own phones are not listed here,
"This share", three buttons, two checkboxes with a sentence each. With three panes the screen is
the same block three times and nothing on it says what the pane is for. The note points at "the
share window" for the phones, which is the wrong way round: the phones are the common case now.

## Plan

**Goal.** Open the Sharing pane and read, in this order: whether remote control is on and which
of your phones are connected; anything waiting for you; who is visiting which pane, with the
controls for that pane only where there is someone or a live invite; and one compact row per pane
nobody is visiting.

**Layout.**

1. **Top line**: "Remote control on · relay-terminal.ai · iPhone, iPad connected" (or "· no phone
   connected", or "Remote control off"), with a **Pair a phone…** button; the devices come from
   `RemoteShare::devices()` and `remoteState()`, fed into the sharing model by the window.
2. **Waiting for you** (only when non-empty): the request rows as today, each naming its pane.
3. **Guests** (only panes that have a participant or a live invite): a block per such pane —
   participants, invite links, the three buttons, and the two options as **one compact row** of
   two checkboxes with the explanations as tooltips and a single shared note under the section,
   not repeated per pane.
4. **Nobody is visiting**: one sentence ("Every pane is reachable from your phones. To let someone
   else in, invite them.") and one row per quiet pane: its title and an **Invite…** link. No
   headings per pane, no options, no buttons.
5. The old note ("Your own paired phones are not guests…") goes.

**Files.** `src/SharingPane.{h,cpp}` (model: `setDevices`, `setRemote`; view: the new build),
one hook block in `src/RelayWindow.h` (feed devices and `remote_state` to the model, small),
`tests/sharingpane_test.cpp`, screenshots under Xvfb in `docs/qa_evidence/2026-09-21-sharing-pane-redesign/`.

## Tasks

- [x] Model carries the devices and the remote state <!-- t:r1 -->
- [x] The view: top line, waiting, guests, quiet panes <!-- t:r2 -->
- [x] Tests and screenshots (three quiet panes; one pane with a guest and an invite; a knock waiting) <!-- t:r3 -->

## Execution Summary

`964f2f5f`: the pane reads, top to bottom, "Remote control on · relay-terminal.ai · iPhone, iPad
connected" with Pair a phone…; Waiting for you (only when there is something, each row naming its
pane); Guests, a block only for a pane with a participant or a live invite, its two options on one
row with the sentences as tooltips and one note under the section; Nobody is visiting, one sentence
and one row per quiet pane with Invite…. When remote control is off the sentence says so. The old
per-pane headings and the "your own paired phones are not guests" note are gone. `2b102129`: the
sidecar's `devices` line carries `online` per device, so the top line can name the phones. The
#SHCK teardown is unchanged. Screenshots of the three states, from the real pane under Xvfb, in
the evidence folder.

## Tests

- `ctest --test-dir build -R '^sharing$'` (23 cases, four new)
- `RELAY_KEYRING=off python3 -m unittest tests.test_remote_gui_host`
- `manual: docs/qa_evidence/2026-09-21-sharing-pane-redesign/` (`quiet.png`, `guests.png`, `knock.png`)

## QA checklist

- [ ] Open the Sharing pane with no guests: one status line naming your connected phones, one sentence, one row per pane with Invite…, nothing else.
- [ ] Invite someone to one pane and have them knock: "Waiting for you" appears first and names the pane; after admitting, that pane alone gets a Guests block with the two options on one row.
- [ ] Toggle both options: no crash, the model follows, the tooltips carry the explanations.
- [ ] Turn remote control off from the plug menu: the top line says off and the sentence changes.

