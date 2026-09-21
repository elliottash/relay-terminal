# The Ctrl+Alt+M dialog: pick *and* prioritize — card #MDL1, t:a7

Owner, 2026-09-21: *"and i realized that the model priority chooser is crtical, and currently its
too hard to find -- model options, then scroll down. i think we should beef up the ctrl alt m
dialogue to be the main way to select / prioritize models."*

`drive.sh` is the whole run under Xvfb, on an isolated `HOME`/`XDG_*`/`TMPDIR` in
`/tmp/claude-1000/dg`, `RELAY_KEYRING=off`, `isolation/enabled=false` (the worker's sandbox will
not start under a fake `XDG_RUNTIME_DIR`, and with no worker there is no catalog). Three literal
non-key strings make three presets "stored" — `glm-coding`, `glm` and `kimi-code`. `glm` and
`glm-coding` serve the same model on purpose: that is the folded row with `+1`. Nothing is ever
submitted to a provider.

    docs/qa_evidence/2026-09-21-model-dialog-prioritize/drive.sh

## What each shot shows

| shot | what it proves |
|---|---|
| `0-filled.png` | Options › Models, "fill the lists" pressed, so the five lists have something in them |
| `1-main.png` | the dialog on **main**, the tab the pane's mode picks: `high · main · flash · lite · all` across the top (no `local` — nothing is served locally here), the list numbered 1…4, the model named once with its provider in **via**, rank 1 carrying "· new panes start here", the level list on the right headed by **default**, and a footer spelling the eight keys |
| `2-flash.png` | one **Right** in the empty filter: the flash list, in *its* order (`glm-5.3-flash`, `kimi-for-coding-highspeed`, …), and no "new panes start here" — that sentence is main's alone |
| `3-moved.png` | **Alt+Down** on rank 1: `glm-5.3` is rank 2, `kimi-k3` is rank 1, and the note moved with the rank rather than with the model |
| `4-not-in-list.png` | typing `flash` on the **main** tab searches every model: nothing in this list matches, so the rule **not in this list** and the rest of the catalog folded one row per model, each with a `+ add` cell |
| `5-added.png` | **Ctrl+Enter**: `glm-5.3-flash` is rank 5 of main at the level Options' "+ add a model…" would have given it (`low`), and the filter cleared so you can see where it landed |
| `6-deleted.png` | **Delete**: rank 5 is gone, with no confirmation — the footer says Ctrl+Z is the answer |
| `7-undone.png` | **Ctrl+Z**: it is back, at rank 5, with its level |
| `8-all.png` | the **all** tab: one row per model whatever serves it — `glm-5.3` and `glm-5.3-flash` say `z.ai (glm) · coding plan  +1` because two presets serve each — favorites/recent/sort intact, and the pane's own model bold with "· current" |
| `9-via.png` | filter `glm-5.3`, **Tab** into the rows, **→**: the row's two providers open above the levels, the chosen one drives the row and the level list follows it |
| `10-options.png` | Options › Models opens on **prioritize models… (Ctrl+Alt+M)** as its first row, with the live chord in the button |

## What was changed after looking at the shots

The first run found three things and they are fixed in the code these shots were taken from:

- the **via** list clipped its own text (`z.ai (glm) · coding pla`) at 150 px with a horizontal
  scrollbar — it is 210 px, elides and carries the full text in its tooltip;
- the **reasoning** list stretched to the height of the models, an empty well beside four words —
  it is capped and the column takes up the slack below it;
- **→** from the filter changed tab (the filter owns ←/→) and so could never open a row's
  providers. Tab now moves the focus along filter → rows → providers → levels, the view no longer
  eats Tab for cell navigation, and the `all` tab's footer says so.

A fourth, smaller one: a tab that does not hold the row you were on now opens on the pane's own
model where it has it, and on rank 1 otherwise, rather than on whatever row happened to be first.

## The keys, as the footer states them

Tier tab: `←→ tab · ↑↓ row · enter uses it · alt+↑↓ moves it · del removes it · type a name,
ctrl+enter adds it · ctrl+z undoes`

`all` tab: `←→ tab · ↑↓ row · enter uses it · tab, then → : the providers of a folded row, and the
levels`

Everywhere: Ctrl+Tab / Ctrl+Shift+Tab change tab whatever has the focus, Backspace removes a row
while the filter is empty, Escape leaves everything as it was, and a single click only highlights.

## Tests

`ctest --test-dir build -R modelpicker` — 20 cases, all passing
(`tests/modelpicker_test.cpp`): the tabs and which one it opens on, `local` appearing only where
something is served locally, ←/→ and Ctrl+Tab (and the caret keeping the arrows while there is
text), the numbered list and rank 1's note, greyed-in-place for three different reasons (no key,
exhausted, not in the catalog at all), Alt+Down and a drag persisting through
`curation::tierList`, Delete then Ctrl+Z, "not in this list" and Ctrl+Enter (and a guest not being
offered to flash or lite), a level written into the list and "default" read back, the three
providers of `gpt-5.6-sol` folded into one row with "via" picking one, a click that only
highlights, the profile box swapping the lists, and the footer changing with the tab.
