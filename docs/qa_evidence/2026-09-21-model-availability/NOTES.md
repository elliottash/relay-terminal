# #MDL1 — the four steps of model availability, and the box's typed filter

Owner, 2026-09-21, verbatim:

> "the text filter isnt working -- its supposed to show all available models, not just the ones
> selected for the box picker. on this point -- i notice now that we lost functionality. there need
> to be 4 steps of model availability: 1 add provider, 2 add model as available, 3 add model to
> priority list, 4 include model in box picker. we currently only have 1, 3, 4. and its step 2 that
> determines the models available in the text filter. for branded providers, all models are
> included by default and you can uncheck them (eg i probably want to uncheck sonnet and haiku and
> gpt 5.5). but then for openrouter, you have to select specific models -- and maybe there are some
> recommended ones by default, deepseek 4.1 and gemini 3.8 flash for example."

Design: `docs/MODEL-PICKING-DESIGN.md` §5.7. `drive.sh` in this folder is the run, re-runnable.

## The run

Xvfb on `:680`, an isolated `HOME`/`XDG_*`/`TMPDIR` under `/tmp/claude-1000/av` (short, for the
108-byte unix-socket limit), `RELAY_KEYRING=off`, three fake provider keys and **no network turn**:
every step is a page draw, a dialog or a combo popup. The OpenRouter listing is real — the owner's
cached 446 rows copied into the sandbox with `RELAY_OPENROUTER_CATALOG=off`, so nothing fetches.

The five lists are seeded so that some available models are in **no** list (kimi's `k3-256k`,
`kimi-for-coding`, `kimi-for-coding-highspeed`, and OpenRouter's three recommended rows), which is
what the box's `other models` section is for.

**The binary is a clean export of the landed tree**, not `build/`: the shared checkout carries other
sessions' uncommitted edits and one of them (`relay::boardCardIdForFile` in `Pane.h`, session
`card-file-link`) does not compile yet.

    git archive main | tar -x -C <scratch>/src     # fd8df23cae12 — the commit this run proves
    cmake -S <scratch>/src -B <scratch>/build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF
    cmake --build <scratch>/build --target relay
    strings -el <scratch>/build/relay | grep -F "other models"                 -> 1
    strings -el <scratch>/build/relay | grep -F "every model you have made available"  -> 1
    strings -el <scratch>/build/relay | grep -F "models.available:"            -> 1

## What each shot shows

| shot | what it proves |
| --- | --- |
| `a-options-provider-link.png` | Step 1 → step 2. Under every provider row, **models… (N of M available)**: "4 of 4" for kimi, "2 of 2" for z.ai, **"3 of 446"** for openrouter — the owner's two rules, side by side, in one line each. |
| `b-link-lands-on-all-tab.png` | That link pressed: the dialog's `all` tab with `openrouter` typed. The three recommended rows (`deepseek-v4.1-flash`, `gemini-3.8-flash`, `gemini-3.5-flash-lite`) are **ticked**; everything under **more from openrouter** is un-ticked and greyed. |
| `c-all-tab-available-column.png` | The whole `all` tab, filter cleared: an **available** column, and the rows grouped by provider with the provider's name as a rule (kimi · z.ai (glm) · relay · openrouter · claude code). Every branded model ticked. |
| `d-openrouter-tail-unticked.png` | "muse" typed: seven live OpenRouter rows under **more from openrouter**, every box empty. Ticking one is "you have to select specific models". |
| `e0-before-untick.png`, `e-unticked-branded-model.png` | `kimi-for-coding-highspeed` un-ticked. The row **stays**, with an empty box to tick again — and the Options page behind the dialog has already changed to **models… (3 of 4 available)**. |
| `f-box-filter-other-models.png` | Alt+M, `kimi` typed. `main → kimi-k3` under its own class header, and `other models → kimi-for-coding` — a model that is available (step 2) but in no list. `kimi-for-coding-highspeed` is **not** there: it was un-ticked a moment ago, which is the owner's sentence ("its step 2 that determines the models available in the text filter") tested end to end. |
| `g0-highlighted.png`, `g-enter-switches-the-pane.png` | Down onto that row, Enter: the pane's chip reads **kimi-for-coding**. A model in no list becomes the pane's own model, on main. |

## The storage, read back

`conf-before.txt` has no `models/available`: the default is not written until the first un-tick.
`conf-after.txt` has it, and it is exactly the default **minus the one model un-ticked** — note what
is and is not in it:

* every model of every branded provider (kimi, z.ai, minimax, openai, anthropic, gemini, deepseek,
  relay-free, and both guests) — including the ones with no key, so adding a key later does not
  arrive with half the provider hidden;
* `kimi-code|kimi-for-coding-highspeed` is **absent** — the one un-tick;
* of OpenRouter's 446 rows, exactly three: `deepseek/deepseek-v4.1-flash`, `google/gemini-3.8-flash`
  and `google/gemini-3.5-flash-lite`, which are the rows its built-in catalog names.

## Targeted tests

`ctest --test-dir build -R "modelcatalog|modelpicker|modelrows|filterpopup|settings"` — 8/8 pass
(modelpicker, filterpopup, modelrows, modelcatalog, modelsettings, settingscache, settings,
remotesettings).

## One thing for QA to look at

In `f-box-filter-other-models.png` the last row of the popup sits flush against the frame — the
descender of "kimi-for-coding" touches it. Every row is drawn and nothing scrolls; it is a pixel or
two of tightness in `FilterPopup::layoutForAnchor`'s height, which shows only on a real X display
(an offscreen probe of the same rows reports `scrolling false` and every row visible). It predates
this card — `layoutForAnchor` is already a series of fixes for exactly this arithmetic — and it is
left alone here rather than changed under a card about availability.
