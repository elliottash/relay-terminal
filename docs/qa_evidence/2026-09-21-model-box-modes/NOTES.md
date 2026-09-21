# The model box: modes, then the models of the mode you are in (#MDL1, t:a6)

`drive.sh` in this folder, run under Xvfb on `:460` against `build/relay` built from the working
tree at commit `3cac1ecd` plus the two follow-up fixes below. Isolated `HOME` / `XDG_*` / `TMPDIR`
under `/tmp/claude-1000/bx` (short, for the 108-byte unix-socket limit), `RELAY_KEYRING=off`,
`isolation/enabled=false`, and three fake provider keys — every step is a model or mode *switch*,
which the worker answers without calling anybody.

The code is in the binary it was driven with:

```
$ strings -el build/relay | grep -E 'agent.highAgent|Run this pane on the high model'
agent.highAgent
Run this pane on the high model (same as Alt+H)
```

The lists were seeded so each thing the card asks for is visible:

| list | entries |
|---|---|
| main | `kimi-code\|k3`, `glm-coding\|glm-5.3`, `glm\|glm-5.3` — the last two are **one model from two providers** |
| high | `glm-coding\|glm-5.3`, `openai\|gpt-5.6-sol` — no OpenAI key here |
| flash | `glm-coding\|glm-5.3-flash`, `kimi-code\|k3` |

## What each shot shows

| shot | what it proves |
|---|---|
| `a-strip` | the collapsed chip on main is the model alone: **kimi-k3**, no "(main)" and no "(switchboard)" |
| `b-altm-main` | Alt+M: `high (glm-5.3)`, `• main (kimi-k3)`, `flash (glm-5.3-flash)`, a separator, then the main list in **list order** — `kimi-k3  kimi` (highlighted: this pane's model) and `glm-5.3  z.ai (glm) +1` (**one row per model**, two providers, the "+1") |
| `c-right-flash` | **Right**: the flash list is drawn in place (`glm-5.3-flash`, `kimi-k3`), the popup is still open, the marker has **not** moved — nothing is sent to the worker until Enter |
| `d-left-left-high` | **Left Left**: the high list, with `gpt-5.6-sol  openai (chatgpt)` **greyed in place** rather than dropped, because no OpenAI key is stored here |
| `e1-filtered-main` | "glm" typed on the main page: the two mode rows that match, and `glm-5.3  z.ai (glm) +1` |
| `e2-filtered-then-right` | then **Right**: the filter is **kept** and re-applied to the flash page, and the highlight lands on that page's own current row (`glm-5.3-flash`) |
| `f1-flash-highlighted` | Right, then Down: `kimi-k3` highlighted on the flash page |
| `f2-picked-flash` | **Enter** on it: the pane goes to flash *and* to that model. The collapsed chip reads **`kimi-k3 · flash`** and the toast says "flash: kimi-k3 · conversation kept" |
| `g-slash-high`, `g2-high-open` | **`/high`**: the chip follows — `glm-5.3 · high` — and the box opens on the high page with the marker on `high`. Note `flash (kimi-k3)`: the pane's own flash pick from the step before is what the parentheses now name |
| `h-slash-main` | **`/main`**: the model alone again, `kimi-k3` |
| `i-console-box` | the **Switchboard console's** box (its worker role is `switchboard`, a main-tier role): `high (glm-5.3)`, `• main (kimi-k3)`, `flash (glm-5.3-flash)`, `kimi-k3 kimi`, `glm-5.3 z.ai (glm) +1` — and its chip reads `kimi-k3`, not "kimi-k3 (switchboard)" |
| `j-pane-box` | the terminal pane's box beside it, in the same window: the same rows. The one difference is `flash (kimi-k3)`, because *that* pane picked kimi-k3 for flash in step f — which is what "what this pane would run in that mode" means |
| `k-flash-remembers-the-pick` | **`/flash`** puts the pane back on **its own** flash model, `kimi-k3 · flash`, not on rank 1 of the flash list. "Enter on a mode row switches the mode and keeps that mode's model", and the command does the same |
| `saved-mode-picks.txt` | what the layout node then holds: `{"flash": {"preset": "kimi-code", "model": "k3", "effort": "high"}}` — cut out of `saved-layout.json` |
| `l-restored-on-its-pick` | Relay quit with SIGTERM and **relaunched with no arguments** ("reopen where I left off"; `--workspace` on the command line suppresses restore). The pane comes back on `kimi-k3 · flash` |
| `l2-restored-box` | and its box agrees: the marker is on `flash (kimi-k3)`, the flash list is the page, `kimi-k3` is highlighted |

`*-box.png` is the bottom 420 px of each shot, cropped so the list is readable.

## Two things the first run found, and the fixes

1. **The box opened on the last page Escape happened to be over.** `FilterPopup::onPageChanged` was
   writing the new page back into `CurrentTextComboBox::pageId`, so after turning to high and
   pressing Escape the *next* Alt+M opened on high although the pane was still on main. Escape must
   leave everything as it was. `pageId` is now the owner's alone — the page the box **opens** on —
   and `Pane` re-reads it from `paneMode()` in `onPages`, the moment the list is about to be drawn.
   `e1`/`e2` in the first run showed the wrong pages; they are right here.
2. The Switchboard opens on the Projects picker in a fresh profile (a tab is unattached until an
   action attaches it), and **Esc there closes the whole pane** rather than the picker. The script
   clicks "Initialize here" instead, which is also a truer test: the console is then a real board's
   console.

## A command's sentence survives the switch it asked for

`drive-sentence.sh` in this folder, a second short run against the same build.

| shot | what it proves |
|---|---|
| `m1-glm-said` | half a second after `/glm`: **`model: glm-5.3 · z.ai (glm).`** — the command's own line, which says which key is being spent |
| `m2-glm-still-said` | the same line three seconds later. `model_changed` has been and gone; before `sayAndSwitch` it replaced this with the generic "model: … · conversation kept", which is the defect `e7cab7d2` fixed for `/swap` alone |
| `m3-box-after` | and the box is unchanged by the two follow-up commits (`conciseModel`'s removal and the one-shot): the modes, the marker on `main (glm-5.3)`, the list in order, `glm-5.3  z.ai (glm) +1` highlighted because that is now the pane's model |

## The marker gutter

The mode rows carry their mark in the row's **text** — `"• "` on the mode the pane is in, two spaces
on the others — because the popup, the combo and the phone's menu all draw the same string and only
one of the three can paint a gutter. In a proportional font a bullet is wider than two spaces, so
the first run's shots had the mode names about 5 px apart. The popup now takes the mark back out of
the text and draws it in a fixed-width column of its own, so `high`, `main` and `flash` start at the
same x whether or not their row is the marked one (`b-altm-main-box.png`, `l2-restored-box-box.png`).
A page with no marked row — the Alt+E level box, and every other box — gets no gutter and is drawn
exactly where it always was. `tests/filterpopup_test.cpp`
`theMarkerSitsInAGutterSoTheNamesLineUp` renders the same word three times with and without a
marker and compares the two images column by column, so the names cannot drift apart again.

## One more thing the shots found

The restored pane's **main** row named the *flash* model. `configure` set the pane's own model
(`m_paneModel`) from the `configured` event's `model`, which is the model of whatever role the pane
started on — so a pane restored on `/flash` came back with its main row naming the flash model.
`model_changed` has followed the rule since t:a3 ("a role's model never rewrites the pane's own
key"); `configure` now does too, reading `roles.main` (protocol 13.4), which is the pane's own model
by definition. It was there before this task and is visible in the shots of it, so it is fixed here.
