# Model ranking

The two tables below are what Relay's **default** priority lists are built from: the model box's
"fill from defaults", Options › Models' two `defaults` buttons, and the lists a fresh install
starts with. They are data, not code — edit them, restart the worker, press `defaults`, and the
lists change. Nothing else in the repo holds a model's score or a provider's rank any more
(`presets.INTELLIGENCE` and the old `_MAIN_GROUP_ORDER` are views over this file).

What they do **not** hold: request bodies, reasoning levels, endpoints and the OpenRouter twin
map, which stay in `presets.py` — they are what a provider's API takes, not an opinion about which
model is better.

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

Relay Free is never listed once any other provider can take a turn. The `local` class is the model
servers on this machine, in the order they were saved, and is not ranked here. A guest harness is
counted as a provider and its models are scored by **name** like anyone else's — `gpt-6-astra`
through codex scores what `gpt-6-astra` through the OpenAI API scores — but it is only ever
offered for `high` and `main`: a flash or lite call is a side call to a running conversation, and
a harness is a whole agent of its own that cannot be handed one (`roles.GUEST_TIERS`).

## How to edit

- **Providers** — one row per preset id the worker knows, plus the two guest harnesses.
  `kind` is one of `plan` (a subscription), `harness` (a guest CLI), `api` (a first-party
  pay-as-you-go endpoint), `router` (OpenRouter) or `free` (Relay's own hosted allowance).
  `order` is the tie-break, **lower first**; the bands (10s plan, 20s harness, 30s api, 40s
  router, 90s free) are the preference order of design rule 2.2, so two providers serving the
  same model at the same score pick the one that costs least.
- **Models** — one row per **name** (design rule 1: one model has one name, lower-case, no spaces,
  no vendor prefix, whoever serves it). `classes` is any of `high`, `main`, `flash`, `lite`,
  comma-separated, or `-` for a model that is a default for nothing. `score` is the owner's
  intelligence number (seeded 2026-09-20 from the Artificial Analysis Intelligence Index v4.3.2,
  each model at its highest reasoning level); leave it blank for a model nobody has scored, which
  sorts last rather than zero. `notes` is free text and is read by nobody.
- Rows are kept sorted by score descending, then by name. Nothing depends on the order — the
  parser reads the whole table — but a diff is easier to read when the file stays sorted.
- `python3 -m unittest tests.test_model_ranking` checks this file: every model the worker's
  catalog names has a row, every provider row is a preset the worker has, no duplicate names, no
  class word outside the four.
- A serving variant (`-highspeed`, `k3-256k`, `:batch`) is a different model to the person picking
  one, so it gets its own row (design section 3.3). It may share a score; it rarely shares a class.

## Providers

| provider | kind | order |
|---|---|---|
| kimi-code | plan | 10 |
| glm-coding | plan | 11 |
| minimax | plan | 12 |
| guest:claude | harness | 20 |
| guest:codex | harness | 21 |
| kimi | api | 30 |
| glm | api | 31 |
| openai | api | 32 |
| anthropic | api | 33 |
| gemini | api | 34 |
| openrouter | router | 40 |
| relay-free | free | 90 |

## Models

| name | classes | score | notes |
|---|---|---|---|
| claude-fable-5.1 | - | 53 | claude code's `fable`, and `anthropic`'s `claude-fable-5-1`. No built-in tier names it, so it is a default for nothing until `high, main` is written here |
| gpt-6-astra | high, main | 53 | `openai`'s main model, and the one codex lists first |
| claude-opus-5 | high, main | 51 | `anthropic`'s main model; claude code's `opus` is the same model |
| gpt-5.6-sol | - | 47 | codex lists it and the OpenAI API serves it; no tier names it |
| glm-5.3 | high, main | 45 | one row for `glm` and `glm-coding`; the coding plan wins the tie on `order` |
| kimi-k3 | high, main | 44 | `kimi`'s main model; kimi code serves the same one as `k3` |
| claude-haiku-4.5 | lite | | `anthropic`'s lite (its own API spells the version with a hyphen) |
| claude-sonnet-5 | flash | | `anthropic`'s flash |
| deepseek-v4.1-flash | high, main, flash | | openrouter's built-in main *and* flash: there is no non-flash DeepSeek V4.1 |
| gemini-3.1-pro-preview | high, main | | `gemini`'s main |
| gemini-3.5-flash-lite | lite | | the lite model both `gemini` and `openrouter` name; what the openrouter defaults put first in lite |
| gemini-3.8-flash | flash, lite | | `gemini`'s flash, and the lite that every provider without one of its own borrows through openrouter |
| glm-5.3-flash | flash | | z.ai's flash, on both the standard API and the coding plan |
| gpt-5.6-luna | lite | | `openai`'s lite |
| gpt-5.6-terra | flash | | `openai`'s flash |
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
