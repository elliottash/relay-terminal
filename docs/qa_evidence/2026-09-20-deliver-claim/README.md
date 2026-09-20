# #R9G7 — a pane claims a card, and the Switchboard shows who holds it

Implementer evidence for the GUI half of the deliver workflow: Execute hands the card to a terminal
pane with one `board_claim`, the card's front matter records that pane's session token, and the
Switchboard draws it as a chip on the row and on the card page — muted, and saying `closed`, once
that pane has gone.

`drive.sh` does the whole thing live: Xvfb, an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR` under a short path, `RELAY_KEYRING=off`, a fixture board of one card
with a `## Plan` (so Execute goes ahead without arming). No model is needed — `board_claim` is a
write, not a turn — and the pane Execute opens is real whether or not its agent gets an answer.

    docs/qa_evidence/2026-09-20-deliver-claim/drive.sh [build-dir] [out-dir]

The run below is against a `relay` built from the landed tree at `ca6ebaad`, not the shared
checkout's `build/`, which holds other sessions' uncommitted work.

## Commits

| sha | what |
| --- | --- |
| `a5e0dfbb` | `configure` carries the pane's own session token (`pane_token`) — GUI |
| `a7746e2d` | the board model: a card carries `session`, and `sessionChip()` is the one spelling of it |
| `d52916e4` | Execute sends `board_claim`; the chip on the row and the card page; `paneExists` wiring |
| `ca6ebaad` | the chip is measured at full width, so a row never elides it to half a token |
| `6a8575f4` | protocol 19.19: `pane_token` on configure, `board_claim`, the `session` field — worker |
| `be5d9ec1`, `a1ec6290`, `baae6a63` | the worker's `board_claim`, `session` on board rows, the policy test |

## What the run checked (`ocr.txt`, all machine-checked)

Nothing here is read by eye: each claim is checked against the card file the worker wrote, or by
tesseract word boxes. The row's badges are drawn at 0.85 of the pane's font, which tesseract misses
at page scale, so the row's own strip is cropped out of the board pane and read at 4×.

1. **`x` on the card claims it** (`04-claimed.png`). The card's front matter gains
   `session: <token>`, `status: executing` and `assignee: agent` from one write, and the thread's
   progress entry reads `Claimed (<first 8>) · working on it from a terminal pane`.
2. **The card page wears the chip** beside `assignee`: `session ⧉ <first 8>`, in the link colour,
   as the `relay-pane:` anchor the thread already uses — one click reveals the pane.
3. **The row wears the same chip** (`05-row-chip.png`), whole:
   `#QS6C Alpha voice mode · ✦ agent · ⧉ ed536111 · 2026-09-20 · 2026-09-20`.
4. **Close that pane and the chip says so** (`06-pane-closed.png`, `07-card-page-closed.png`):
   `⧉ ed536111 closed`, muted, on the row at the board's next redraw and on the card page when it
   is read again. The token stays — the claim is the record of who took the card — and the card
   page's chip is no longer a link.
5. Relay is alive at the end and the run's `relay.log` has no `gui_crash` (the log is copied
   in beside these shots only when it has anything in it; this run left it empty).

## Shots

| file | what |
| --- | --- |
| `00-first-run.png` | the window as it opens; the Approvals pane is closed to give the board room |
| `01-board-open.png`, `02-ready-unfolded.png`, `03-card-open.png` | to the card |
| `04-claimed.png` | after `x`: the claim, the chip on the card page, the pane it opened |
| `04b-list-folded.png`, `05-row-chip.png` | back to the list, EXECUTING unfolded: the live chip |
| `06-pane-closed.png` | the claimed pane closed: `⧉ … closed` on the row |
| `07-card-page-closed.png` | and on the card page, muted and unlinked |
| `claimed-card.md` | the card file the worker wrote, front matter and all |
| `ocr.txt` | every check the run made, including that `relay.log` held no `gui_crash` |

In `07` the row's chip is absent: the card page takes half the pane, and a narrow row drops its
badges (`board::fitBadges`) — a closed claim goes early, with the labels, because it is history.
The row's tooltip still names the pane either way.

## QA checklist

- [ ] Execute a card from a Switchboard: the card's front matter gets `session`, and only one
      `board_claim` goes out (no `board_update`/`board_move`/`board_comment` trio).
- [ ] The row and the card page show the same eight characters as the thread's `Claimed (…)` entry.
- [ ] Clicking the chip on the card page reveals the pane that claimed the card.
- [ ] Close that pane: both chips say `closed`, the card page's stops linking, and the row's
      tooltip says "Claimed by the pane ⧉ …, which has closed".
- [ ] Narrow the board pane: the chip is dropped whole, never elided to part of a token.
