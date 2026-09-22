<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The pane view after #EFT9, #KBD7, #CPY4 and #PKT5 — 2026-09-22

The four faults `docs/qa_evidence/2026-09-22-phone-ux-drive/` measured in `app/pane.js`, measured
again on the same bench after the fix: `app/pane-demo.html` over `tests/fixtures/pane_state/` in a
real headless Chrome at 390×844, DPR 2, touch emulation on. `drive.py` is the drive, `drive.log`
is what it printed, and the `.png`s are what it saw. Nothing here is a test; the tests are the
thirty-six cases in `tests/test_pane_view.py`.

```sh
python3 docs/qa_evidence/2026-09-22-streamA-pane/drive.py
python3 -m unittest tests.test_pane_view          # 36 tests, all pass
```

Commits: `44a46ca1` (#EFT9), `d6116335` (#KBD7), `782808f9` (#CPY4), `dc2c00ab` (#PKT5 item 4).

## #EFT9 — the reasoning-level chip

`EFT9-chip.png`, `EFT9-chip-after-level-only-state.png`, `EFT9-chip-fixed.png`.

| | before (2026-09-22 drive) | now |
|---|---|---|
| closed chip | `{"value":"high","shown":"✓ high"}` | `{"value":"","shown":"high"}` |
| after `effort=low`, same model | `{"value":"high","shown":"✓ high"}` — ignored | `{"shown":"low","opts":["low","✓ low","medium","high"]}` |
| chevron vs the level | chevron 216.4–223.6 **over** the level at 198–289 | model 17–154.5 with its chevron 140.2–147.5; level 158.5–230.6 with its own at 216.4–223.6 |
| `effort_fixed: true` | no such thing | `{"disabled":true,"chevron":"none"}` |

Each chevron is now inside its own control's box and inside that control's right edge. The closed
chip carries a disabled placeholder with the plain level, so the `✓` only appears in the open list.

## #KBD7 — Send, and what the blur took with it

`KBD7-send-touch.png`, `KBD7-send-mouse.png`.

```
KBD7 [touch] after Send: focus=demo-bare  box=""      <- the body: the keyboard comes down
KBD7 [mouse] after Send: focus=rp-input   box=""      <- the box keeps it, so Enter still escalates
```

The mouse half is the half the card warns about: `enter()` is reachable only from the box's own
keydown listener, so an unconditional blur would take the three-step Enter escalation,
`queue_resume` on an empty box and ArrowUp row selection with it.
`tests/test_pane_view.py` drives the second Enter after the send and asserts the steer.

## #CPY4 — Copy id, and the list's scroll

`CPY4-toast-over-the-sheet.png`, `CPY4-refused-id-over-an-open-sheet.png`,
`CPY4-list-holds-its-scroll.png`.

```
CPY4 asked on the press: {"t": "conversation_id", "session": "s1", "id": "…"}
CPY4 clipboard before the tap: ""
CPY4 clipboard written inside the tap: "9f2c7a1e-4b3d-4e5f-8a90-1b2c3d4e5f60"
CPY4 topmost at the toast's centre, sheet closed by the tap: {"topmost":"the toast (rp-toast)"}
CPY4 refused write, sheet open: {"topmost":"the toast (rp-session-id)","sheetOpen":true}
CPY4 the refusal carries the id: "Clipboard refused — long-press to copy: 9f2c7a1e-…"  sheet lines: 0
CPY4 a minute tick: scrollTop 1975 -> 1975, focus unchanged, row node kept: true,
                    when now "one minute later"
```

The review measured `rp-session-when` — a row of the sheet — as the topmost element at the toast's
centre, and `scrollTop` 2010 → 0 on one `when` tick. Both are the measurements above now. The
z-index is what does it: with `.rp-toast`'s `z-index` forced back to `auto` on the same page the
answer is `rp-session-open` again.

The refusal path is the iOS one, and the id is in the toast rather than appended to whatever sheet
was in the DOM — `sheet lines: 0`.

## #PKT5 item 4 — a finger on an ask option

`PKT5-ask-across-states.png`.

```
PKT5 the ask button survived ten states: {"sameNode":true,"marked":true}
PKT5 the tap still fires: [{"t": "compose", "text": "main", "when": "queue", …}]
```

Ten `pane_state`s land between the mark and the tap; the option is the same node afterwards and
the tap sends the answer. The queue rows have the same guard, and `scrollIntoView` now runs on a
move of the selection rather than on every rebuild.

## Provenance

`main` at `dc2c00ab`, Ubuntu 24.04 aarch64, Chrome at `/usr/bin/google-chrome`, viewport 390×844.
The drive needs no desktop and touches nothing of the owner's profile.
