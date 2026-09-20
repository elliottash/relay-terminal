# #GMCF decision 4: assembly order, a fixed tool list, the plan note in the turn

What was measured, on this machine (DGX Spark, `local:bonsai` on 127.0.0.1:8080, llama.cpp
`timings`), with `modeswitch.py` in this directory. "Before" is a clean export of `main` at
`b9e873ef`; "after" is that same export with only `backend/relay_core/agent.py` and
`backend/relay_core/planning.py` replaced, so no other implementer's uncommitted work is in the
numbers.

```
python3 docs/qa_evidence/2026-09-20-perf-fixes/assembly/modeswitch.py \
    --repo <export> --bench http://127.0.0.1:8080 --json <out>.json
```

One pane agent with skills, `AGENTS.md`, the app block, the `src/Keymap.h` keybinding catalogue and
a Switchboard; a two-turn conversation; the build-mode request is sent first (`cache_prompt: true`),
then the plan-mode one, so `timings.prompt_n` is exactly what the switch re-prefilled.

| | before | after |
|---|---|---|
| system prompt, build / plan | 17,888 B / 19,369 B | 17,888 B / 17,888 B |
| tool JSON, build / plan | 38,410 B (33 tools) / 27,428 B (31) | 38,864 B (34) / 38,864 B (34) |
| a build → plan switch changes the prompt | yes | **no** |
| a build → plan switch changes the tool list | yes | **no** |
| tokens re-prefilled by the switch | **11,714** | **363** (the turn's plan-mode note) |
| time to first token on the switch | **12.9 s** | **0.56 s** |

`cache_n` after the fix is 14,347: the whole prefix was reused and only the turn's own context
block was new. The before run's second and third switches came back in 0.19 s — the server's
host-side `--cache-ram` had kept the other mode's prefix — which is the proposal's point in 4.1
that the host cache is a help nobody can rely on: the first switch paid 12.9 s.

The +454 B of tools is `write_plan`, now offered in both modes (it is refused outside plan mode in
`Agent._prepare`, with the message it always had). The plan-mode note is 1,559 B / 363 tokens that
used to be in the system prompt of every request of a plan turn and is now in the turn's first
user message once, so a multi-step plan turn sends it fewer times, not more.

Tests: `tests/test_system_prompt.py::StabilityTests::test_switching_mode_changes_neither_the_prompt_nor_the_tool_list`
and `::test_attaching_a_switchboard_changes_nothing_above_the_workspace_line` (both fail before
the change),
`tests/test_sessions.py::PlanModeTests` updated to the new behaviour.

# #GMCF decision 7: the short prompt profile

`profilesize.py` in this directory builds one pane agent (skills, `AGENTS.md`, the app block, the
`src/Keymap.h` catalogue, a Switchboard), takes its prompt and tool list under each profile, counts
the tokens with the Local tier's own tokenizer (llama.cpp `/tokenize` on `local:bonsai`) and times a
cold and a warm prefill of each. `profilesize.json` is the raw output.

```
python3 docs/qa_evidence/2026-09-20-perf-fixes/assembly/profilesize.py . http://127.0.0.1:8080
```

| | full | short |
|---|---|---|
| system prompt | 13,133 B / 3,047 tok | 2,878 B / **681 tok** |
| tool JSON | 31,403 B / 8,000 tok (35 tools) | 3,140 B / **802 tok** (8 tools) |
| before the first user word | **11,047 tokens** | **1,483 tokens** (−87 %) |
| cold prefill (server `timings`) | 11,309 tokens, **13.3–13.7 s** | 1,745 tokens, **2.1–2.3 s** |
| warm (same prefix again) | 0.19–0.21 s | 0.20–0.22 s |

The full profile is 11,047 tokens here rather than the proposal's 14,544 because decisions 1, 2 and
8 landed in between (`set_keybinding` slimmed, `SYSTEM` distilled, the board policy tiered). The
wall-clock figures move around by tens of seconds while another session is using the same llama.cpp
slot; `prompt_ms` from the server's own `timings` is the number to read.

Evals (`scripts/eval-requests.py`, keyless stub provider, no model): `eval-stub-full-summary.json`
is scenarios 1, 2, 3, 8, 9, 10, 11, 12 on the full profile — every check true — and
`eval-stub-short-summary.json` is 1, 2, 8, 10, 11, 12 with `--profile short`, which now passes the
landed `prompt_profile: "short"` agent option rather than the draft files. Every check true there
too, including scenario 11 (#TN4P: the terminal is used unasked, `rm -rf` goes to the prompt box)
and scenario 12 (nothing is ever typed into a password prompt). Scenarios 3 and 9 are todo-list
scenarios and do not apply to a profile with no todo tool.

The short profile carries **no Switchboard tools and no board policy** — decision 8's sub-question,
answered "none until an A/B shows a 27B model can file a card". `prompt_profile: "full"` in
Options › Agent is the override, and it takes effect on the next request.

Tests: `tests/test_prompt_profiles.py` (11 tests: which profile `auto` picks, every hard rule of
`SYSTEM` present in `SYSTEM_SHORT`, the byte budgets, the eight tools, and the setting overriding
`auto` live).

# #GMCF decision 9: three tool groups loaded on demand

`groupsize.py` in this directory builds the same pane agent and sizes its request as the model loads
each group, with the Local tier's tokenizer; `groupsize.json` is the raw output.

```
python3 docs/qa_evidence/2026-09-20-perf-fixes/assembly/groupsize.py . http://127.0.0.1:8080
```

| | tools | prompt | tool JSON | total |
|---|---|---|---|---|
| everything sent, as before | 35 | 3,043 tok | 8,000 tok | **11,043 tok** |
| deferred, nothing loaded | 23 | 2,920 tok | 5,913 tok | **8,833 tok** (−20 %) |
| after `load_tools(app)` | 32 | 2,920 | 7,234 | 10,154 |
| … and `own_session` | 34 | 2,920 | 7,596 | 10,516 |
| … and `tests` | 36 | 2,920 | 8,136 | 11,056 |

A turn that needs none of the three — most turns — sends 2,210 tokens less. A turn that needs all
three pays 13 tokens more than before (the `load_tools` schema) plus one round trip per group. The
prompt saving is the app and own-session rules going with their tools, replaced by the one line
naming them: 3,043 → 2,920 tokens.

Loading only ever appends: `tests/test_tool_groups.py::LoadTests::test_a_load_only_appends_to_the_tool_list`
compares the JSON of the list before a load with the same prefix after it, byte for byte, and the
system prompt is unchanged by a load at all. The refusal a model gets for calling a name whose group
it has not loaded names the group and the tool to call:
`tests/test_tool_groups.py::LoadTests::test_calling_a_deferred_tool_first_is_refused_with_the_group_named`,
and `ConversationTests` runs the whole round trip — refusal, load, successful call — through a
scripted provider, checking that the request *after* the load is the first to carry the schema and
that its tool list still starts with the one before it.

Deferral is off on the Local tier and under the short profile (every load re-prefills there,
proposal 4.1) and off for a group this pane has nothing wired up for.

# Re-measured at the tip, and the Local tier's own A/B

Decisions 1, 2, 3, 8 and 9 all landed while this work was going on, so the numbers above are each
true of the tree they were taken on and none of them is the whole saving. `profilesize-tip.json` is
the same `profilesize.py` run again at `f86266da`, where every decision of the proposal is in:

| | full (with the groups deferred) | short |
|---|---|---|
| system prompt | 12,546 B / 2,921 tok | 3,028 B / 710 tok |
| tool JSON | 23,197 B / 5,913 tok (23 tools) | 3,140 B / 802 tok (8 tools) |
| before the first user word | **8,834 tokens** | **1,512 tokens** (−83 %) |
| cold prefill | 9,096 tokens, **9.5–9.8 s** | 1,774 tokens, **1.94–1.96 s** |
| warm | 0.19–0.21 s | 0.19–0.20 s |

Against the proposal's starting point — 14,544 tokens and 18.5 s for a pane with a board — the full
profile is now 8,834 tokens and 9.6 s, and the short one 1,512 tokens and 1.95 s.

**The Local tier's A/B (section 5), on the owner's own model, no key and no cost.** Three scenarios
that the short profile has to keep passing — a five-ask prompt, an edit-and-rename, and a single
simple ask — run against `local:bonsai` on both profiles:

```
scripts/eval-requests.py --preset local:bonsai --profile short --scenarios 1,2,8
scripts/eval-requests.py --preset local:bonsai --profile full  --scenarios 1,2,8
```

| Scenario | short | full |
|---|---|---|
| 1 (five files in one prompt) | 5/5, 43.0 s | 5/5, 27.0 s |
| 2 (fix, rename, append) | 3/3, 50.0 s | 3/3, 49.5 s |
| 8 (one simple ask, no list) | 1/1, 11.5 s | 1/1, 16.0 s |

`eval-local-bonsai-short-summary.json` and `eval-local-bonsai-full-summary.json` are the runs. Both
profiles complete every check, so the short profile loses nothing the deterministic checks can see;
the wall-clock column is not a fair latency comparison because other sessions were using the same
llama.cpp slot throughout (the prefill numbers in the table above, taken from the server's own
`timings`, are). What the full run does show is `no_list_nudges: 1` in all three scenarios — the
27B model never writes a todo list and has to be nudged — which is the evidence behind the short
profile dropping the todo tool.

What this A/B does **not** answer is decision 8's sub-question, "can this model file a card": no
scenario here asks for one. The Local tier keeps no board tools until someone writes that scenario.
