<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Fixing the eight phone cards — the workstream, 2026-09-22

The eight cards from the review and drive in this folder are being fixed as one workstream. It is
organised **by the files a fix has to touch, not by card**, because several sessions share this
checkout and two fixes in one file collide however tidy the cards are. Two of the cards (#EFT9 and
#PKT5) are worked by two streams each for exactly that reason: neither fits inside one file.

## Who owns what, while this is live

| Stream | Owns these files, and nothing else | Cards |
|---|---|---|
| **A** | `app/pane.js`, `app/pane.css`, `tests/test_pane_view.py` | #EFT9 (the chip), #KBD7, #CPY4, #PKT5 item 4 |
| **B** | `app/screen.js`, `tests/test_web_screen.py` | #NK73 |
| **C** | `app/board.js`, `app/boardmd.js`, `app/board.css`, `tests/test_board_view.py` | #MDX6, #RCN8 |
| **D** | `app/app.js`, `app/index.html`, `app/style.css`, `tests/test_remote_browser.py` | #TBR2, #PKT5 items 1–3 |
| **E** | `src/Pane.h`, `src/PaneState.{h,cpp}`, `src/RemoteShare.{h,cpp}`, `remote/pane_state.py`, `remote/host.py`, `docs/REMOTE-PROTOCOL.md`, `tests/test_remote_pane_state.py` | #EFT9 (the wire) |

Each stream lands its own commits through `scripts/land.py`, small and as they pass, and writes its
evidence under `docs/qa_evidence/2026-09-22-stream<X>-<slug>/`. **No stream edits `issues/`**: the
orchestrating session holds every card and thread file and makes the board writes, so four
implementers cannot collide there either. Every card carries its `## Done means` (written before
any code) and lands in `needs-verification` for a separate session to check.

**E is held until a slot frees.** It is the only C++ in the set, so `land.py`'s build gate
materialises the tree it is about to put on `main` and builds it — minutes, and one build at a
time across the whole machine (`scripts/relay-build`). Running it beside four JS streams would make
each of them wait on the lock. A also has a dependency on E: a model whose reasoning level is fixed
should draw a chip that cannot be changed, and the wire does not carry that fact yet. A writes
`m.effort_fixed === true` → disabled, and E adds the field; until E lands, that one line is
untested.

## The one decision that was made rather than asked

**#KBD7: the blur is touch-only.** The commit that added it blurred unconditionally, reasoning that
"a physical keyboard's blur is invisible". It is not: `enter()` is reachable only from the prompt's
own `keydown`, so a blur takes Enter's three-step steer escalation, `queue_resume` on an empty box
and ArrowUp row selection with it. Blurring only where there is an on-screen keyboard to dismiss
keeps both — the phone gets its screen back, the laptop keeps its keys. The view already knows
which it is drawing for.

## The order of the end

When the streams have landed: one full `scripts/relay-build`, then one live drive of the whole
phone app — `pane_drive.py` and `app_drive.py` in this folder, re-run against the fixed tree — and
a verifier writes each card's `## QA checklist` against its `## Done means`.
