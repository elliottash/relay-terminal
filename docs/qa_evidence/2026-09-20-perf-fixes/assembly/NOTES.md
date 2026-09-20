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
