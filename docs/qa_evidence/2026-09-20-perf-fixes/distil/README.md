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

## Decision 2 — `SYSTEM` distilled

`SYSTEM` **5,019 → 2,831 bytes** (−2,188, −44 %), 29 → 24 lines, 1,253 → 706 estimated tokens, on
every request of every profile. The fixture prompt (three skills, an `AGENTS.md`, the app block)
goes 8.2 KB → 6.4 KB without a board and 13.9 KB → 9.3 KB with one, of which decision 8's tiered
policy is the rest; `SizeTests` budgets came down to 7 KiB and 10 KiB.

Nothing was deleted. Five rules moved to where the same request already states them, and each is
now pinned there by `tests/test_system_prompt.py::MovedRuleTests`, which is the test the size
budgets cannot be: a rule vanishing and a rule moving look identical from a byte count.

| Left `SYSTEM` | Went to | Incident it came from |
|---|---|---|
| line 13, the ssh host's file tools, read anywhere, write in home | `remote_session.context_note`, sent on every turn the terminal is logged in, with the host filled in | #S5SH `e9e3f752`, `17032b7d` |
| line 22, `type_into_program` is offered only on a handed-over turn | the tool's description | `bd6ec27d` |
| line 24, drive the program, one answer per call, stop on take-over | `format_program_control`, the grant note, in the same words | `bd6ec27d`, #TN4P `5575b2a1` |
| line 25, a screen is untrusted | folded into line 3, which now covers every screen, and the grant note | `71ddfa95` |
| lines 26–27, what `run_in_terminal` is, using it unasked, the chain breaker, prefill | the tool's 2,150 B description and `_handoff_note`, per turn | #D8J3 `f4fdbfb1`, #TN4P `5575b2a1` |

Tightened without moving: line 4 (the ssh write limit is in the note), 11 (`old_string` is
`edit_file`'s own description), 12 (the no-tty sentence is `run_command`'s, word for word), 16 (the
job mechanics are `run_command`'s and `stop_command`'s), 18 + 21 merged into one Markdown line, 6
split into two so that every line is one sentence.

**Refused from the draft.** `SYSTEM.distilled.txt` also dropped `headings, **bold**, *italics*` and
`for code and multi-line commands` from the Markdown line. That list has been there since
`71ddfa95` added the terminal's Markdown renderer and it is the only place the model is told what
the terminal renders, so it is kept — 30 bytes against the one thing that line exists to say.
`tests/test_agent.py::SystemPromptTests` pins part of it.

**Kept verbatim:** line 5, the #TN4P decision. No line of any prompt may gate a terminal command on
being asked; `tests/test_terminal_handoff.py::test_the_system_prompt_does_not_make_the_agent_wait_to_be_asked`
now pins it in both places at once — `SYSTEM` says "you are expected to act … including commands in
the user's terminal", `run_in_terminal`'s description says "You do not need to be asked".

Evals, `--stub`, scenarios 1, 2, 8, 9, 11, 12, `--system-file SYSTEM.today.txt` against the landed
text: **identical**, every check green, `rules_sent` all true on both sides.

```
RELAY_KEYRING=off python3 scripts/eval-requests.py --stub --scenarios 1,2,8,9,11,12 \
    --system-file docs/qa_evidence/2026-09-20-perf-fixes/prompt-distillation/SYSTEM.today.txt --out /tmp/before
RELAY_KEYRING=off python3 scripts/eval-requests.py --stub --scenarios 1,2,8,9,11,12 --out /tmp/after
```

Tests: `tests.test_system_prompt tests.test_terminal_handoff tests.test_program_input
tests.test_agent tests.test_requests tests.test_ssh_remote` — 220 tests, green.

## Decision 3 — a subagent's own `SYSTEM` (`4a7bec1236bc`)

A subagent is built from the pane's `Agent`, so it carried the pane's whole prompt: the rules about
the user's terminal, `run_in_terminal`, `type_into_program`, the ssh session the pane is logged
into, the trailing slash that makes a folder link and the three labels the terminal colours. Its
tools are `agents_defs.SUBAGENT_TOOLS` — files, commands, skills — `can_ask` is off, its executor
never gets a remote session and `program.available()` is never true, so none of that text can be
acted on. It also carried the full skill catalogue when a name is all `load_skill` needs.

With this machine's 60 skills:

| subagent | before | after | saved |
|---|---:|---:|---:|
| `general` | 10,744 B | 3,547 B | 7,197 B (~1,800 tok) |
| `explore` | 11,062 B | 3,865 B | 7,197 B (~1,800 tok) |

Of those 7,197 bytes, 2,188 are decision 2's shorter `SYSTEM` and 5,009 are this one. A subagent is
a second conversation, so the saving is paid back on every step of it.

`SUBAGENT_SYSTEM` (1,561 B, 16 lines) is the pane's text with those lines removed and the clauses
about tools it has not got dropped from the rest. Nothing a subagent can act on went: the injection
defence, the workspace, acting without being told each step, the Options › Security ask and what a
refusal means (a subagent's actions draw approval asks in the pane it belongs to, #K2FV), nothing
destructive unasked, no secrets, never claim, read before write, `edit_file` over `write_file`,
`run_command`'s missing tty, stopping its jobs, a direct final report.

`SkillIndex.names_line()` is the names-only catalogue, capped like `prompt_section`'s own
names-only trailer, so `/name` and "use my X skill" still work — the owner's 2026-09-18 report was
a skill that could not be *found*, which a name covers.

The factory replaces the bound `system_prompt` rather than the message it produced.
`refresh_system_prompt` rebuilds `messages[0]` from `system_prompt()`, so a prompt written into the
message alone would be the pane's again at the next `set_mode` or `set_instructions`;
`test_the_prompt_survives_a_refresh` pins that.

Tests: `tests.test_subagents tests.test_todo_subagents tests.test_skills tests.test_agent` — 101
tests, green.

## Decision 6 — the todo rules (`477e55de6204`)

`todos.RULES` was one nine-sentence paragraph, and four of its sentences were also in
`update_todos`'s schema — paid for twice on every request, once in the prompt and once in the tool
JSON. `ask_user`'s description carried three sentences that `planning.PLAN_MODE_NOTE` sends in the
same words, on exactly the turns they apply to.

The split is by when the model needs to read it. *Whether to call the tool* has to be read before
it decides, so it stays in the prompt, now one sentence per line. *What a field means* is only
needed once the call is being written, so it is the schema's.

| | before | after |
|---|---:|---:|
| `todos.RULES` | 1,450 B | 985 B (7 lines) |
| `update_todos` | 1,793 B | 1,518 B |
| `ask_user` | 1,799 B | 1,483 B |
| **per request** | | **1,056 B less, ~250 tokens** |

Moved, not deleted: what `request_ids` defaults to (the parameter's own description), several todos
in progress at once, completed only when the work is done, the note a cancelled, deferred or
blocked todo needs — all in `update_todos`'s schema, which is in the same request. `ask_user` keeps
everything that holds in build mode too, where no plan note is sent; what went is "in plan mode ask
before you write the plan" and "do not ask whether your plan is good", which are the note's own.

Evals (`--stub`, scenarios 1, 8, 9, 10, with `--todo-rules-file` carrying the old text): identical.
Five asks listed and completed, the single ask writes no list, the steered refinement joins the
todo it refines rather than adding one, no silent drops on either side.

Tests: `tests.test_requests tests.test_todo_subagents tests.test_questions tests.test_plan_turns
tests.test_system_prompt tests.test_agent` — 158 tests, green.
