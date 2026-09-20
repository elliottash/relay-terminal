# #GMCF: the Lite tier defaults to the short profile, and the short profile gets the board five

The owner's two answers of 2026-09-20 on the card's three open questions ("1 yes, 2 yes, 3 yes"):

1. **the Lite tier defaults to the short prompt profile**, and
2. **the Local tier gets the five-tool board set** — which is decision 8's sub-question, answered
   the other way round from the draft's "none until an A/B shows a 27B model can file a card".

Both are changes to what `prompt_profile: "auto"` means and to what the short profile contains
(`backend/relay_core/prompt_profiles.py`, protocol 12.12).

## What changed

* `prompt_profiles.resolve` takes a `tier`, and `SHORT_TIERS = ("lite",)` is short whatever the
  window. A Lite model's window says nothing — `gemini-3.5-flash-lite` has a million — so only the
  user's own Options › Models lists can say a model is a Lite model.
* `Agent._model_tier()` asks the resolver which of the five lists names the model **actually
  serving**, through a new read-only `RoleResolver.naming_tier(preset, model, tiers=TIERS)`.
  `turn_tier` (the failover's question) is now one line over the same scan and behaves exactly as
  before: it still looks at main / flash / local only, because a failing Lite turn steps *up* to
  Main.
* `Agent._adopt_model` — the one path `set_model`, the failover swap and `_end_failover` all take —
  rewrites `messages[0]` when, and only when, the profile changed. Before this, a swap onto the
  Local or Lite tier sent the **short tool list under the full prompt**: `tools()` is rebuilt per
  request and the prompt is not. That was a latent fault of decision 7 as landed, not of this
  change; it is fixed here because this decision makes it reachable by an ordinary failover.
* The short profile carries `SHORT_BOARD_TOOLS = board_list, board_read, board_create_card,
  board_claim, board_comment` and, in the prompt, `board_tools.prompt_section` (decision 8's tiered
  policy) and `board_tools.session_note` (the claims line, last of all) — but only when the pane has
  a Switchboard. The five keep their **full descriptions**: decision 8 moved rules *out* of
  `board_policy.md` and *into* those schemas, so a short description here would delete a rule.
  `board_update_card`, `board_move_card`, `board_import_items`, `board_signals`, `tests_check` and
  `tests_run` stay out.

## Before / after, on `local:bonsai` with its own tokenizer

`profilesize-tiers.py` in this directory. It is `assembly/profilesize.py` plus the case that did not
exist before: the pane with **no** board. The old short column was the no-board number under another
name, because the board was dropped whether or not it was attached.

```
python3 docs/qa_evidence/2026-09-20-perf-fixes/tiers/profilesize-tiers.py <tree> http://127.0.0.1:8080
```

Both columns are clean `git archive` exports — "before" is the tip `7226236a`, "after" is that same
export with only `prompt_profiles.py`, `agent.py` and `roles.py` replaced — so no other session's
uncommitted work is in the numbers. `before.json` / `after.json` are the raw output.

| one pane agent, skills + AGENTS.md + app block + keybinding catalogue | tools | prompt | tool JSON | total | cold prefill | warm |
|---|---:|---:|---:|---:|---:|---:|
| **full**, board attached (before = after) | 23 | 2,926 tok | 5,913 tok | **8,837 tok** | 9,100 tok, **10.0–10.2 s** | 0.20 s |
| **short**, board attached — *before* | 8 | 715 | 802 | **1,517** | 1,780 tok, 2.02–2.09 s | 0.20 s |
| **short**, board attached — *after* | 13 | 1,508 | 2,338 | **3,846** | 4,109 tok, **4.61–4.73 s** | 0.21 s |
| **short**, no project attached (before = after) | 8 | 713 | 802 | **1,515** | 1,780 tok, **2.26 s** | 0.20 s |

So the board five and the tiered policy cost **2,331 tokens and 2.4 s of cold prefill**, paid only
where a project is attached, and the short profile with a board is still **56 % smaller and 2.2×
faster to the first token** than the full one. A pane with no project is unchanged at 1,515 tokens.

## The A/B the owner asked for: can Bonsai file a card and claim it?

`scripts/eval-requests.py` gained **scenario 13**. It attaches a real Switchboard to the scenario's
temporary workspace and sends one message: *"Don't fix this now — put it on the Switchboard so it is
not lost, and claim the card so I can see it is yours: the pane header flickers for about a second
every time I rename a tab, on both themes."* The three checks are the three things the policy asks
for: a card exists, its `## Issue` is the user's own words, and this pane holds it (`session` on the
card is the pane token).

```
scripts/eval-requests.py --preset local:bonsai --profile short --scenarios 13 --timeout 420
scripts/eval-requests.py --preset local:bonsai --profile full  --scenarios 13 --timeout 420
scripts/eval-requests.py --stub --profile short --scenarios 13      # the plumbing, keyless
```

**Bonsai 2 27B can do it.** Three runs on the owner's own model, no key and no cost:

| run | filed a card | kept the user's words | claimed it | board calls | wall |
|---|---|---|---|---|---:|
| `--profile short`, run 1 | yes | **no** (paraphrased) | yes | `board_list`, `board_create_card`, `board_claim` | 172.6 s |
| `--profile short`, run 2 | yes | yes | yes | `board_list`, `board_create_card`, `board_claim` | **55.5 s** |
| `--profile full` | yes | yes | yes | `board_list` ×2, `board_create_card`, `board_claim`, plus two stray `list_directory` calls | 166.1 s |

`eval-scenario13-short-run1.json`, `-run2.json` and `-full.json` are the runs;
`eval-scenario13-stub-short.json` is the keyless stub, which is the plumbing test (3/3 under both
profiles). Every run reached for `board_list` before creating, which is the policy's rule 1, and
every run claimed what it filed; no run tried one of the six board tools the profile does not offer,
and `board_errors` is empty throughout.

The one failure is run 1's `## Issue`: it wrote its own wording of the report instead of the user's
sentence, which the policy's rule 2 and `board_create_card`'s own description both ask for — the
rule is *sent* under this profile, and the model followed it in the other short run and in the full
one. So: **the short profile can file and claim; verbatim capture is not yet reliable on a 27B
model**, and that is a property of the model rather than of the tool list. It is worth saying on the
card, because "the request is the user's words" is the thing the Switchboard is for.

The wall-clock column is not a fair latency comparison — other sessions were using the same
llama.cpp slot throughout — but the shape is consistent with the prefill table: the full profile
spent its first ten seconds on prefill and then wandered (two `board_list`s and two
`list_directory`s before it committed), while short run 2 went straight through in 55 s.

## Tests

* `tests/test_prompt_profiles.py` — `test_auto_is_short_on_the_lite_tier_whatever_its_window`,
  `test_a_pane_on_the_lite_list_sends_the_short_profile`,
  `test_a_failover_across_tiers_switches_the_profile_and_switches_it_back`,
  `test_a_swap_inside_one_tier_leaves_the_prompt_object_alone`,
  `test_a_board_adds_five_tools_and_the_policy_and_nothing_else`,
  `test_the_board_five_keep_the_rules_decision_8_moved_into_them`, and the two existing ones
  retargeted (`…_sends_eight_tools_when_there_is_no_board`, the override test).
* `tests/test_system_prompt.py::StabilityTests::test_a_failover_across_tiers_re_prefills_once_and_comes_back_byte_for_byte`
  — the byte-identity test across a tier failover, on the full fixture: the prompt and the tool list
  change once on the way down and come back byte for byte on the way up.

All of them were run on a clean export of exactly the tree that landed, not on the shared checkout.

The GUI row was corrected with them: Options › Agent's "Prompt profile" hint said "the short prompt
and 8 tools on a local or small-window model" and now names the Lite tier and stops promising a
fixed tool count (`src/RelayWindow.h`).

## One failure that is not this change

`tests/test_system_prompt.py::SizeTests::test_the_board_policy_block_stays_tiered` fails in the
shared checkout (3,893 B against a 3,072 B budget). It is another session's **uncommitted**
`backend/relay_core/board_policy.md` (5,567 B in the tree against 4,512 B at the tip): the same test
passes on a clean export of the tip and fails on that export as soon as the working copy of
`board_policy.md` is dropped into it. Nothing here touches that file.
