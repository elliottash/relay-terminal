# #93WR live: the cards the agent closed itself are one row of the done list

2026-09-20, under Xvfb at 1440×1180 with an isolated `HOME`, `XDG_CONFIG_HOME`, `XDG_RUNTIME_DIR`
and `TMPDIR` (a short path — `XDG_RUNTIME_DIR` holds sockets and the limit is 108 bytes) and
`RELAY_KEYRING=off`. `drive.sh` in this folder is the whole run; `ocr.txt` is what it checked,
every line by tesseract rather than by eye, and `fixture-cards.txt` is the board it built.

**The binary is the landed tree's**, not the shared `build/`: `scripts/land.py`'s own verify tree
for `a8fe410c` (`/tmp/claude-1000/land/93wr-gui/verify/build/relay`, built 15:19), which is
`main` at `dedad97c` plus the layout-node hunks. The shared `build/` holds several sessions'
uncommitted code, so it cannot say what a commit does.

Commits: `d5128e04` (the model: `board::selfClosed`, `Row::Fold`, Done rather than Verified),
`dedad97c` (the view: the row, its keys, the reveal), `a8fe410c` (`self_closed` in the layout
node). Design: `docs/SWITCHBOARD-DESIGN.md` 4.11.1.

## The fixture

Seven cards, written straight to disk with the stamps the worker will write (the backend half of
#93WR is a separate commit; nothing here depends on it):

| card | status | `implemented_by` | `verified_by` | where it lands |
| --- | --- | --- | --- | --- |
| Alpha voice mode | ready | – | – | READY TO START |
| Bravo release notes | done | claude-opus-5 | – | DONE, an ordinary row |
| Charlie old glyph | dropped | – | – | DONE, an ordinary row |
| Golf checked by codex | done | claude-opus-5 | openai/codex | VERIFIED |
| Delta tooltip wording | done | claude-opus-5 | claude-opus-5 | DONE, folded |
| Echo log rotation | done | claude-opus-5 | claude-opus-5 | DONE, folded |
| Foxtrot cache header | done | claude-opus-5 | claude-opus-5 | DONE, folded |

## What the shots show

| shot | what |
| --- | --- |
| `00-first-run.png` | the first run, before the Approvals pane is closed |
| `01-board-open.png` | the Switchboard as a new pane opens it: every section folded |
| `02-done-unfolded.png` | DONE unfolded — Bravo, Charlie and `▸ 3 closed by the agent`. Delta, Echo and Foxtrot are nowhere on the page, Golf is in VERIFIED, and the header still reads `DONE 5` |
| `03-agent-cards-shown.png` | a click on the row: `▾ 3 closed by the agent` with its three cards under it as ordinary rows, no `✓` badge on any of them, header still `DONE 5` — and the shortcut hint *"Next time: Enter"* the click earns |
| `04-folded-again.png` | a second click puts them away; the row stays |
| `05-card-open.png` | an ordinary closed card opened, to give the list the keyboard |
| `06-keyboard-open.png` | Esc back to the list, End onto the fold row, Enter: its cards are showing |
| `07-keyboard-folded.png` | ← puts them away again, with the row still standing |

Every assertion in `ocr.txt` reads `yes`, and `relay.log` holds no `gui_crash`.

## Not covered here

- The layout round-trip (`self_closed` saved and restored) is a unit test —
  `BoardModelTests::theFoldRowTogglesOnClickEnterAndTheArrowsAndRidesTheLayout` — rather than a
  screenshot: it needs a quit and a restore, which this driver does not do.
- The tooltip's words are asserted in the same test; a tooltip does not survive `import -window`.
