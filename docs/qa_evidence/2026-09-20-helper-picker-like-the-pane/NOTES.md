# #PK5Q — the helper agent's model box is the terminal pane's model box

Implementer evidence, 2026-09-20. Commits `78143918` (the shared row builder), `3669df02` (both
boxes onto it, and what a pick means), `cddde462` (the keys), `b193f83c` (a box is never opened on
four rows and a gear), `eb45e571` (Alt+M asks the keyboard which box it is in), `7226236a` (only
the Main row takes the concise wording) and the commit that carries this folder.

The owner, looking at the Switchboard agent's model box beside a terminal pane's:

> can you have the picker be the same as in the main terminal

They were two lists over one idea. A terminal pane's box drew the role rows — "glm-5.3 (main)",
"glm-5.3 flash (flash)", a Local row where this machine serves one — then the per-model catalog in
rank order, then "more models…" and the gear. The helper's drew Follow Main, Flash, Lite, one row
per usable *provider*, and a gear onto the Model roles dialog. Which model a thing ran on read
differently depending on which box you happened to be looking at.

Now `relay::modelrows` builds the list once and both boxes call it. What a pick means is the same
in either: a role row sets the tier, a catalog entry pins the provider **and** the model, "more
models…" opens the same `relay::ModelPicker` dialog, and the gear opens Options › Models.

## How to run it again

```
docs/qa_evidence/2026-09-20-helper-picker-like-the-pane/drive.sh [relay-binary] [out-dir]
```

Xvfb, an isolated `HOME` / `XDG_*` / `TMPDIR` under a short path (the 108-byte socket limit) and
`RELAY_KEYRING=off`. **No real key is read or spent.** The catalog in the shots is made of stub
keys — `RELAY_GLM_CODING_API_KEY=stub`, `RELAY_KIMI_CODE_API_KEY=stub`, which is all
`has_stored_key` looks at — and every agent in the run actually answers from `stub-provider.py`
over a local endpoint. Needs Xvfb, xdotool, ImageMagick and tesseract. Each check writes one line
to `notes.txt` naming the shot it was read from.

The shots here were taken with the binary `scripts/land.py` built from the exact tree it
committed (`/tmp/claude-1000/land/helperpicker/verify/build/relay`). This checkout's `build/relay`
carries other sessions' in-flight edits, so it is not evidence of what landed.

## The shots

| | |
|---|---|
| `01-pane-box.png` | the **terminal pane's** box, dropped open with Alt+M, with the Switchboard beside it |
| `02-switchboard-box.png` | the **Switchboard agent's** box, dropped open with the same key from its own composer |
| `03-two-popups-side-by-side.png` | the two lists in one picture — the thing the card is about |
| `04-card-page-box.png` | the **card page's** box, open over an open card: Discuss and Plan are turns of that same agent |
| `05-options-helper-box.png` | the **Options helper's** box, open from its panel's composer |
| `06-picker-from-helper.png` | the model picker dialog, opened over a helper with Ctrl+Alt+M |
| `…-list.png` | each popup photographed as the X window it is — the list and nothing behind it, which is what the rows are read from |
| `_rows-pane.txt`, `_rows-switchboard.txt`, `_rows.diff` | the two lists as OCR read them, and the difference between them |

`_idle.png`, `_board.png`, `_card-page.png`, `_approvals.png`, `_switchboard-composer.png`,
`_options-composer.png` and the two `…-typed.png` are the run's working shots — how it found what
to click, and the word it typed into each composer to prove the cursor was in it.

## What the run found, and what was fixed because of it

Three real faults, in the order the runs found them.

**A helper's box listed no models at all** until somebody had asked that tab's agent a question.
The tab's helper worker is started at the **first ask** and never merely to fill a box (owner
decision 5, protocol §30.7), so a panel opened in a fresh tab has heard no `presets`: the Options
helper's box was Main agent, Flash agent, more models… and the gear, and the picker dialog behind
"more models…" opened on an empty list. The catalog is the machine's rather than any one worker's,
so `b193f83c` lends a silent box the `presets` rows a terminal pane in that tab already holds —
asked again just before the list is drawn, because the lent catalog may itself still be arriving
when the panel is built. No worker is started to draw a box.

**Alt+M in the Switchboard's composer opened the terminal pane's box.** `focusedHelperModelBox`
was asking the window which leaf it considered active, and the window still said the terminal
pane. `eb45e571` walks up from `QApplication::focusWidget()` instead: the widget the key actually
went to cannot be wrong about which box it belongs to. The run that found this had been
photographing one list twice and calling the two identical, so the script now checks *where* the
popup opened (`leftpopup` / `rightpopup`) and types a word into each composer and reads it back
before pressing any key — the two guards that make this class of false pass impossible.

**The rows did not read the same.** The Main row said "stub (main)" in a helper's box beside
"stub · local (main)" in the pane's, so `eb45e571` gave the helper's role rows the pane's own
wording (`Pane::conciseModel`) — and then the Local row said "stub · local (local)", because a
pane puts only its *Main* row through that wording. `7226236a` matched that too.

## Reading the two lists

`_rows.diff` is the whole difference between the two lists, read from the two `…-list.png` shots —
each the popup as its own X window, nothing of the page behind it. It is empty: every row matches,
in order. The Main and Flash role rows, the Local row, the local model, the guest harness models,
GLM's two, Kimi's four, Relay Free's three, "more models…" and "customize…".

One thing is still not identical, and is honest rather than a gap: in a tab whose helper has never
been asked anything, the three role rows read "Main agent", "Flash agent", "Local agent" with no
model beside them (`05-options-helper-box-list.png`). The catalog under them is the pane's, but
which model each *tier* resolved to is the worker's own answer, and it has not given one yet. They
fill in at the helper's first reply. Printing a guess there would be worse than printing the
role's name.

A guest harness (`fable · claude code`, `gpt-6-astra · codex`) is a row in the helper's list too,
deliberately: the owner asked for the same list, and hiding the row would have made the two differ
in exactly the place the card is about. Picking one for a helper is refused with the sentence
`guest_harness_provider.helper_refusal` already writes for card #GH5T — the helper works through
Relay's own `board_*` and `app_*` tools, which a guest does not take — and the box goes back to
where the helper actually is. The refusal happens before anything is stored, so the worker never
has to raise it.
