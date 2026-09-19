# The usage meter in words — implementer evidence (#6BGA)

The pane's resource meter now says **`cpu 12% · mem 3%`** in plain words, and the pane chip, the
tab label, the Sessions row and the tooltips all print that one string. The 13 px processor die
and the memory module are gone. Owner, 2026-09-19, on the mock-up round in
`docs/qa_evidence/2026-09-19-usage-meter-numbers/`: *"the cpu / mem bar things are ugly and
unintuitive. i think it should be numbers"* — variant **a**.

`drive.sh` is the capture, adapted from that round's `mockup-harness/live.sh`, which is the same
recipe against the chip as it was before. Xvfb, an isolated `HOME`, `XDG_CONFIG_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR`, all under a short path (unix sockets have a 108-byte limit), and
`RELAY_KEYRING=off`. **Nothing is typed into the prompt box**: the load is started by the
sandbox's own `~/.bashrc`, which the pane's shell sources, so the busy processes are children of
the pane's shell and land in the meter's tree. Four `yes` loops and a 4 GB `bytearray` in python;
a second stage waits on a file the script touches and adds thirty more `yes` for the inks.

```
docs/qa_evidence/2026-09-19-usage-meter-words/drive.sh          # THEME=relay-dark by default
SWEEP=1 docs/qa_evidence/2026-09-19-usage-meter-words/drive.sh  # one crop per width: the ladder sheet
```

## The shots

| file | what it shows |
|---|---|
| `implementer-header.png` | the whole window, 1440×900, dark |
| `implementer-header-3x.png` | the pane header at 3×: `cpu 20% · mem 3%` then the title. Plain words in the body face, the header's muted ink, **no glyph of any kind** |
| `implementer-tab.png` | the tab label at 3×: `project  ·  cpu 20% · mem 3%` — the chip's string, not the old `· 20% / 3%` |
| `implementer-tooltip.png` | the chip's tooltip at 2×: first line `cpu 19% · mem 3% (4.0 GiB)`, then `yes · cpu 5% · mem 0%` and `python3 · cpu 0% · mem 3%`. The breakdown reads the same way round as the line above it |
| `implementer-narrow.png` | the window at 420 px: the header ladder's last rung, `cpu 20%` alone — the same grammar shortened, never a bare number |
| `implementer-ladder.png` | the ladder itself, 700 → 380 px: the directory elides and goes, then the memory half and its separator go together |
| `implementer-high.png` | thirty more `yes` loops: `cpu 74%` in the amber warning ink while the word "cpu" and the whole memory half stay muted — only the number that crossed 60 % colours |
| `implementer-high-tab.png` | the same reading on the tab, `project  ·  cpu 74% · mem 3%` |
| `implementer-idle.png` | the load stopped: within a poll the chip and the tab suffix are gone and the header is exactly as it was before the feature |

## What was checked, and what it proves

- **One string in three places.** The header crop, the tab crop and the tooltip's first line all
  read `cpu N% · mem M%`. The Sessions row prints `relay::usage::liveTag`, which is now literally
  `readingText()` — `tests/conversations_test.cpp` already pins that string and still passes.
- **No glyphs.** `paintDie`, `paintModule` and `kGlyph` are deleted; `implementer-header-3x.png`
  has nothing but text between the state glyph and the title.
- **The halves and the narrow rung.** `implementer-narrow.png` and `implementer-ladder.png`:
  rung 5 drops the memory half and its separator together and leaves `cpu 20%`.
- **The inks at the thresholds.** `implementer-high.png`: the number is amber past 60 %, the word
  beside it is not. An earlier pass of the same scene caught 87 % and rendered it in the error red.
- **Quiet panes.** `implementer-idle.png`: nothing at all.

## Tests

`./build/relay-paneusage-tests` — 21 cases, all passing, including the three written for this
card: `oneWordingInEveryPlace`, `theNarrowRungKeepsTheGrammar`,
`thePaintedPiecesSpellTheSameString`.

`ctest --test-dir build` — 61 of 63. The two failures are other sessions' uncommitted work in the
shared checkout, not this change and not on `main`:

- `buttonfit` — `stylesheetFontsStayAtOrAboveTheFloor`: `dark-copper: "font-size: 8.5pt" is under
  the 9pt floor`, from an uncommitted edit to `src/Theme.cpp`. `git show HEAD:src/Theme.cpp` has
  no `8.5pt`.
- `backend-and-bash` — 3 errors of 3408, all
  `AttributeError: 'types.SimpleNamespace' object has no attribute 'executor'` at
  `backend/relay_core/subagents.py:214`, from an uncommitted edit to that file. This card touches
  no Python.
