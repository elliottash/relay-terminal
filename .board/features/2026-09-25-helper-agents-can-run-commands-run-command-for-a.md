---
id: NXN0
type: work
status: needs-verification
labels: [feature, agent-ui, switchboard]
assignee: agent
implemented_by: kimi/k3
session: 89a58a8a-55cf-4032-8356-62f9b362f830
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: owner, terminal pane, 2026-09-25
links: {plans: [], commits: [36bb9d5bfa8e, '0b63208b6534', 736fa030453d], evidence: [docs/qa_evidence/2026-09-25-helper-agents-run-commands/], related: [FEJQ, AGNT, ANRZ], github: null}
---
# Helper agents can run commands (run_command for agent consoles)

## Issue
Helper agents — the agent consoles behind the Switchboard/Board, Options, Actions and Sessions — have no way to run a shell command. On card #ANRZ the Board agent twice had to say "That's Run work, not Discuss — I can't run commands from here" and "I can't run commands from Discuss to check pytest myself", taking the owner's confirmation on trust instead of verifying. Helper agents should get a command-running capability comparable to the pane agent's `run_command`, with appropriate scoping.

> feature: helper agents needs to be able to run commands … check the agent thread here for an example of why: #ANRZ
> — elliott · [session:74e8fa8794ea49cd8002041c4116b285](relay://session/74e8fa8794ea49cd8002041c4116b285) · 2026-09-25

## Execution Summary
**What the owner asked:** helper agents need to run commands, exemplified by #ANRZ where the Board agent in Discuss could not run `pytest --version` and took the owner's word.

**What was true:** consoles (Board, Options, Actions, Sessions) already ran commands on ordinary turns — `ConsoleScope` fences nothing. The fence was the **card turn** (Discuss / Plan / Refine): `Agent.CARD_BLOCKED` and `CardScope` both refused `run_command`, `command_output`, `stop_command`.

**The change** (owner 2026-09-25, card #NXN0): a card turn now verifies by running.

- `Agent.CARD_BLOCKED` = `READONLY_BLOCKED - {"run_command"}` — the job tools come off with it; writers stay refused, sentence names Run.
- `board_tools.CardScope`: new `CARD_COMMAND_TOOLS`; `allows` keeps them beside the read tools; `refusal` says commands inspect and verify, writing is Run's job.
- `board_discuss_brief.md` / `board_plan_brief.md` say the same in the agent's own instructions.
- Docs: protocol 19.10 table gains a commands row, the call-refusal paragraph, 36.4; ARCHITECTURE 33.3.
- Read-only survey turns still refuse `run_command` (a survey writes nothing by design; unchanged deliberately).

**Commits:** `36bb9d5b` (initial land), `0b63208b` + repairs `7c92c065`, `6f5c8d2a` (see Incident below), `736fa030` (final re-land).

**Incident, for the record:** the first two landings (`36bb9d5b`, `0b63208b`) used `--confirm` digests that no longer matched after `main` moved mid-landing, and swept in other sessions' uncommitted hunks — #EE42's `board_tools.py` board-links work (importing a module that was never committed, which briefly broke `main` twice), #R660's ARCHITECTURE section, and prose sections of #3B1B / #PBZ4 / #R660 / #P2W8 in `docs/AGENT-SESSIONS-PROTOCOL.md`. Both were taken back with `land.py repair` and the feature re-landed selected. `main`'s final state was verified on a clean export: imports resolve, 629 backend tests pass. Two consequences for other sessions: (1) their `docs/AGENT-SESSIONS-PROTOCOL.md` prose sections landed early (with `736fa030`) — their code commits will show less diff; (2) #EE42's board-links **code** did not land and is still uncommitted in the tree, untouched.

## Done means
- A Board-card conversation in Discuss or Plan mode can call `run_command` (and `command_output` / `stop_command`) and the command actually runs — e.g. `pytest --version` answers the #ANRZ question without taking the owner's word.
- `write_file` / `edit_file` on a card turn are still refused at call time, in a sentence that names Run and the card id.
- Ordinary console turns and read-only survey turns behave exactly as before (commands allowed / refused respectively).
- The offered tool list on a card turn is byte-identical to a console's (nothing re-prefilled on mode change).
- Backend tests covering the change pass on a clean export of `main`.

## Tests
Run on a clean export of `main` (`736fa030`), 2026-09-25 — `git archive | tar -x`, then pytest:

- `tests/test_board_protocol.py::CardScopeAgentTests::test_a_plan_turn_is_refused_the_writers_when_it_calls_them_and_told_about_execute` — updated: `run_command` / `command_output` / `stop_command` pass the scope on a card turn (the job tenders as far as the jobs table); `write_file` / `edit_file` refused with "Run's job" + card id. **pass**
- `tests/test_queue.py::ConsoleFieldTests::test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why` — updated (also fixes its stale "Execute" assertion, already failing on `main`); proves `run_command` prepares on a card turn and the offered list still contains both `run_command` and `write_file`. **pass**
- `tests/test_board_tools.py::CardTurnScopeTests::test_a_card_turn_does_not_move_a_consoles_tool_list` — updated: `CardScope("plan", …).allows` true for the three command tools, false for `write_file`. **pass**
- Full set `tests/test_queue.py tests/test_board_protocol.py tests/test_board_tools.py tests/test_agent.py tests/test_jobs.py`: **629 passed, 2 failed** — both pre-existing on `a7cc1fcc` (before this card), filed as #42G1 and #6AZW.
- Working-tree full run before landing: 630 passed, 1 failed (the pre-existing InitTests one).

Not covered (noted for the verifier): no end-to-end GUI test of a Board Discuss conversation actually rendering a command run — the worker-side path is unit-tested; the console surface is a `Pane`, which already renders `run_command` for ordinary console turns.
