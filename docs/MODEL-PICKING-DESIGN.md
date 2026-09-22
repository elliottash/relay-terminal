# Model picking: one name, one row, one default

Card #MDL1, 2026-09-21. A fresh review of how a model is named, listed, picked and defaulted,
after the owner found that the picker said "Codex" for `gpt-5.6-sol`, that a new pane and `/swap`
disagreed about which model comes first, and that `/swap` "seems like it doesn't work".

This document is the review's findings, the rules proposed to replace what it found, the edge
cases those rules have to survive, and the order the work lands in. Storage does not change: a
model entry is still `<preset>|<model>` everywhere it is saved (tier lists, profiles, favorites,
the exported JSON), so nothing here needs a migration.

## 1. What the review found

### 1.1 Names

There is no single function that names a model. Five exist, and three never look at the catalog:

| function | used by | yields |
|---|---|---|
| `relay::models::Entry::displayName()` | the picker, tier-list rows, the box's catalog rows | `glm-5.3 flash · z.ai (glm)` |
| `Pane::conciseModel()` | the box's Main row for guests and Relay Free, the subagent menu, the phone | **`Codex`**, `relay free`, else the id after its last `/` |
| `Pane::presetLabelOf()` | `/glm`, `/kimi`, serving tooltips | `z.ai · glm-5.3 · coding plan` — a *preset*, not a model |
| `Pane::guestDisplayName()` | every guest status line, the Tier B chip | `Claude Code`, `Codex` |
| `modelrows::roleLabel` and a second `Pane::roleLabel` | role rows, toasts, tooltips | `Main agent`, `Flash agent` — Title Case, two tables that differ |

Around them, raw ids are printed directly by `model_changed` ("Model: glm-5.3-flash"), the role
tooltips, the "this turn" row, session info, the Conversations list and the phone's event lines.

`Entry.label` itself is whatever the worker sent, lower-cased: `glm-5.3 flash`, `relay main`,
`kimi for coding highspeed`, and for OpenRouter's live listing `openai: gpt-5.6 sol`. 25 of the 31
built-in labels contain a space. The owner's example is a deliberate branch, not an accident:
`Pane::roleRowModel` sends a guest entry to `conciseModel`, which returns the preset label, although
`m_guestModel` and `currentEntryKey()` already hold `gpt-5.6-sol` at that moment. The phone copies
the box's text, so it says "Codex (main)" too.

### 1.2 One model, several spellings

| the model | first-party | guest harness | OpenRouter |
|---|---|---|---|
| GPT-5.6 Sol | `openai` `gpt-5.6-sol` | `guest:codex` `gpt-5.6-sol` | `openai/gpt-5.6-sol` |
| Kimi K3 | `kimi` `kimi-k3`, `kimi-code` **`k3`** | | `moonshotai/kimi-k3` |
| GLM-5.3 | `glm` and `glm-coding`, both `glm-5.3` | | `z-ai/glm-5.3` |
| Claude Opus 5 | `anthropic` `claude-opus-5` | `guest:claude` **`opus`** | `anthropic/claude-opus-5` |
| Claude Haiku 4.5 | `claude-haiku-4-5` (hyphen) | `haiku` | `anthropic/claude-haiku-4.5` (dot) |
| MiniMax M3 | **`MiniMax-M3`** (capitals) | | `minimax/minimax-m3` |
| Gemini 3.8 Flash | `gemini` `gemini-3.8-flash` | | `google/gemini-3.8-flash`, a built-in `openrouter` row |

The only bridges today are `presets.OPENROUTER_TWINS` (first-party id → slug) and
`presets.GUEST_MODEL_ALIASES` (`opus` → `claude-opus-5`). Nothing connects `kimi-code|k3` to
`kimi|kimi-k3`, or Codex's ids to the `openai` preset's although they are byte-identical. The cost
already shows: `presets.INTELLIGENCE` scores `k3` and `kimi-k3` separately, by hand, at the same 44.

Measured on the cached OpenRouter listing (446 ids): stripping the vendor prefix produces **no**
collisions, no id has capitals, 95 carry a `:variant` suffix (`:batch`), and five are moving
aliases written `~openai/gpt-sol-latest`.

### 1.3 Defaults

A new pane does not read the main list. It reads `provider/preset` (`Pane.h`, the `presets`
handler), and then takes **that preset's own default model**; `provider/model` is never read at
pane start. `provider/preset` is rewritten by every switch in every pane — `selectModel`,
`setMainModel`, `configurePreset`, and the worker's own `model_changed` report. So:

- a new pane starts on the provider of the last model anyone picked, on that provider's default
  model — pick `glm-5.3-flash` in pane A and pane B opens on `glm-5.3`;
- `/swap` reads `mainDefault()` / `fallback()`, ranks 1 and 2 of the *main tier list*, which is a
  third answer;
- `applyMainDefault` is the only bridge from the list to the setting, runs on five of the eleven
  paths that change the list (not on drag-reorder, remove, a level change, the first-run fill or
  the checklist), reads the *old* ranked list (`shown()`), not the main tier list, and writes a
  `provider/model` nothing reads;
- a tier-list edit is not re-sent to a running worker (`requestOptions()` carries `fallbacks`, not
  `tiers`), so `/flash` resolves on the old list until the pane reconfigures;
- a restored pane loses its model: `serializeNode` saves `model`, `initRestore` never reads it;
- the reasoning level is one global key, `agent/effort`, written by every per-pane pick;
- `/main` only clears the agent role; the `/profile` status line says it goes to main rank 1;
- `/swap` will start a guest harness at rank 1; a new pane and `applyMainDefault` both refuse to;
- an exhausted model is greyed in the picker and *absent* from the box;
- `models/effort/<key>` and `models/priority` are documented and dead.

### 1.4 `/swap`

`/swap` dispatches and the worker switches every time; what it does is not what anyone would
expect. Reproduced under Xvfb, and the same sequence is in the owner's own log
(`~/.local/share/relay/logs/relay.log`, 2026-09-21 10:49): a pane configured on `kimi-k3`, then four
`/swap`s alternating `gpt-5.6-sol` and `z-ai/glm-5.3-flashx`, never `kimi-k3` again.

1. **It knows two keys and has no memory.** The target is `current == fallback.key ? main :
   fallback` (`Pane.h`, the `swap` branch), where main and fallback are ranks 1 and 2 of the main
   tier list. A pane on any *other* model — the normal case: the owner's working models are
   `kimi|kimi-k3`, `guest:claude|opus`, `glm-coding|glm-5.3`, none of them rank 1 or 2 — is sent to
   rank 2, and every later `/swap` ping-pongs 1↔2. It can never come back to where it was. On the
   owner's machine rank 1 and 2 are `openrouter|z-ai/glm-5.3-flashx` and `guest:codex|gpt-5.6-sol`,
   which came from the first-run fill, not from him.
2. **Its confirmation is overwritten.** "Swapped to … (the fallback)" is replaced 100–300 ms later
   by the generic "Model: … · conversation kept" from `model_changed`, so `/swap` reads like a
   `/model` that picked the wrong thing.
3. **The pane's own key is sometimes one no list contains.** `currentEntryKey()` is
   `preset|<what the worker last reported>`. Claude Code is started with `opus` and reports
   `claude-opus-5`; after `/flash` the pane keeps its preset and takes the role's model from another
   one (`openrouter|glm-5.3-flash`, which exists nowhere). The owner's settings hold both
   `uses/guest:claude|opus=12` and `uses/guest:claude|claude-opus-5=8`. For `/swap` this makes both
   comparisons false; with two models of one guest at ranks 1 and 2 it would be a no-op forever.
4. **"Fallback" means two things.** `/swap`'s is rank 2 including guests; the failover chain's
   (`rememberFallback`) excludes them. On the owner's machine they are different models.
5. With no catalog yet (worker not ready) it says "every ranked subscription is exhausted".
6. There is no test of the handler, and #DC4J's `/swap` QA item was never ticked.

## 2. The rules proposed

### Rule 1 — a model has one name, and everything prints it

`relay::models::name(entry)`: lower-case, no spaces, no vendor prefix. It is for people; it is never
sent on the wire (the API still gets `MiniMax-M3`).

The worker computes it and sends it as `name` on every catalog row; the C++ side derives the same
thing for a row that has none (a custom id, an older worker). In order:

1. an explicit `name` on the `MODEL_CATALOG` row — for the few that cannot be derived:
   `kimi-code|k3` → `kimi-k3`, `claude-haiku-4-5` → `claude-haiku-4.5`, `claude-fable-5-1` →
   `claude-fable-5.1`;
2. a guest alias through `GUEST_MODEL_ALIASES`: `opus` → `claude-opus-5`;
3. otherwise the id, lower-cased, with everything up to the last `/` and a leading `~` removed.

`Entry.label` becomes this name. The prettified `label` strings in `presets.py` go; `displayName()`
stays `"<name> · <provider>"` for the places that need to say which provider. `conciseModel`,
`presetLabelOf`-as-a-model-name and the raw-id prints are replaced by one call. Harness names stay
where the sentence is about the harness ("codex is this pane's agent"), lower-case.

Tier and role words are lower-case too: `main`, `flash`, not `Main agent`.

### Rule 2 — the small picker shows a model once

`relay::models::grouped(catalog)` folds entries with the same name into a `Group{name, entries}`,
entries in **preference order**. The box, the Ctrl+Alt+M picker and `/model <name>` work on groups;
Options › Models and the tier lists keep working on entries, because that is where the order
between providers is *expressed*.

Preference order inside a group:

1. the entry's position in the tier lists (main first), i.e. what the user already ranked;
2. then the kind of access, in the order the Providers table of `backend/relay_core/model-ranking.md`
   gives — its `order` column, lower first, which the worker sends on every `presets` row as
   `kind` and `order` (protocol 13.2). That file is the owner's to edit, so the order is not
   written out again here: read the table. A row from a worker too old to send the pair falls back
   to the shape this rule used to name — a subscription or plan, then a guest harness, then the
   first-party pay-as-you-go API, then OpenRouter, then Relay Free — which is what
   `accessRank` in src/ModelCatalog.cpp still holds for exactly that case;
3. skipping an entry that is unusable (no key) or exhausted.

Picking the row picks the first live entry. The row says which provider that is
(`gpt-5.6-sol   codex  +2`), the tooltip lists the others, and in the big picker `→` opens a "via"
list beside the level list so a specific provider can be chosen. `/model gpt-5.6-sol` takes the
group's first entry; `/model gpt-5.6-sol@openrouter` names one.

The payoff is that de-duplication and failover become the same idea: when the subscription runs
out, the row stays and quietly uses the next provider. A row is greyed only when every provider in
it is spent.

### Rule 3 — the main list is the default, read directly

> A pane runs on rank 1 of the main list until you pick something else *in that pane*.

- A new pane, tab or window asks `mainDefault(catalog)` when the worker's `presets` arrive — the
  same call `/swap` makes — and takes its preset, its **model** and its **level**.
- `applyMainDefault` is deleted rather than called in six more places: there is no second copy of
  the default to keep in sync. `provider/preset` survives only as the fallback for an install with
  no tier lists yet.
- A pick in a pane is that pane's. It no longer rewrites what new panes start on; the way to change
  the default is to reorder the main list (or switch profile).
- A restored pane gets its saved preset, model and level back.
- The level comes from the list entry; `agent/effort` is only the default for an entry with none,
  and a per-pane pick does not write it.
- Every tier-list edit re-sends `tiers` to running workers.
- `/swap` is a toggle between rank 1 and *the other model this pane uses*. Off rank 1, it remembers
  where the pane is and goes to rank 1; on rank 1, it goes back to the remembered model, or to rank
  2 when there is none. So from `kimi-k3`: `/swap` → rank 1, `/swap` → `kimi-k3`. Its sentence
  survives `model_changed`, and it says which one it went to.
- The pane's key is resolved through the catalog, by **name** (Rule 1): `guest:claude` reporting
  `claude-opus-5` is the `guest:claude|opus` entry, so `/swap`, the "current" mark, recents and
  usage counts all see one model. A role's model never rewrites the pane's own key.
- The box and the picker treat an exhausted model the same way: greyed, in place.
- `models/priority` and `models/effort/*` are retired; `ranked()` stops reading `provider/preset`.

## 3. Edge cases

1. **A harness is not just another provider.** `gpt-5.6-sol` through Codex is a different agent
   (its own tools, loop and permissions) from the Relay agent on the OpenAI API. One row is still
   right, but the row must say which it will use, and the harness must never be reached *silently*
   by failover — which the worker already guarantees ("a guest is never a failover target").
2. **Local never folds into cloud.** A local `bonsai-2-27b` and OpenRouter's
   `prism-ml/ternary-bonsai-2-27b` would share a name. "Local" is a promise about where the text
   goes, so a local entry only groups with other local entries.
3. **Serving variants are different models to the person picking**: `-highspeed`, `:batch`,
   `k3-256k`, `gpt-5.6-sol-pro`. They keep their own names and their own rows. (They may share an
   intelligence score; that is a different table.)
4. **Moving aliases**: `opus`, `kimi-for-coding`, `~openai/gpt-sol-latest`. Named after what they
   point at only where the worker knows (`GUEST_MODEL_ALIASES`); otherwise the alias is the name.
   The alias table goes stale when Opus 6 ships, so once a guest reports the model it actually
   started (`m_guestModel`), that report wins for the *current* row.
5. **Relay Free's `relay-main` / `relay-flash` / `relay-lite`** are role names, not models. They
   keep those names, hyphenated, and group with nothing.
6. **Two presets, one id** (`glm` and `glm-coding` both serve `glm-5.3`): one row; the plan is
   preferred over pay-as-you-go by rule 2.2, so credit is spent last.
7. **Capitals in the id** (`MiniMax-M3`): the name is lower-case, the wire id is untouched.
8. **Reasoning levels differ by provider** (Codex says `xhigh` where others say `max`): the level
   list is the *chosen entry's*, and is redrawn when "via" changes.
9. **The remembered level** is per entry, in the tier lists; a group has no level of its own.
10. **Favorites, recents, usage counts** are stored per entry and stay that way; a group is a
    favorite when any of its entries is, and its usage is the sum.
11. **Typing a provider** ("openrouter") in the filter still finds the row, and picks *that*
    provider's entry from the group rather than the preferred one.
12. **Two different models, same stripped name** across vendors on OpenRouter: none today (0 of
    446). If one appears, both keep their vendor prefix.
13. **Hand-typed custom ids** get the derived name, so `openai/gpt-5.6-sol` added by hand folds
    into the existing row instead of making a twin.
14. **History** (Conversations, session info, exports) recorded raw ids; they are passed through the
    same `name()` on display and left alone on disk.
15. **Rank 1 is exhausted**: a new pane starts on the first live entry, exactly where `/swap` would
    go, and goes back to rank 1 by itself when the subscription resets because nothing was written.
16. **Rank 1 is a guest harness**: see the decisions below.

## 4. Decisions that are the owner's

1. **A harness at rank 1 of main: does a new pane start it?** Today no ("starting Claude Code
   because it happens to be installed is not a default"). *Recommended: yes when the owner ranked it
   first* — being installed is not a default, being put first is — provided the harness process
   starts on the first turn, not when the pane opens.
2. **The name of a Claude Code alias**: `claude-opus-5` (what it points at; recommended, matches
   the OpenAI example) or `opus` (what is typed to the CLI).
3. **`relay-main` rather than `relay main`**: follows from "always lowercase, no spaces";
   recorded here because an earlier note called the hyphenated form unrecognisable.

## 5. The two surfaces (owner, 2026-09-21)

### 5.1 The box (Alt+M): modes, then the mode's list

```
  high (gpt-6-astra)
• main (kimi-k3)              ← the mode this pane is in
  flash (glm-5.3-flash)
  local (bonsai-2-27b)        only where this machine serves one
  ─────────────────────
  gpt-6-astra   codex
▌ kimi-k3       kimi          ← this pane's model, highlighted when the box opens
  glm-5.3       z.ai +1
  ─────────────────────
  more models…   customize…
```

- A mode row is the tier first and, in parentheses, the model **this pane** would run in it: rank 1
  of that list until a model was picked in the pane for that mode, then the pick.
- Below, the models of the mode the pane is in, **in list order** (the order is the information:
  rank 1 is the default, the rest is the failover order), one row per model (Rule 2), with the
  provider it would use. Models outside the list are behind "more models…".
- **Left / Right change the mode in place**: the marker moves, the list below is redrawn, nothing
  closes. Up / Down move through the models, Enter picks, typing filters, Escape leaves everything
  as it was. Enter on a mode row switches the mode and keeps that mode's model.
- The collapsed box is the model alone on main, and `<model> · <mode>` on any other mode.
- `/high` joins `/main`, `/flash` and `/local` as a pane mode: the pane runs on the high list.

### 5.2 The dialog (Ctrl+Alt+M): pick *and* prioritize

The priority lists were under Options › Models, below the providers and the checklist — "too hard
to find". The dialog becomes the main place for both jobs, over the same storage
(`models/tier/<tier>`, written through to the current profile), so Options and the dialog can never
disagree.

- Tabs across the top, `high · main · flash · lite · local · all`; Left / Right or Tab changes tab,
  and it opens on the tab of the mode the pane is in.
- A tier tab is that list, numbered, in order. Enter uses the row in this pane. **Alt+Up / Alt+Down
  (or a drag) moves it; Delete takes it out of the list**; the level list on the right sets the
  level the entry carries in the list. Rank 1 says what it is: "new panes start here".
- Typing filters across *every* model: those already in the list first, then, under a rule,
  "not in this list" — Enter uses one, **Ctrl+Enter adds it to the list**.
- `all` is the old flat picker: favorites, recents, the sort menu.
- One row per model with a "via" column; Right on a row with several providers opens them.
- The profile is named in the header and can be switched there; "customize…" still opens Options ›
  Models for providers and keys, and that page gets a "prioritize models… (Ctrl+Alt+M)" button
  at the top.
- A footer line spells the keys. Every change is live and undoable with Ctrl+Z inside the dialog.

### 5.3 The box, second design (owner, 2026-09-21, confirmed)

```
high
  gpt-6-astra        codex
  glm-5.3            z.ai +1
main
▌ kimi-k3            kimi           ← this pane's model, highlighted on open
  glm-5.3            z.ai +1
flash
  glm-5.3-flash      z.ai
local                               only where this machine serves one
  bonsai-2-27b       spark
──────────────────────────
more models…
```

- Classes high, main, flash and local, each a header (a label: **not selectable**, skipped by Up
  and Down) and its top-ranked models. Lite is never a pane mode and never shown.
- Two per class by default. In the dialog every row of a class tab has a "show in box" checkbox
  that is a **cutoff**: checking row 4 shows the class up to rank 4; a class tab has a "show this
  class in the box" switch. Both are stored with the lists, per profile.
- Right on a row expands its class to the whole list, Left collapses it; expansion lasts while
  the box is open. Typing filters across every listed model in every class.
- **Exhausted models are not shown** (unusable ones neither). If the pane's own model is below
  the cutoff its row is shown anyway, so the highlight has a home.
- Enter on a model row switches the pane to that class and that model. Escape changes nothing.
- The collapsed box is **the model alone**, on every mode.
- "more models…" leads to the dialog; "customize…" leaves the box.

This replaces 5.1's mode rows and Left/Right paging.

### 5.4 Defaults from a ranking file (owner, 2026-09-21: "yes to all your recs")

`backend/relay_core/model-ranking.md`, shipped with the worker and parsed at startup: a
**Providers** table (`provider | kind | order`; kind is plan, harness, api, router or free) and a
**Models** table (`name | classes | score | notes`), keyed by the one name per model (Rule 1). The
score replaces `INTELLIGENCE`; the provider order replaces the group order. Request bodies,
levels and the OpenRouter twin map stay in code.

The defaults, from what can take a turn right now:

- **no providers** → Relay Free's three: `relay-main` in high and main, `relay-flash` in flash,
  `relay-lite` in lite;
- **one provider** → one model per class from it, the highest score eligible for the class;
- **two or more** → two per class by score, at most one per provider per class, the provider
  order breaking ties; a harness counts as a provider and its models rank by name like any other;
- Relay Free appears only with no keys; local endpoints fill the local class as before; the
  OpenRouter twins follow only with the key; levels as before (default / top / lowest).

### 5.5 Options › Models keeps providers, keys and profiles

The tier lists and the "models in the picker" checklist leave the page: one row, "models and
priorities… (Ctrl+Alt+M)", opens the dialog on the main tab, and the defaults buttons move into
the dialog as "fill from defaults". The checklist's setting (`models/shown`) retires; the dialog's
`all` tab shows every usable model and keeps OpenRouter's long tail behind typing.

### 5.6 Levels are the model's own, and the file says where each starts (owner, 2026-09-21)

Four rulings, all landed in the worker on 2026-09-21.

**1. The effort options are the model's.** *"i want the effort options in relay to be determined by
the model … so xhigh shows up for codex for example."* Relay's own four — low, medium, high, max —
stop being the universe. A model's levels are exactly what its provider reports: codex low/medium/
high/xhigh/max/ultra, Claude Code the same without ultra, the OpenAI API and OpenRouter low/medium/
high/xhigh, Kimi, GLM and DeepSeek low/high/max, Gemini low/medium/high, Relay Free low/medium. The
word the picker offers is the word that is sent, so `effort_labels` — the table that said what each
Relay level was *really* sent as — is retired, and `EFFORT_MAP` with it. The compatibility read is
one function, `presets.nearest_effort`: a level the model does not have becomes the weakest it does
have that is at least as much work, and its top level when there is none. That reproduces, one for
one, every answer the old mapping table gave, so a client still holding Relay's four loses nothing.

**2. A box that cannot move is greyed.** *"for no knob models, the effort box should be grayed out.
same for relay free."* Every model row of the `presets` event carries `effort_fixed`. Two different
kinds of no: a model with no levels at all, and every Relay Free model — it has two levels and
sends them, but the gateway clamps each role to its own ceiling, so a control the user could move
would only pretend.

**3. A `## Levels` table, beside the Models one.** *"can we have a similar defaults file for the
reasoning levels across model X class."* `name | high | main | flash | lite | notes`: the level a
model starts at in each class, which the owner filled in himself. It decides the level on every
default-filled list entry, the starting level of a hand-added model, and a pane's own pick in the
model box. A blank cell is the rule that was there before it (top level for high, the provider's
own default for main, the lowest for flash and lite), and a cell is mapped into the vocabulary of
whoever runs the model, because the table is keyed by name and one name can be served two ways.

**4. A `## Provider picks` table.** *"provider picks looks good, so we could add that for cerebras
for example later on."* `provider | high | main | flash | lite`: a provider whose defaults differ
from the shared Models rows, by model name, replacing its candidate for that class outright. Only
`openrouter` has a row today, and it absorbed `LITE_LIST_FIRST`, which had said the same thing in
code.

Three rulings about the lists themselves landed with them. **Lite is Relay Free by default** —
*"for lite, i am thinking to simplify that and just everybody is on relay free by default, or
openrouter if they want privacy"* — the one exception to "Relay Free only with no providers", with
the other button leading on OpenRouter's own lite pick. **A harness may serve Flash**, because a
`/flash` pane is a conversation it can own from its first turn, while every background job on Flash
and Lite skips it and takes the next entry; on a pane whose own model *is* a harness, and with
nothing left in the list, those jobs run on Relay Free's role for the tier (*"so if somebody just
has a harness, the flash chores run on relay flash?"* — *"i agree"*). And **DeepSeek runs Flash for
everything** while **gemini gains its two moving `-latest` aliases**, with the concrete versions
left a default for nothing so the defaults follow Google forward.

### 5.7 The four steps of availability (owner, 2026-09-21, verbatim)

> "the text filter isnt working -- its supposed to show all available models, not just the ones
> selected for the box picker. on this point -- i notice now that we lost functionality. there need
> to be 4 steps of model availability: 1 add provider, 2 add model as available, 3 add model to
> priority list, 4 include model in box picker. we currently only have 1, 3, 4. and its step 2 that
> determines the models available in the text filter. for branded providers, all models are
> included by default and you can uncheck them (eg i probably want to uncheck sonnet and haiku and
> gpt 5.5). but then for openrouter, you have to select specific models -- and maybe there are some
> recommended ones by default, deepseek 4.1 and gemini 3.8 flash for example."

Step 2 was lost at t:a10, which retired `models/shown` on the reading that "a model a provider
serves is a model you can pick" and wrote the one exception it still needed — an open-ended
provider's long tail — into `shown()` by hand. That exception *was* step 2, minus the ability to
say anything about it. It is a setting again, and its default is exactly what t:a10 hard-coded.

| step | what it says | where it is edited | what stores it |
| --- | --- | --- | --- |
| 1 · provider | this machine can reach this provider at all | Options › Models, **providers**: add key…, a guest login, a custom endpoint | the keyring, `models/custom_providers` |
| 2 · available | this model exists for the lists, the box and the box's filter | the Ctrl+Alt+M dialog, **all** tab, the `available` column — and the **models… (N of M available)** link under each provider row on Options › Models opens it there | `models/available` (per machine, never per profile) |
| 3 · in a list | this model is one of the five tier lists, at a rank and a level | the dialog's **high · main · flash · lite · local** tabs | `models/tier/<tier>`, and a profile's copy |
| 4 · in the box | how much of a class the Alt+M box draws at rest | the same tabs' **in box** cutoff column and **show this class in the box** | `models/box/<tier>`, `models/box_off/<tier>` |

**The default for step 2** (nothing stored, which is every install until the first un-tick):

- a **branded** provider — one whose catalog is a handful of rows it names — is available whole;
- an **open-ended** one (`Entry::openEnded`; OpenRouter's live listing of four hundred) is
  available only in its **recommended** rows: the ones the worker's own catalog names, which are
  exactly the rows carrying a `tier` (`presets.MODEL_CATALOG["openrouter"]` —
  `deepseek/deepseek-v4.1-flash`, `google/gemini-3.8-flash`, `google/gemini-3.5-flash-lite`;
  `openrouter_catalog.py` sends `tier: null` on every live row), plus anything a tier list names
  and anything typed by hand into `models/custom`.

A provider added *after* the list was written keeps the default, so step 1 still gives you step 2
for free; and a model one of the lists names is available whatever the tick says, because a rank
the user wrote down that the box would not offer is a list that lies. The dialog's tooltip says so.

**What each step feeds.** `models::shown(catalog)` is every *usable and available* entry in rank
order — the lists, the box, `/model` and the box's filter all read it. `models::allUsable` is every
usable entry: the dialog's "more from openrouter", and the fallback `/model <name>` takes, because
typing a name is asking for that model. `models::curatable` is what the `all` tab draws: available,
plus available-by-default-and-un-ticked, so an un-ticked row stays there greyed with a box to tick
again.

**The filter (the owner's first sentence).** Alt+M draws each class down to its cutoff — step 4,
what it shows *at rest*. The moment something is typed, `FilterPopup::onQueryRows` asks the pane
for the rows the *filter* should search and draws those instead, put through the same match:
`modelrows::filtered` gives every class **whole** plus one section, `other models`, holding every
available model no class lists, folded one row per model. Enter on one of those is
`pick:main|<key>` — a model in no list becomes this pane's own model, on main, at the level the
Levels rule gives it. With the filter empty the box is exactly what it was.

### 5.8 The models pane (owner, 2026-09-21: "lets build the models pane") — **built**, t:a11

The modal dialog becomes a **pane** — a `ToolPane` beside the active pane, the way Options and
Sessions are hosted — with three tabs for the first three steps of §5.7: **providers** (the
Options › Models provider rows, keys included), **available** (the `all` tab with its tick
column and the tail behind typing), **priorities** (the tier tabs: reorder, add, remove, level,
the cutoff column and the class switch; the `all`-tab picker's favorites/recents/sort live on the
available tab). Enter or "use" on a row switches the pane the manager serves — the one it was
opened from, named in its header — and a click only highlights; the filter line and the key map
carry over. **Ctrl+Shift+M** opens it (Ctrl+Alt+M goes: "not worth the extra confusion"); pressed
again it closes the pane; Escape returns focus to the pane it serves and leaves it open. One per
window; opened again from another pane it re-targets. Options › Models becomes one row that opens
it. **First run** opens with a terminal pane at the left and the models pane at the right.

**Built on 2026-09-21** (`src/ModelsPane.{h,cpp}`, `relay-modelspane`, `tests/modelspane_test.cpp`;
evidence under `docs/qa_evidence/2026-09-21-models-pane/`). What the build settled that this
section did not say:

- **The picker is a widget, not a dialog.** `relay::ModelPicker` lost its `QDialog` base, `exec()`,
  `pickModel()`, "cancel" and `reject()`; it is embedded twice over — on `all` it is the available
  tab, on a class it is the priorities tab — and the host, not the widget, says which. `onUse` and
  `onEscape` replaced `accept()`.
- **One renderer, two hosts** for providers: the tab is a `relay::SettingsPane` over
  `modelsSection(true)`, the same section Options › Models draws, with `setEmbedded(true)` taking
  its own tab row and footer away. The only difference between the two hosts is the
  "models and priorities…" row, which inside the pane would be a door to where you already are.
- **The tab keys are Alt+1 / Alt+2 / Alt+3**, and ←/→ where the class row is not in front (the
  available tab). Not Ctrl+Tab: that is the window's **Next tab** (`tab.next`) and never reaches a
  pane — the first Xvfb run pressed it three times and stayed put. ←/→ stay the class tabs.
- **`agent.model` is gone from the keymap**, not merely unbound, and `agent.modelOptions` is the
  one models key. `/model` alone opens the pane on priorities; `/models` opens it on providers.
- **First run opens on providers.** A fresh profile has no key, so there is nothing to rank or tick
  yet and step 1 is the first thing to do. The condition is `instructions/onboarded` unset *and* no
  saved layout to restore (`WindowManager::newWindowAt`).
- **The pane is saved with the layout** as `{"models": {"cwd", "tab"}}` and comes back serving the
  first terminal pane of its tab; a `SettingsWatch` listener re-reads its target, because a first
  run has no catalog at all until the worker answers `presets`.

### 5.9 The jobs tab: what each job runs on (owner, 2026-09-21) — **built**

> "for the per-job models, i think that should be reviewed and improved and made a 4th tab. take a
> careful look at it to see how to improve it for that."

**The review.** The surface was `relay::RolesDialog`, a modal opened from Options › Models' last
row, "per-job models (advanced)", and from the palette. Five things were wrong with it:

1. **It is a modal nobody finds**, and it is the only surface in Relay that answers "where does a
   summary actually go" — two clicks down, behind the word "advanced".
2. **Fifteen rows in protocol order, in Title Case.** `roles.py ACTIONS` order is main, plan mode,
   high, subagents, terminal use, flash, local, suggestions, summaries, helpers, chores, audit,
   loop check, vision, routing — neither the tier order nor any order a person has in their head —
   and the labels are Title Case sentences ("Next-command and next-prompt suggestions", "Chores:
   duplicate checks, labels, titles, note scans"), against rule 1 of this very card.
3. **What a job runs on is a grey subtitle.** `buildActionRow` printed "flash · glm-5.3-flash —
   condensing the conversation" under the row's name, only once `model_roles` had arrived, and
   never said the level. It is the most useful thing on the surface and the least visible.
4. **The override duplicates the tier lists.** A row's first box was a tier picker (default / high
   / main / flash / lite / local / "its own provider…") — which re-says what the priorities tab
   owns — and picking a *model* then took three more boxes that appeared and disappeared. The
   owner's actual ask ("i might want to pick kimi k3 for main agents and glm 5.3 flash for
   subagents") is one pick: a model.
5. **Nothing says what may not be overridden.** A background role (`roles.py BACKGROUND_ROLES`)
   silently skips a guest harness and falls through to Relay Free; the dialog offered
   `guest:claude` in its provider box all the same and then displayed a model the job would never
   use.

**The tab.** `relay::JobsTab` (`src/JobsTab.{h,cpp}`, library `relay-jobstab`,
`tests/jobstab_test.cpp`), the models pane's fourth tab, **Alt+4**. One row per job, **grouped by
the tier it follows**, in a lower-case table of its own — main (agent turns, subagents, the helper
agent), high (plan mode, /high panes), flash (terminal driving, /flash panes, summaries,
suggestions), lite (chores, the request audit, the loop check), local (/local panes), and the two
that follow no tier: images and command routing. Four columns:

    job              what it does                          runs on              override
    ─ lite ────────────────────────────────────────────    relay-lite · low
      chores         small structured judgements…          relay-lite · low     follows lite
      request audit  flags an ask that may be unaddressed  relay-lite · low     kimi-k3 · low  ×

- **runs on is the point.** It is `model_roles.roles[<role>]` (protocol 13) — the worker's own
  answer — resolved to a catalog *name* and its level, refreshed on every report. Nothing in Relay
  showed a person where a summary went before this column. A job the worker has not resolved yet
  reads "—" rather than a guess, and that is a real state: a pane whose rank 1 is a guest harness
  has not configured anything until its first turn.
- **The group heading carries the tier's own resolution** (`model_roles.tiers[<tier>]`). That is
  where **lite** is visible: the owner took the lite section off the priorities tab
  ("remove the lite section"), so there is no lite list to edit anywhere. Lite is Relay Free unless
  a provider's lite model was filled in by the defaults, the heading says which, and a per-job
  override is how one lite job moves. That is the answer to "does lite still need an editor": no.
- **An override is one pick: a model.** Not a tier — which tier a job follows is a property of the
  job, and moving the tier is what the priorities lists are for. Enter on a row drops
  `relay::FilterPopup`, the same filter list the model box uses, over the row's own override cell:
  "follows <tier>" first, then `models::grouped` one row per model name with the provider in the
  via column. A model with levels of its own then drops its level list, in the model's own words.
  Delete, or the row's ×, puts the job back on its tier.
- **A background job may not be overridden onto a guest harness.** `BACKGROUND_ROLES` are side
  calls into a conversation running somewhere else and a harness is a whole agent with its own
  transcript, so the worker skips a guest entry for them. The list does not offer one,
  `rolestore::setOverride` refuses one written any other way, and the row's tooltip says why and
  says that such a job falls through to Relay Free rather than to the pane's own model. The rule is
  derived in the GUI from the same fact it is derived from in the worker — the flash and lite
  tiers, less `flash` itself — and `tests/test_roles.py` pins the two together.
- **Storage is unchanged**, so nothing migrates: `roles/<role>/{preset,model,effort}`, the keys the
  modal wrote, now `relay::rolestore` in the tab's own header because `Pane::rolesObject` reads
  them back. A `roles/<role>/tier` the old dialog could pin is read and shown as "follows <tier>";
  the × clears it, and nothing writes it any more.
- **Live.** A write calls `ModelsPane::Target::rolesChanged` → `Pane::rolesChanged()`, the same
  `set_agent_options {roles, tiers}` a tier-list edit sends; the worker's next `model_roles` fires
  `Pane::onRolesResolved` → `RelayWindow::refreshModelsPaneFor`, and the column follows. The tab
  never predicts what a change will resolve to.
- **Keys.** ↑↓ a job (the group headings are `NoItemFlags`, so they are stepped over — the same
  ruling the box's class headers got), Enter the override pick, Delete clears, Escape back to the
  served pane.

**The old dialog is retired.** Options › Models' row is "per-job models" with a `jobs…` button and
the palette's item is "per-job models…"; both run `agent.modelRoles`, which opens the models pane
on this tab. `src/ModelSettings.*` is the keys modal alone. Its per-job tests moved to
`tests/jobstab_test.cpp`.

**Built and driven on 2026-09-21.** Evidence:
`docs/qa_evidence/2026-09-21-models-pane-jobs/` — `drive.sh`, `NOTES.md` and eleven Xvfb shots: the
column filled from a live worker, an override set and the column following it, the override
cleared, and Options' row landing on the tab. Two faults the run found were fixed before the shots
were kept: the filter list dropped at the top-left of the tree instead of at the row (it anchors on
the row's override cell now), and four fixed column widths put "runs on" and the override behind a
horizontal scrollbar whenever the pane was narrow ("what it does" takes the slack now).

## 6. Order of work

1. `/swap` and the defaults (Rule 3): bugs the owner is hitting now; no visible redesign.
2. Names (Rule 1): worker `name`, `models::name`, one call at every site in §1.1, lower-case roles.
3. Groups (Rule 2): `models::grouped`, the box, the picker's "via" list, `/model name@provider`.

Each step lands with its targeted tests and an Xvfb run under `docs/qa_evidence/`.
