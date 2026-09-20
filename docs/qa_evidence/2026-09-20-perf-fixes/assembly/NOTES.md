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
and `::test_attaching_a_switchboard_only_appends` (both fail before the change),
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
