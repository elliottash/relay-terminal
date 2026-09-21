# #SQ3D — the `/command` in an echoed prompt

The composer tints a leading `/deliver` with the theme's `[syntax] token` colour while it is being
typed. The moment the turn started, the pane printed the prompt back in one flat ink. This is that
row, after the change, in a Relay whose profile, keyring and X display are its own.

    docs/qa_evidence/2026-09-21-slash-command-ink/drive.sh [build-dir]     # the screenshots
    python3 docs/qa_evidence/2026-09-21-slash-command-ink/analyse.py      # the numbers, exit 0 = pass

`drive.sh` runs the build under Xvfb with an isolated `HOME`, `XDG_*` and `TMPDIR`, no provider
account (`stub-provider.py` on loopback answers as an OpenAI-compatible endpoint, and the tier
lists in the profile point at it), and `RELAY_KEYRING=off`. It sends one ordinary prompt — the
pane configures its agent on the first agent submission, and the skill list that makes `/deliver`
a command rather than an unknown one arrives with it — then the prompt this card is about, and
photographs the result. Then it types `/light` and photographs the same rows again: nothing is
retyped, the view repaints what is already in the scrollback.

| file | what it shows |
|---|---|
| `composer.png` | the prompt in the box: `/deliver` in the composer's token colour |
| `warmup.png` | the warm-up turn: a prompt with no command is one flat ink, unchanged by this card |
| `dark.png`, `dark-row.png` | Relay Dark, whose agent band is light: the command a dark teal, the rest of the row the role's near-black |
| `light.png`, `light-row.png` | IBM Beige, whose agent band is dark: the same rows, the command a pale teal, the rest white |
| `notes.txt` | what `analyse.py` read out of the two shots |

## What analyse.py checks

Not "it looks different" but the rule the code implements: the pane writes the command span as a
palette *index* (SGR 96), and the view paints it in `legibleOn(<the theme's bright cyan>, <the
row's band>)` — the written hue, moved only as far as the band demands
(`engine/view/FaintInk.h`). The script recomputes that colour from the theme files' own numbers
and looks for it in the command's pixels.

    --- dark.png (relay-dark)
      band          #b48ef7
      written       #78ddea   (palette[14], SGR 96)  1.63:1 on the band
      legibleOn()   #213d41   4.51:1 on the band
      painted       21 px of it in the command, 0 in the rest of the row
      role ink      #110527   7.59:1 on the band, 108 px
    --- light.png (ibm-beige)
      band          #7500c3
      written       #0a4a46   (palette[14], SGR 96)  1.22:1 on the band
      legibleOn()   #aec4c2   4.51:1 on the band
      painted       35 px of it in the command, 0 in the rest of the row
      role ink      #f4e8fc   6.99:1 on the band, 126 px

Both themes: the command is painted in exactly the ink the rule predicts, that ink clears the
4.5:1 floor on the band it is drawn on, the raw palette colour (1.63:1 and 1.22:1 — unreadable
either way) is nowhere on the row, and every other word is still the role's ink. The two inks
differ because the band does: nothing in the scrollback was rewritten between the two shots.

## The unit tests behind it

    ctest --test-dir build -R engine          # or: RELAY_ENGINE_TEST=FaintInkTest ./build/engine/relay-engine-tests

* `FaintInkTest::aWrittenInkIsMovedOntoTheFloorOfItsBand` — `legibleOn()` on each shipped theme's
  agent band, and on grounds where it must do nothing.
* `ViewTest::aCommandInAUserRowKeepsItsHueAndClearsItsBand` — the two paint paths that draw such a
  row: the grid's own row and the prose block the layer re-wraps after a resize (#R2WQ), in a
  light-band and then a dark-band scheme. Both assertions were checked to fail with either
  branch disabled.
