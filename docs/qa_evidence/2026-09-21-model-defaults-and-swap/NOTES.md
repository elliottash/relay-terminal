# #MDL1 t:a3 / t:a4 — one default, and /swap as a toggle

Driven 2026-09-21 under Xvfb by `drive.sh` in this directory, on `build/relay` built from the
commits below (`scripts/relay-build --check "rank 1 of the main list. /swap goes back to"` and
`--check "it starts on your first prompt"` both pass, so the binary holds this code).

The rule being shown: **a pane runs on rank 1 of the main list until you pick something else in
that pane.**

## The set-up, and why it is shaped this way

Isolated `HOME` / `XDG_*` / `TMPDIR` under `/tmp/claude-1000/df` (short, because
`XDG_RUNTIME_DIR` holds a unix socket and the path limit is 108 bytes), `RELAY_KEYRING=off`, two
fake keys, `isolation/enabled=false` (the worker exits under a fake `XDG_RUNTIME_DIR` otherwise),
X display in 380–410. No turn is ever run: every step here is a model *switch*, which the worker
answers without calling a provider.

`relay.conf` is seeded with

    [provider] preset=kimi-code  model=k3
    [agent]    effort=high
    [models]   tier/main = glm-coding|glm-5.3-flash|low, glm-coding|glm-5.3|max, kimi-code|k3|high

Three things are deliberate:

* **rank 1 is `glm-5.3-flash`, which is not what `glm-coding` serves by default.** A pane that
  merely followed the *preset* would land on `glm-5.3`. Only a pane that reads the list's **model**
  lands on `glm-5.3-flash`.
* **`provider/preset` / `provider/model` name a different provider and a different model.** That
  pair is exactly what a new pane read before this card.
* **rank 3 is `kimi-k3`.** `/swap` is exercised from a model that is neither of the two keys the
  old implementation knew — the owner's case, and the one it could never come back from.

Two profiles are seeded, `work` (the list above) and `admin` (the same three with `kimi-k3` first),
so the main list can be reordered *live*, in the app, with one typed command.

## What each shot shows

Each `*.png` has a `*-chips.png` beside it: the composer strip, cropped, where the model box and
the level box are.

| shot | what it proves |
|---|---|
| `01-new-pane-rank1` | the first pane opens on **glm-5.3-flash · low** — rank 1's *model* and rank 1's *level* — while `provider/preset=kimi-code`, `provider/model=k3` and `agent/effort=high` say otherwise |
| `02-pane-a-picked-k3` | `/model k3` in pane A: **kimi-k3 · high** |
| `03-pane-b-still-rank1` | Ctrl+Shift+E: pane A stays on kimi-k3, pane B opens on **glm-5.3-flash · low**. A pick is that pane's; it is no longer the next pane's default |
| `04-pane-b-on-k3` | pane B put on rank 3, `kimi-k3` |
| `05a-swap-immediately` | 0.4 s after `/swap`: the pane is already on glm-5.3-flash |
| `05-swap-to-rank1` | 2.9 s after `/swap`, so *after* the `model_changed` that used to wipe it: **"Swapped to glm-5.3-flash · z.ai (glm) — rank 1 of the main list. /swap goes back to kimi-k3."** |
| `06-swap-back-to-k3` | `/swap` again: **"Back on kimi-k3 · kimi — where this pane was. /swap returns to glm-5.3-flash."**, and the box is on kimi-k3. The old `/swap` went to rank 2 here and ping-ponged 1↔2 for ever |
| `07a-profile-said` | `/profile admin` reorders the main list live: **"Profile: admin · main runs on kimi-k3 (/swap puts this pane on it)."** — the line used to say `/main`, which put a pane on nothing of the sort |
| `07-profile-admin` | three seconds later, the two panes are where they were: switching the list moves no pane |
| `08-pane-c-follows-the-list` | Ctrl+Shift+T: the next new pane opens on **kimi-k3 · high**, the reordered list's new rank 1 |
| `09-pane-c-picked` | pane C picks `glm-5.3` (rank 3 under `admin`): **glm-5.3 · max**, the level that entry carries in the list |
| `09b-list-put-back` | `/profile work` puts rank 1 back to glm-5.3-flash, so at the quit **no pane is sitting on rank 1** |
| `10-restored` | after SIGTERM and a relaunch: pane C comes back on **glm-5.3 · max**, with its scrollback. Rank 1 is glm-5.3-flash, so this is the pane's own saved model, not the default |
| `11-restored-other-tab` | the other tab: panes A and B come back on **kimi-k3 · high** |

`saved-layout.json` is the layout store after the restart: two tabs, `{"model": "k3", "preset":
"kimi-code"}` twice under an `h` split and `{"model": "glm-5.3", "preset": "glm-coding"}` in the
second tab. `conf-before.txt` / `conf-after-run1.txt` / `conf-after-run2.txt` are the settings file
at each stage — note `agent/effort` is still `high` at the end, although every pane in the run
started at `low` or ran at `max`: a level that came with a model is the pane's and no longer
rewrites the default new panes start at.

## Two things the run turned up that are not this card's

* **`--workspace` suppresses the restore.** `src/main.cpp` treats an explicit `--workspace` as "open
  a new window here", so run 2 is launched with no arguments at all. Worth knowing for the next
  restore drive; it is documented behaviour, not a defect.
* **Nothing configured at all, in any pane.** The first drive never reached a `configured` event:
  every `configure` was refused with `Invalid key 'Ctrl++': empty part`, because `#Z00M` had bound
  `terminal.zoomIn` to the plus key that morning and the worker's keybinding parser rejected the
  way Qt spells it — and the shortcut catalogue travels inside `configure`. The GUI looked
  ordinary while it happened: the model box still names a model, because with no model reported
  the box falls back to the preset's main row. Fixed in `4d540c0e`, with the test that would have
  caught it (every default key, and every preset table's key, through the worker's own parser).

## Targeted tests

    ctest --test-dir build -R "modelcatalog|modelpicker|modelrows"     3/3 pass
    PYTHONPATH=backend python3 -m unittest tests.test_keybindings      23 pass

`tests/modelcatalog_test.cpp` has eleven new cases for the two decisions this card adds
(`startEntry`, `swapTarget`): rank 1's model and level, a guest at rank 1, a restored entry coming
back, a restored entry that is gone / has no key / is spent, an empty catalog, the swap toggle
there and back, rank 2 when nothing is remembered, a remembered model that cannot run, a spent
model swapping to the first live entry, a one-model list, and the "not ready" wording.
