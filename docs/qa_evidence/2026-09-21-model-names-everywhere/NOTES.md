# One name, everywhere a model is printed — #MDL1 task t:a5

Rule 1 of `docs/MODEL-PICKING-DESIGN.md`: *"a model has one name, and everything prints it"*, and
*"tier and role words are lower-case too: main, flash, not Main agent"*. The library half landed in
`f3c2593a` (`relay::models::nameOf`, `Entry::name`, `Catalog::resolveKey`, `findByName`, the
worker's `presets.model_name`) and the model box's own rows in `44b7b026`. This is every remaining
site.

## What runs this

`drive.sh` — Xvfb, an isolated `HOME`/`XDG_*`/`TMPDIR` under a short path (the 108-byte
unix-socket limit), `RELAY_KEYRING=off`, `isolation/enabled=false`, and **four** fake provider keys:
`glm-coding` and `glm` both serve `glm-5.3`, `kimi-code` and `kimi` both serve Kimi K3 (whose ids
are `k3` and `kimi-k3`). Two providers per model is what makes `@provider` mean something and what
puts two spellings of one model in history. No turn is taken: every step is a model switch or a
panel opening, which the worker answers without calling anybody.

`seed_sessions.py` writes three saved conversations under three ids of two models — `k3`,
`kimi-k3`, `openai/gpt-5.6-sol` — so the Sessions pane has something to name.

Binary under test: `build/relay`, build `2026-09-21.09H.13`. The strings are in it (the literals
are UTF-16, so `strings -el`, and `·` splits them):

```
$ strings -el build/relay | grep -cF "model: %1 "        # 5
$ strings -el build/relay | grep -cF "Already on %1%2."  # 1
$ strings -el build/relay | grep -cF "%1 for this pane%2"    # 1
$ strings -el build/relay | grep -cF "gpt-5.6-sol@openrouter" # 1  (the /model help)
$ strings -el build/relay | grep -cF "Main agent"        # 0
```

## What the shots show

| shot | what it proves |
|---|---|
| `01-model-switch-said.png` | **model: kimi-k3 · conversation kept** — lower-case, and the *name*: the Kimi Coding Plan's id for this model is `k3`, which only its catalog row can turn into a name. |
| `02-flash-said.png` | **flash: glm-5.3-flash · conversation kept** — never "Flash agent". |
| `02b-already-on-flash-said.png` | **Already on flash · glm-5.3-flash.** (was "Already on the Flash agent · glm-5.3-flash.") |
| `03-main-said.png` | `/main` puts the pane back: **model: kimi-k3 · conversation kept**. |
| `04-model-tooltip-chips.png` | the model chip's tooltip: `main: kimi-k3 (same as main)`, `terminal use: glm-5.3-flash`, `subagents:`, `helpers:`, `high:`, `flash:`, `local:`, `plan mode:`, `summaries:`, `suggestions:`, `chores:`, `request audit:`, `loop check:`, `vision:`, `route assist:` — every role lower-case, every model a name. It read "Main agent: glm-5.3", "Switchboard agent: …" before. |
| `05-glm-said.png` | `/glm` lands on **glm-5.3**, by name. (The line the command itself prints, "model: glm-5.3 · z.ai (glm).", goes to the status line and is then spoken over by `model_changed`'s own toast — the same overwrite §1.4.2 describes for `/swap`, and not this task's to fix.) |
| `06-at-glm-coding-said.png`, `06b-at-glm-coding-info.png` | `/model glm-5.3@glm-coding` → **glm-5.3 · z.ai · coding plan (glm-coding)**. |
| `07-at-glm-said.png`, `08-session-info.png` | `/model glm-5.3@glm` → **glm-5.3 · z.ai · standard api (glm)**. One name, two providers, and the "@" says which. The Model row also shows the provider half with the model taken out of it: it read "glm-5.3 · z.ai · glm-5.3 · standard api (glm)" before, naming one model twice. |
| `09-conversations.png` | the Sessions pane's Model column: **gpt-5.6-sol**, **kimi-k3**, **kimi-k3** — the rows recorded as `k3`, `kimi-k3` and `openai/gpt-5.6-sol`. Nothing on disk changed. |
| `10-model-filter-menu.png` | the model filter: **Any model · gpt-5.6-sol · kimi-k3**. One entry per model; `k3` and `kimi-k3` are one line, and picking it selects both rows. |

`*-chips.png` is the composer strip of the same shot (the model chip), `*-said.png` the toast.
The toast is captured at 2.2 s, not at once: a model switch also re-applies the reasoning effort,
and its "Effort: high" toast is queued in front for 1.6 s. That was measured, not guessed.

A first run of this script **crashed Relay** at the first `/model kimi-k3`: `findByName` returns an
entry of the list it is handed and the list was a temporary in an `if` condition, so `selectEntry`
read a freed key (`SIGSEGV` in `Catalog::splitKey`). Fixed in `9eccaa8e`; the frames are in the
git history of `relay-stderr.log`'s first version and quoted in that commit message.

## Tests

| test | result |
|---|---|
| `ctest -R modelrows` — `roleLabelsAreLowerCaseAndOneTable` | pass |
| `ctest -R conversations` — `theModelColumnAndTheInfoPanelPrintNames` | pass |
| `ctest -R "modelpicker\|modelcatalog\|modelsettings\|panestate\|panestatus\|paneusage\|boardpane"` | pass (8/8) |
| `tests/test_model_switch.py::test_every_model_a_switch_names_travels_with_its_name` | pass |
| `tests/test_conv_index.py::test_the_model_filter_and_its_menu_work_on_names_not_ids` | pass |
| `tests/test_web_model_name.py` (6 cases; runs `app/modelname.js` under Node and compares every answer with `presets.derived_name`) | pass |
| `tests/test_roles.py`, `test_presets.py`, `test_local_tier.py`, `test_conv_index.py`, `test_model_switch.py` | 280 run, 1 fail |
| `tests/test_guest.py`, `test_guest_harness_provider.py`, `test_session_protocol.py`, `test_failover.py`, `test_sessions.py`, `test_configure_provider.py` | 239 run, all pass |
| `tests/test_web_manifest.py`, `test_board_view.py`, `test_remote_pane_state.py`, `test_remote_control.py` | 142 run, all pass |

The one failure, `test_presets.TierTableTests.test_role_tiers_cover_every_role`, is **not this
task's**: it already failed at `7f8f0616^`, before anything here landed. `roles.ROLES` grew a
`high` role that `ROLE_TIERS` does not cover yet — half of the `/high` work another session holds.

## The grep audit

Run from the repository root, against the tree at `bae87fd7`. The commands are in
`scripts`-less form on purpose: they are what a reviewer should be able to paste.

```sh
# A. a Title-Case tier or role word in a string a person reads
grep -rn 'QStringLiteral("[^"]*\(Main agent\|Flash agent\|High agent\|Lite agent\|Local agent\|Switchboard agent\|Helper agent\|Terminal-use agent\)' src/ --include=*.h --include=*.cpp
grep -rn '"[^"]*\(Main agent\|Flash agent\|High agent\|Lite agent\|Local agent\|Helper agent\)' backend/relay_core/*.py backend/*.py app/*.js

# B. a raw model id printed under a "Model:" label
grep -rn 'Model: %1\|"Model: \|Model: ${' src/ app/ backend/ --include=*.h --include=*.cpp --include=*.js --include=*.py

# C. the two functions that used to answer "what model is this"
grep -rn 'conciseModel\|presetLabelOf' src/ --include=*.h --include=*.cpp

# D. an event's or a row's `model` field printed with no naming call
grep -rn 'value(QStringLiteral("model")).toString()' src/*.h src/*.cpp \
  | grep -v 'modelNameFor\|modelName\|modelNameOf\|rowModelName\|nameOf\|resolveKey\|m_model =\|m_guestModel\|m_ctxNext\|selectEntry\|keyFor\|== \|request\|insert\|settings\.\|//'
grep -rn 'event.model\b\|\${model}\|attrs.model' app/*.js
```

**B** is clean: the one hit is a comment quoting the old sentence. **D** is clean in `app/`
(`${model}` is now `modelName(event)`'s answer) and, in `src/`, every remaining hit is a model id
being *used* — sent in a request, stored, compared, or put into a catalog — not printed.

What **A** and **C** still list, and why each is left:

- **`roles.ACTIONS`** (`backend/relay_core/roles.py:113-118`) — "New panes (Flash agent)", "Panes on
  the Local agent (/local)", "Helper agent". These are the *row headings* of Options › Agent roles'
  Advanced list, in the sentence case that page uses throughout ("Agent turns", "Driving programs in
  the terminal", "Images and vision turns"). They name a job, and the model box beside each row is
  where the model is named — which is now lower-case and a name. Lower-casing three of fifteen
  headings would read as a bug. `roles.LABELS`, which is what reaches a person in a *sentence*
  ("flash: no stored key for kimi; using main."), **is** lower-case now.
- **`src/RelayWindow.h:4112,4122`** — "Flash agent for this pane", "Local agent for this pane" in
  the Actions palette, and the same wording in `src/Keymap.h`'s shortcut descriptions. These name an
  *action*, beside "Clear terminal" and "Open last agent turn", in the palette's own sentence case;
  `tests/settingspane_test.cpp` pins the first. The toast the action raises is lower-case
  ("flash for this pane · glm-5.3-flash"), which is the line that names a model.
- **`src/BoardPane.cpp`** — "The Switchboard agent is busy", "Ask the Switchboard agent". These name
  the Switchboard's agent as a thing in the product, not a model row; `docs/ARCHITECTURE.md` uses the
  same words. No model is named in any of them.
- **`Pane::conciseModel` and `Pane::presetLabelOf`** — nothing outside the model box's own row
  builders calls `conciseModel` any more (`roleRowModel`, and `in.choices` in `remoteState`), and
  `presetLabelOf` is left as the fallback in `servingName` for a preset the catalog has no row for.
  Both belong to the box, which the `fable-box` session holds; removing `conciseModel` is its call,
  not this one's.

## What is left for the owner

Nothing this task could decide. Two notes for whoever picks the card up next:

- `/glm` and `/kimi` print their own sentence and `model_changed` speaks over it 100–300 ms later —
  the overwrite design §1.4.2 describes for `/swap`, which `e7cab7d2` fixed for `/swap` alone by
  handing the handler's sentence to the report. The same one-shot would work here.
- `PRESETS[...].label` carries the model for three presets ("z.ai · glm-5.3 · coding plan"), which is
  why `providerBeside` in `src/SessionInfo.cpp` has to take it back out. Changing those labels is a
  wider change than this task, and they are also what Options › Models' provider headings read.
