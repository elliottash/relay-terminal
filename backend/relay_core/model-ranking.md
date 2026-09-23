# Model ranking

The four tables below are what Relay's **default** priority lists are built from: the model box's
"fill from defaults", Options › Models' two `defaults` buttons, and the lists a fresh install
starts with. They are data, not code — edit them, restart the worker, press `defaults`, and the
lists change. Nothing else in the repo holds a model's score, a provider's rank or the level a
model starts at any more (`presets.INTELLIGENCE` and the old `_MAIN_GROUP_ORDER` are views over
this file, and `LITE_LIST_FIRST` was deleted when the Provider picks table took its place).

- **Providers** — who is offered, and in which order ties break.
- **Models** — which class each model is a default for, and how good it is.
- **Provider picks** — a provider whose defaults differ from the shared Models rows.
- **Levels** — the reasoning level a model starts at in each class.

The last two were added on 2026-09-21 and are **optional**: a copy of this file without them
parses, and every rule they carry falls back to the one in code. That is what lets a table be
added, or emptied, one at a time.

What they do **not** hold: request bodies, endpoints and the OpenRouter twin map, which stay in
`presets.py` — they are what a provider's API takes, not an opinion about which model is better.
Nor the *set* of levels a model has: that is the provider's own list, reported by its API or its
CLI (`presets.EFFORT_LEVELS`), and the Levels table below only says which of them to start at.

How the defaults are computed from them (`presets.tier_list_defaults`, design section 5.4). Count
the providers that can take a turn *right now* — a built-in preset with a stored key, a guest
harness that is installed and signed in, a custom provider with a key; **not** a model server on
this machine, and **not** Relay Free:

- **none** → Relay Free's three rows, and nothing else;
- **one** → one model per class from that provider: the highest score whose `classes` names the
  class;
- **two or more** → two models per class, by score descending, **at most one per provider per
  class** and never the same model twice. A blank score sorts last; ties break by the provider's
  `order`, then by name.

Relay Free is never listed once any other provider can take a turn — **except in `lite`**, which is
`relay-lite` and nothing else whenever Relay Free can run, however many keys are stored (owner,
2026-09-21: "for lite, i am thinking to simplify that and just everybody is on relay free by
default, or openrouter if they want privacy"). That is why the providers' own `lite` cells below
are empty. The `local` class is the model servers on this machine, in the order they were saved,
and is not ranked here.

A guest harness is counted as a provider and its models are scored by **name** like anyone else's
— `gpt-6-astra` through codex scores what `gpt-6-astra` through the OpenAI API scores — and is
offered for `high`, `main` and `flash` (`roles.GUEST_TIERS`; flash since 2026-09-21, owner: "the
worker should allow the harness for flash"). Never for `lite`: a flash *pane* is a conversation a
harness can own from its first turn, while every background job on those two tiers — terminal use,
summaries, suggestions, chores, the audit, the loop check — is a side call into a conversation
already running elsewhere, which a whole agent of its own cannot be handed. Those jobs skip a
guest entry and take the next one; on a pane whose own model is a harness, with nothing left in
the list, they run on Relay Free's role for the tier instead.

## How to edit

- **Providers** — one row per preset id the worker knows, plus the two guest harnesses.
  `kind` is one of `plan` (a subscription), `harness` (a guest CLI), `api` (a first-party
  pay-as-you-go endpoint), `router` (OpenRouter) or `free` (Relay's own hosted allowance).
  `order` is the tie-break, **lower first, and every row's number must be its own**: two
  providers at the same number decide nothing, the sort falls through to the name, and the file
  reads as a ruling it is not making (`check()` says so). The bands as they stand are 10s
  harness, 20s plan, 30s api, 50s router, 90s free — the owner re-ordered them on 2026-09-21, and
  `Ranking.kinds()` derives the preference order from this column, so re-numbering the table is
  the whole of changing it.
- **Provider picks** — one row per provider whose defaults differ from the shared Models rows. A
  cell names a model by its **name** in the Models table and replaces that provider's candidate
  for that class outright, whatever the Models table classes it as; a blank cell follows the
  Models table, and a provider with no row follows it entirely. Only `openrouter` has a row
  today; the owner asked for the table so that "we could add that for cerebras for example later
  on". A pick must be a model that provider actually serves — for OpenRouter that includes
  anything in the twin map — and `check()` says so when it is not.
- **Levels** — one row per model **name**, giving the level it starts at in each class, in the
  words its provider uses (`xhigh` through codex, `max` through Kimi). A blank cell is the rule in
  code: the model's top level for `high`, the provider's own default for `main`, its lowest for
  `flash` and `lite`. A model with no reasoning knob ignores its row. The table is keyed by name
  and one name can be served two ways, so a cell is mapped onto whichever provider is about to run
  the model (`presets.nearest_effort`): `gpt-6-luna | high = max` is `max` through codex and
  `xhigh` through the OpenAI API, which is that model's top there. `check()` reports a level no
  provider that serves the model offers.
- **Models** — one row per **name** (design rule 1: one model has one name, lower-case, no spaces,
  no vendor prefix, whoever serves it). `classes` is any of `high`, `main`, `flash`, `lite`,
  comma-separated, or `-` for a model that is a default for nothing. `score` is the owner's
  intelligence number (seeded 2026-09-20 from the Artificial Analysis Intelligence Index v4.3.2,
  each model at its highest reasoning level); leave it blank for a model nobody has scored, which
  sorts last rather than zero. `notes` is free text and is read by nobody.
- Rows are kept sorted by score descending, then by name. Nothing depends on the order — the
  parser reads the whole table — but a diff is easier to read when the file stays sorted.
- Both per-class tables read their classes off the **header**, so a class may be left out and the
  four may be written in any order; a trailing `notes` column is read by nobody. `-`, `none` and a
  blank cell all mean "nothing said here".
- `python3 -m unittest tests.test_model_ranking` checks this file: every model the worker's
  catalog names has a row, every provider row is a preset the worker has, no duplicate names, no
  class word outside the four, no pick a provider does not serve, and no level a model has not
  got.
- A serving variant (`-highspeed`, `k3-256k`, `:batch`) is a different model to the person picking
  one, so it gets its own row (design section 3.3). It may share a score; it rarely shares a class.

## Providers

| provider | kind | order |
|---|---|---|
| guest:codex | harness | 11 |
| guest:claude | harness | 12 |
| glm-coding | plan | 21 |
| kimi-code | plan | 22 |
| minimax | plan | 23 |
| relay-pro | plan | 24 |
| openai | api | 31 |
| anthropic | api | 32 |
| gemini | api | 33 |
| deepseek | api | 34 |
| glm | api | 35 |
| kimi | api | 36 |
| openrouter | router | 51 |
| relay-free | free | 90 |

## Models

| name | classes | score | notes |
|---|---|---|---|
| claude-fable-5.1 | high | 53 | claude code's `fable`, and `anthropic`'s `claude-fable-5-1`; `anthropic`'s high |
| gpt-6-astra | high | 53 | `openai`'s high, and the one codex lists first |
| claude-opus-5-5 | main | 51 | `anthropic`'s main model; claude code's `opus` is the same model |
| gpt-6-sol | main | 47 | `openai`'s main; codex lists it and the OpenAI API serves it |
| glm-5.3 | high, main | 45 | one row for `glm` and `glm-coding`; the coding plan wins the tie on `order` |
| kimi-k3 | high, main | 44 | `kimi`'s main model; kimi code serves the same one as `k3` |
| claude-haiku-4.5 | - | | `anthropic`'s lite (its own API spells the version with a hyphen) |
| claude-sonnet-6 | flash | | `anthropic`'s flash |
| deepseek-v4-pro | - |  | `deepseek`'s pro model; the owner, 2026-09-21: "deepseek pro is never used" — DeepSeek runs high, main and flash on V4.1 Flash |
| deepseek-v4.1-flash | high, main, flash | | openrouter's built-in main *and* flash: there is no non-flash DeepSeek V4.1 |
| gemini-3.1-pro-preview | - | | the concrete version behind `gemini-pro-latest`; a default for nothing, so the defaults follow Google forward |
| gemini-3.5-flash-lite | lite | | the lite model both `gemini` and `openrouter` name; what the openrouter defaults put first in lite |
| gemini-3.8-flash | - | | the concrete version behind `gemini-flash-latest`; a default for nothing, for the same reason |
| gemini-flash-latest | main, flash | | google's moving alias for its current Flash. It **moves**: the model behind it changes without this file changing |
| gemini-pro-latest | high | | google's moving alias for its current Pro. It **moves** too; `gemini`'s high |
| glm-5.3-flash | flash | | z.ai's flash, on both the standard API and the coding plan |
| gpt-5.6-terra | - | | older OpenAI flash; a default for nothing since gpt-6-luna took flash |
| gpt-6-luna | flash | | `openai`'s flash, and its lite |
| k3-256k | - | | kimi code's K3 at a wider window: a serving variant with its own row, a default for nothing |
| kimi-for-coding | - | | kimi code's moving alias for whatever it currently serves; no tier names it |
| kimi-for-coding-highspeed | flash | | kimi code's flash; no reasoning knob at all |
| kimi-k2.7-code-highspeed | flash | | `kimi`'s flash; no reasoning knob at all |
| minimax-m2.5 | - | | older MiniMax; a default for nothing |
| minimax-m2.7 | - | | the plain serving tier; minimax's flash is the -highspeed one below |
| minimax-m2.7-highspeed | flash | | `minimax`'s flash |
| minimax-m3 | high, main | | `minimax`'s main (its API spells the id `MiniMax-M3`) |
| relay-flash | flash | | relay free's flash role. Only ever listed when no other provider can take a turn |
| relay-lite | lite | | relay free's lite role |
| relay-main | high, main | | relay free's main role |


## Provider picks

A provider whose defaults differ from the shared rows above. A cell names the model this provider
puts in that class, by its name in the Models table; a blank cell follows the Models table. Only
OpenRouter needs this today: its picks are a cost-conscious subset of what it serves. The lite
cell is what the "…with openrouter" default puts first.

| provider | high | main | flash | lite |
|---|---|---|---|---|
| openrouter | glm-5.3 | glm-5.3-flash | deepseek-v4.1-flash | gemini-3.5-flash-lite |

## Levels

The reasoning level a model starts at in each class, in Relay's four words (low, medium, high,
max). A blank cell is today's rule: high is the model's top level, main the provider's own
default, flash and lite the model's lowest. A model with no reasoning knob ignores the row. Each
provider shows the level in its own vocabulary (codex says `xhigh` for max). Pre-filled on
2026-09-21 with what the rule gave then.

| name | high | main | flash | lite | notes |
|---|---|---|---|---|---|
| claude-fable-5.1 | high | low | low  |  | levels only through claude code; the anthropic API row has no knob |
| claude-opus-5-5 | xhigh | high | low  |  | claude code's levels; the API row has no knob |
| claude-sonnet-6 | max | high | low |  |  |
| gpt-6-astra | xhigh | medium | low  |  | the API's default is high; codex's own is medium |
| gpt-6-luna | max | high | low |  |  |
| kimi-k3 | max | high | low |  |  |
| kimi-k2.7-code-highspeed |  |  |  |  | no knob |
| kimi-for-coding-highspeed |  |  |  |  | no knob |
| glm-5.3 | max | high | low  |  |  |
| glm-5.3-flash | max | high | high |  |  |
| gemini-3.1-pro-preview | high | medium | low  |  | gemini has no max |
| gemini-3.8-flash | high | high| medium |  |  |
| gemini-pro-latest | high | medium | low |  | the alias's levels are the concrete row's |
| gemini-flash-latest | high | high | medium |  |  |
| minimax-m3 |  |  |  |  | no knob |
| minimax-m2.7-highspeed |  |  |  |  | no knob |
| deepseek-v4.1-flash | max | high | low |  | first-party levels are low/high/max; through openrouter also medium |
| gemini-3.5-flash-lite |  |  |  | low |  |
| relay-main | medium | medium |  |  | relay free offers low and medium only |
| relay-flash |  |  | low |  |  |
| relay-lite |  |  |  | low |  |
