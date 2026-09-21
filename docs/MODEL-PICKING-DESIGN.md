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
2. then the kind of access: a subscription or plan, then a guest harness, then the first-party
   pay-as-you-go API, then OpenRouter, then Relay Free;
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

### 5.4 Defaults from a ranking file (proposal)

See the card's thread, 2026-09-21: the defaults come from a reviewable file in the repo, and the
tier lists and the checklist leave Options › Models for the dialog.

## 6. Order of work

1. `/swap` and the defaults (Rule 3): bugs the owner is hitting now; no visible redesign.
2. Names (Rule 1): worker `name`, `models::name`, one call at every site in §1.1, lower-case roles.
3. Groups (Rule 2): `models::grouped`, the box, the picker's "via" list, `/model name@provider`.

Each step lands with its targeted tests and an Xvfb run under `docs/qa_evidence/`.
