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

## Known cosmetic point

The mode rows use a text marker — `"• "` for the mode the pane is in, two spaces on the others — so
that the combo, the popup and the phone's menu all say the same thing from one string. In a
proportional font the bullet is a few pixels wider than two spaces, so the mode names sit about 5 px
apart (visible in `b-altm-main-box.png`). Exact alignment would need a fixed-width gutter in the
row delegate; it is legible as it is and the design's mock draws the marker in a gutter too.
