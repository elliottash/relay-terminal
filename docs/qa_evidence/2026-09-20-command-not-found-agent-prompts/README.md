# #EB4A — "command not found" under agent prompts (implementer evidence, 2026-09-20)

Owner report (#EB4A): typing prose for the agent — `another session like that, same issue?` —
printed `command not found: another` under its ✦ echo.

## What the change is

1. `backend/relay_core/router.py`, `classify()`'s forced-agent branch (composer chip on AGENT):
   the Decision shipped `explain_invalid=True` by default even for prose. It now runs the same
   `explain_invalid()` judgement as auto mode; valid text keeps `True`, so its decision dict is
   byte-for-byte what it was.
2. `src/Pane.h`, `dispatch()`'s `route == "shell"` branch: an invalid line forwarded to the agent
   passed `invalid_reason` as the note without consulting `explain_invalid` (reachable with a
   route-assist-rewritten or legacy decision). It now gates the note on the field, defaulting to
   shown for an older worker that does not send it.
3. `tests/test_router.py`: `test_agent_mode_prose_does_not_explain_itself` (the owner's line and
   same-shape variants, auto and agent modes, plus a real typo keeping its note) and new
   `explain_invalid` assertions in `test_agent_mode_reports_runnability`.

## Router level: before / after

`router-before.txt` (HEAD `d31a5da`) vs `router-after.txt` (working copy), same probes. The only
changes: in **agent** mode the three prose lines flip `explain_invalid` `True` → `False`. Auto mode
is untouched; `gti status` keeps `True` in both modes; `git status` is unchanged.

At HEAD the owner's exact line was already quiet in **auto** mode (the 2026-09-18 fixes, cards
#T4JV/#W954) — the pane that saw the bug predated them. These changes close the two latent gaps
that re-open it.

## Live check (Xvfb, isolated XDG_CONFIG_HOME)

`drive.sh` runs `build/relay` under Xvfb and submits three lines; `implementer-*.png` are the
screenshots, `implementer-*.txt` their OCR (tesseract).

1. **AUTO, owner's line** (`01`): the agent (kimi-k3) answers the prose; no `command not found`
   anywhere in the shot. OCR of `04` shows the same echo in scrollback (`+ session like that,
   same issue?`) with no note under it.
2. **AUTO, `gti status`** (`02`, visible in `04`'s scrollback lines 21–22): the echo keeps
   `command not found: gti` under it — a real typo still explains itself.
3. **AGENT mode, `same issue as before?`** (`03` shows the chip reading "Input: Agent", `04`):
   the agent answers; the only `command not found` in the file is `: gti` from check 2.

Note for re-runs: Ctrl+I cycles the composer chip auto → terminal → agent, so the script presses
it twice from auto. An earlier run of this script pressed once (Terminal mode) and its `04` shot
showed the terminal-mode fix-it flow (`asking the agent to fix it (attempt 1 of 3)`) instead —
that run's shots were overwritten by the corrected run above.

## Tests

`PYTHONPATH=backend python3 -m unittest tests.test_router` — 44 passed.
`PYTHONPATH=backend python3 -m unittest tests.test_ssh_remote tests.test_routing_thinking_skills` —
75 passed (the other modules that import `classify`).

## Not this card

`scripts/relay-build` builds the `relay` binary used above, but the full build fails on
`tests/boardmodel_test.cpp:3635` (void lambda assigned to `std::function<bool(const QString&)>
onModelPick`, `src/BoardPane.h:73`) — a compile break committed on main, already tracked as
#5BAS. Untouched here.
