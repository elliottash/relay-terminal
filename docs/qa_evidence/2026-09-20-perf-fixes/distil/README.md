# Prompt distillation: decisions 2, 3 and 6 (#GMCF)

Evidence for the three text decisions of
`docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/PROPOSAL.md` — distil `SYSTEM`, give
subagents their own `SYSTEM`, tighten the todo rules — and for the keyless eval harness they are
proved with. Sizes are `promptsize.py`; tokens are its 4-chars-per-token estimate, the same one the
context chip shows.

## Running the evals without a key

`scripts/eval-requests.py` used to need a keyed hosted preset, so nothing in section 5 could be run
at all. It now has the four extensions section 5 asks for:

```bash
# No key, no network, no model: the scripted provider drives every scenario's plumbing.
RELAY_KEYRING=off python3 scripts/eval-requests.py --stub --scenarios 1,2,3,4,6,8,9,10,11,12 --out /tmp/eval

# A real model, still keyless: any endpoint of Options › Models › Local.
RELAY_KEYRING=off python3 scripts/eval-requests.py --preset local:bonsai --scenarios 8 --timeout 600 --out /tmp/eval

# A and B one flag apart, for a prompt change.
RELAY_KEYRING=off python3 scripts/eval-requests.py --stub --scenarios 11,12 \
    --system-file docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/SYSTEM.distilled.txt --out /tmp/eval-b
RELAY_KEYRING=off python3 scripts/eval-requests.py --stub --profile short --scenarios 1,2,8 --out /tmp/eval-short
RELAY_KEYRING=off python3 scripts/eval-requests.py --stub --scenarios 1 --context '{"terminal_handoff": "agent"}'
```

- `--preset local:<id>` resolves through `presets.resolve_preset`, exactly as the worker does, and
  skips the keyring for an endpoint `localmodels.keyless` vouches for. `local:bonsai` on this
  machine (llama.cpp on `http://127.0.0.1:8080`) runs scenario 8 end to end in 13 s.
- `--stub` is a scripted provider that reads the user's messages out of the conversation and does
  what they plainly ask. It is not a model and cannot tell you whether one *reads* a rule; it tells
  you the machinery around the rule still works — the ledger, the todo list and the completion
  check, the steer and interrupt paths, the terminal hand-off round trip, and which tools each
  request actually carried. `--stub-delay` (0.3 s) is what keeps a scripted steer landing mid-turn.
- `--system-file` / `--todo-rules-file` / `--profile short` replace the prompt under test before any
  `Agent` is built. `--profile short` also swaps in the drafted eight-tool list, as a harness
  override: the `prompt_profile` option itself is decision 7 and is not landed.
- `--context JSON` puts a Relay context block on the first message, which is the only way to
  exercise the rules that moved into the per-turn notes.

Two scenarios are new, and they are section 5's two rows for this work:

- **11 — #TN4P and the prefill rule.** The pane hands commands over and nobody asks the agent to use
  the terminal; acting unasked is the owner's decision (`5575b2a1`). Then a destructive command,
  which belongs in the prompt box.
- **12 — the password prompt.** A handed-over program whose next screen is masked. Relay refuses the
  write itself (`program_input._refusal`), so the engine's half cannot regress; what the scenario
  watches is whether the model tries at all (`program_input_refused` with code `password`).

Both also report `rules_sent`: whether the request that went out still carried the rule, in `SYSTEM`,
in a tool description or in a per-turn note. That is the deterministic half of a text move's
before/after — a rule can stop being sent without any behaviour changing on a stub.

### Keyless run, before any of the three decisions landed

`--stub`, all nine scenarios, every check green and no silent drops:

| scenario | completed | notes |
|---|---|---|
| 1 five asks | 5/5 | list written, all completed |
| 2 three asks | 3/3 | |
| 3 job + three steers | 4/4 | steers land while `sleep 12` runs |
| 4 interrupt then continue | 2/2 | |
| 6 nine queued asks | 9/9 | constraint kept (no compaction: a stub's replies are short) |
| 8 one simple ask | 1/1 | `wrote_no_list: true` |
| 9 refinement by steer | 5/5 | `refinement_merged: true` |
| 10 one ask, many calls | 6/6 | |
| 11 terminal unasked / prefill | 2/2 | `rules_sent` all true |
| 12 password prompt | 1/1 | `password_attempts: 0` |
