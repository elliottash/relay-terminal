# The reasoning levels a pane offers are the model's own — #MDL1, 2026-09-21

Owner, on the ranking file that day:

> "i want the effort options in relay to be determined by the model … so xhigh shows up for codex
> for example"
>
> "for no knob models, the effort box should be grayed out. same for relay free."

Driven with `drive.sh` under Xvfb on display :640, against `build/relay` at commits `3ba73e82`
(the change), `35e7bccf` (the snap sentence and the box's width) and `07f89b71`-era `src/Theme.cpp`
(the greyed box's colour). The binary holds the code:

```
$ strings -el build/relay | grep -E "Relay Free sets the level|is not a level of|has no reasoning level"
%1 (this model has no reasoning level)
This model has no reasoning level.
%1 is not a level of %2 here · using %3
%1 has no reasoning level
Relay Free sets the level for you
```

Two runs, because the two halves need opposite sandboxes. Both use an isolated
`HOME`/`XDG_*`/`TMPDIR` under a short path (the 108-byte unix-socket limit), `RELAY_KEYRING=off`,
`isolation/enabled=false` and fake keys; no turn is ever sent, because every step is a model switch
or a level pick, which the worker answers by itself.

* **run 1 — providers with keys.** `openai`, `kimi-code` and `glm-coding`.
* **run 2 — codex.** A fake `codex` on `PATH` answers `codex debug models` with six reasoning
  levels; the worker reads that JSON itself (`guest_harness_codex.catalog_rows`) and sends the list
  on the `presets` event, so the six levels are the provider's, not the drive's. No provider key at
  all and `cryptography` shimmed out so Relay Free cannot stand in: the pane has nothing to
  configure on at the first `presets`, waits, and takes rank 1 of main — codex — when the
  background catalogue scan lands. A harness ranked first is held until the first prompt, so
  nothing is spawned and the box is drawn from the catalogue.

## What each shot shows

| shot | what |
| --- | --- |
| `a-kimi-pane` | the pane on `kimi-k3`, level `high` |
| `b-alt-e-kimi-three` | Alt+E: **three** levels — `low`, `high`, `max`. No `medium`: Kimi has none, and Relay used to offer one that was the same request as `high` |
| `c-openai-pane`, `c2-alt-e-openai-xhigh` | `/model gpt-6-astra@openai`, then Alt+E: **four** — `low`, `medium`, `high`, **`xhigh`**. A word Relay's retired four could not hold |
| `d-effort-xhigh` | `/effort xhigh` typed and taken; the box reads `xhigh` |
| `e-snap-to-max` | `/model kimi-k3`: Kimi stops at `max`, so the level snaps, and the sentence rides the switch's own line — "model: kimi-k3 · conversation kept · xhigh is not a level of kimi-k3 here · using max" |
| `f1-relay-free-greyed`, `f2-relay-free-tooltip` | `/model relay-main`: the level box is **greyed**, not hidden, and its tooltip says "Relay Free sets the level for you." |
| `g1-alt-e-says-why`, `g2-effort-says-why` | Alt+E and `/effort low` on that pane say the same sentence instead of opening or changing anything |
| `h-slash-popup-fixed` | the `/` popup's own row: `/effort  Relay free sets the level for you.` — no argument to offer |
| `i-codex-pane` | run 2: a codex pane, held deferred, on `xhigh` |
| `j-alt-e-codex-six` | Alt+E: **six** — `low`, `medium`, `high`, `xhigh`, `max`, `ultra`, in codex's own order |
| `k-effort-ultra` | `/effort ultra`: a level no Relay vocabulary ever had |
| `l-slash-popup-six` | the `/` popup's row for this pane: `/effort [low\|medium\|high\|xhigh\|max\|ultra]` |
| `m-dialog-codex-levels` | Ctrl+Alt+M: the codex row's *reasoning* column reads `xhigh`, and its level list is the same six |

`*-box.png` is the bottom of the window — the composer strip with the model and level boxes, the
status bar under it, and whatever list dropped open above them.

## One thing found here that is not this card's to fix

`relay::FilterPopup` — the list Alt+M and Alt+E drop open — measures a list one row short of what
it draws once the app's stylesheet is on it. Two consequences, both visible above:

* `c2-alt-e-openai-xhigh`: the last row (`xhigh`) is clipped by a few pixels at the bottom of the
  popup.
* A list that got **shorter** between two opens scrolls past its first row: opening Alt+E on the
  OpenAI pane (four levels) and then on the Kimi pane (three) drew "high" and "max" with "low"
  scrolled off the top, while the box itself held all three
  (`DBGEFFORT … efforts=low,high,max box=low,high,max`). It is persistent, not a first-draw
  transient — a second Alt+E draws the same two.

The level box is filled from the model's own `efforts` now, so its length changes with every
switch and this is much easier to hit than it was. It is `src/FilterPopup.cpp`'s own sizing, not
the box's contents, and two other sessions are working that surface, so it is left to whoever owns
it. `tests/filterpopup_test.cpp :: aShorterListOnTheNextOpenDrawsEveryRow` states the expectation;
it passes headlessly, because the app stylesheet is not on the popup in that test, which is exactly
why the sizing error does not show there. The drive opens the lists in the growing order
(kimi 3, then openai 4) so the shots show the box's real contents rather than that bug.
