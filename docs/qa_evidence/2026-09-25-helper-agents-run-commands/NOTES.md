# Helper agents can run commands (#NXN0)

## What was asked

Owner, 2026-09-25: "feature: helper agents needs to be able to run commands", pointing at card
#ANRZ's thread, where the Board agent twice had to say "I can't run commands from Discuss" and
took the owner's word that pytest was installed instead of running `pytest --version`.

## The change

Consoles (Board, Options, Actions, Sessions) already ran commands on ordinary turns —
`ConsoleScope` fences nothing. The fence was the **card turn** (Discuss / Plan / Refine): two
halves refused `run_command`, `command_output` and `stop_command`.

- `Agent.CARD_BLOCKED` (`backend/relay_core/agent.py`): now `READONLY_BLOCKED - {"run_command"}` —
  the job tools come off with it. The writers stay refused, sentence unchanged but for what it
  names (Run, not the retired Execute wording).
- `CardScope` (`backend/relay_core/board_tools.py`): new `CARD_COMMAND_TOOLS`; `allows` keeps them
  beside the read tools; `refusal` says commands are available to inspect and verify, writing is
  Run's job.
- Briefs: `board_discuss_brief.md` and `board_plan_brief.md` now tell the agent it may run
  commands to read and verify, never to change code or files.
- Docs: `docs/AGENT-SESSIONS-PROTOCOL.md` (19.10 table gains a commands row; call-refusal
  paragraph; 36.4), `docs/ARCHITECTURE.md` (33.3 paragraph).

Read-only survey turns (`readonly: true`) still refuse `run_command` — a survey writes nothing by
design and needs no shell; unchanged here deliberately.

## Verification

- `python3 -m pytest tests/test_queue.py tests/test_board_protocol.py tests/test_board_tools.py
  tests/test_agent.py tests/test_jobs.py` → **630 passed, 1 failed**, the one failure being
  `InitTests::test_the_owners_first_card_asks_first_and_lands_on_a_yes`, which fails on a clean
  export of `main` too (`git archive main` + run, 2026-09-25) — filed separately as #42G1.
- The stale assertions this work replaced: `test_queue.py` expected the refusal sentence to name
  "Execute" (the sentence has said "Run's job" since the #CTRN work renamed it — that test was
  already failing on `main`); both card-turn tests now also prove `run_command` prepares on a card
  turn while `write_file` is refused, and that `command_output` / `stop_command` pass the scope as
  far as the jobs table.
- `tests/test_board_tools.py`: `CardScope("plan", …).allows` now true for the three command
  tools, still false for `write_file`.
