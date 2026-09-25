# #K54A — system prompt and tool list back under their budgets

Measured with `measure.py` (this folder: the `tests/test_system_prompt.py` fixture, per section
and per tool), run from the repository root: `python3 docs/qa_evidence/2026-09-25-k54a-prompt-budget/measure.py`.

| measure | budget | HEAD 2f3834c1 | after |
|---|---|---|---|
| boardless prompt | 6,656 | 7,011 | 6,335 |
| pane tool list | 17,920 (and 18,432) | 18,894 | 17,879 |
| prompt with a board | 9,728 | 10,451 | 9,538 |
| board policy block | 3,072 | 3,302 | 3,065 |

## Where the growth was

- **698 B of the prompt was not the code at all.** The fixture read the global Board at
  `~/.config/relay/switchboard`, so the "boardless prompt" carried the owner's two pinned global
  memories (#26BN, #2XZV). The card's 7,153 B was this machine's number. `PromptFixture` and the
  restart subprocess now point `RELAY_GLOBAL_SWITCHBOARD` at an empty folder in the temp dir.
  With that isolation the prompt proper is 6,335 B, so no prompt text was trimmed.
- **Tool list, +2.5 KB since 2026-09-20:** `scratch_dir`/`scratch_release` (#DVV2),
  the four `media_*` tools (#2GV0), `land_try` (#76QW), `run_command`'s `memory_max` (#WBDX),
  `app_user_memory`. These were trimmed by 1,015 B, cutting only sentences that repeat a parameter's own
  description, the schema's min/max, or a SYSTEM line:
  - `run_command`: the `memory_max` sentence (size syntax, local-only and the scope are the parameter's
    description) → one sentence on when to use it and what happens over the limit; the
    wide-crawl refusal sentence shortened (the refusal itself names the fix); "(default 30, at most 1800)"
    (the parameter says both).
  - `scratch_dir`: "Never invent temp paths" (the opening says it); per-class lifetimes (the
    `lifetime` parameter's job); `class` param text restating the enum.
  - `scratch_release`: the `ref` explanation moved into `ref`'s description; the `promote_to`
    mechanics (its parameter says them).
  - `app_user_memory`: "New records omit key and use base_hash ''" (both parameters say it);
    "list, get and suggestions do not write. Writes use the same validation…" (informational).
  - `edit_file`: "Preferred over write_file for editing a file you have read" (SYSTEM's
    edit_file/write_file rule, on every turn already).
  - `command_output`: "at most 1800" (the schema's maximum).
- **Policy block, +280 B:** #MJ76 (3512773d) added the `assignee`/`owner`/`parent`/
  `duplicate_of` lines to rule 5. `board_update_card` and `board_move_card` already describe
  each field where it is set, so they left again (policy v10). `.board/POLICY.md` regenerated;
  it was also stale at HEAD from the `issues/` → `.board/` move.

## Tests

`python3 -m pytest tests/test_system_prompt.py` — 33 passed.
Neighbours: `tests/test_board.py tests/test_board_tools.py tests/test_tools.py tests/test_jobs.py
tests/test_scratch_ledger.py tests/test_user_memory_tools.py tests/test_guest_memory.py
tests/test_prompt_profiles.py tests/test_guest_board_bridge.py` — 711 passed, 3 failed. All three
fail identically on a clean `git archive` of HEAD run with `PYTHONPATH` set to the export
(so the export does not import this checkout's `relay_core`), so this change did not cause them:
`test_prompt_profiles…test_a_board_adds_five_tools…` (short board tool list 11,599 > 11,264;
growth is in the five board schemas, filed separately), `test_guest_board_bridge…parity`
(`land_try`/`scratch_*` missing from the guest catalog), and `test_guest_memory…bound_agent` (Mock).
