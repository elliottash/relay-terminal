---
id: 1ZNS
type: work
status: done
labels: [bug, router]
assignee: agent
implemented_by: kimi/kimi-k3
verified_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-23'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
links: {plans: [], commits: [de31a0136f2a, 2f3834c1e73f], evidence: [], related: [], github: null}
---
# Router false positive: sentence starting with a non-English command word routes to shell

## Issue
i just had this false positive as a shell command:

claude has usage rests now, so we should start tracking those in my crontab as well as add the functionality in relay

claud is a command but thats clearly agent, so see if you can see why it tripped the detector

## Done means
- `classify("claude has usage rests now, so we should start tracking those in my crontab as well as add the functionality in relay")` routes to agent with a best-guess reason, not shell.
- Real invocations stay shell: `claude --help`, `git status`, `docker ps`, `ssh filly tail the log`, flags/operators/quoting still zero the score.
- English-command first words (`make`, `find`, `look`) score exactly as before — no change to existing router tests.
- Failure looks like: the sentence above still deciding `shell`, or any existing router test changing its expectation.

## Execution Summary
Root cause: `assist_signals()` bailed at its first gate (`first not in ENGLISH_COMMANDS → score 0`), so a sentence starting with a runnable non-English program name fell through `check_runnable` (bash -n OK, `claude` on PATH) to "Runnable shell command."

Fix, landed in commit c4955c5:
- `backend/relay_core/router.py`: the args after a non-English command name now score with the same signals, but must clear `NON_ENGLISH_THRESHOLD = 4` plus a signal no invocation has (sentence punctuation, or a weight-2 article/pronoun/question word); below the bar the score is reported as 0 so nothing downstream trips on `ASSIST_THRESHOLD = 2`. New `_clears_non_english_bar()` and `_assist_why()` ("“claude” is a command; the rest reads like a sentence"), used by both the local and `_classify_remote` call sites. Flags, operators, quoting, globs still zero the score first.
- `backend/relay_core/route_assist.py`: the route-assist SYSTEM prompt no longer claims the first word is always an English word.
- `tests/test_router.py`: new `NonEnglishCommandNameTests` (4 tests).

Verified: the owner-reported sentence routes to agent with needs_assist; `claude --help`, `claude fix the tests`, `git status`, `git commit -m wip`, `docker ps`, `ssh filly tail the log`, `kubectl get pods` stay shell and never ask; all 44 pre-existing router tests unchanged and passing (48 total OK). `lang_router` uses `assist_signals` too and benefits automatically; its 21 tests pass.

## Tests
- `PYTHONPATH=backend python3 -m unittest tests.test_router` — 48 tests OK (44 pre-existing unchanged + 4 new `NonEnglishCommandNameTests`: sentences after program names route to agent and ask, real invocations stay shell and never ask, below-bar scores are exactly 0, reason wording names a command rather than an English word).
- `PYTHONPATH=backend python3 -m unittest tests.test_lang_router` — 21 tests OK (shares `assist_signals`).
- land.py byte-compiled the two landed `.py` files at commit.
