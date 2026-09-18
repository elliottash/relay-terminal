---
id: W954
type: work
status: needs-qa-llm
labels: [bug]
component: [router]
milestone: desktop-alpha
workstream: routing
assignee: agent
implemented_by: Claude Opus 5 (Claude Code), 2026-09-18
rank: zzzzzw
created: '2026-09-18'
acceptance: 'A sentence whose first word carries punctuation, a capital or an apostrophe gets no "command not found" note under its ✦ echo; mistyped commands keep theirs; `tests/test_router.py` passes'
source: issues/bug_intake.txt, 2026-09-18
links: {plans: [], commits: [54f4c0c], evidence: ['docs/qa_evidence/2026-09-18-command-not-found-sentence-punctuation/'], related: [T4JV], github: null}
---
# "command not found" under a request whose first word ends in a comma

## Issue
another "command not found" bug:
✦ yeah, see if there is a clear issue to resolve. if not, lets unblock and start backfilling at full capacity
command not found: yeah,

Note from filing: reproduced against `relay_core.router.explain_invalid` — it answers True for this line and False for the same line without the comma. #T4JV's rule "a name that is not a plain lowercase word" counts `yeah,` as evidence a command was meant; sentence punctuation on the first word (`,` `.` `:` `?` `!`) should not.

## Cause

`explain_invalid` (card #T4JV) returned True for any first word that was not `isalpha()` and
`islower()`, before its sentence tests ran. That rule was meant for names like `kubectl2`,
`pip3`, `./run.sh`, but it also caught how people write: `yeah,`, `ok.`, `Yeah`, `Sure`,
`wait...`, `yeah—do`. A corpus of 72 ordinary replies and sentences (evidence folder) found 59
that printed the note and 7 that ran in the shell; after the change, 2 and 1 (see Known gaps).
Other ways in, besides the comma:

- **Capitals**: `Yeah`, `Sure`, `Continue`, `Resume`, `Sounds good!`.
- **Apostrophes**: `let's …`, `don't …`, `it's …` are a bash syntax error (a lone `'` is an
  unterminated string), and a syntax error was always explained; `don’t` (curly) failed the
  `isalpha` test.
- **Dashes**: `yeah—do it` (em dash stuck to the word), and `yeah - do it` / `ok -- do it`, where
  the lone dash counted as a flag.
- **Short one-word replies**: `ok`, `no`, `good`, `fine`, `cool`, `nope`, `yep`, `hi`, `go`,
  `stop` each sit one edit from some installed program (`od`, `nl`, `col` …), so #T4JV's typo
  test flagged them. Also `lets go` (`lets`→`let`).
- **Routing, not just the note**: `really?`, `ready?`, `hmm?`, `really?!` ran **in the shell** —
  the `?` made the first word a glob, which the router treats as undecidable-so-runnable. And a
  lone `yes` ran `yes` (y forever until Ctrl+C); a lone `nice` ran `nice`.

## Change

All in `backend/relay_core/router.py`:

- `explain_invalid` strips sentence punctuation stuck to the first word (`, . : ; ? ! …` closing
  quotes, `)`, opening curly quotes, an em/en dash and what follows it, an apostrophe inside the
  word) before judging it. Punctuation found there is itself evidence of a sentence: the line is
  quiet. The plain-word test now accepts capitals; a capitalised word that *is* a command here
  (`Docker ps`, `Ls`) is still explained as a slip, and `Gti status` / `GTI status` still go
  through the typo test.
- A line whose only quotes are apostrophes inside words, with no other shell syntax or flags
  (`don't break the build`), is quiet even though bash calls it a syntax error. `echo 'unfinished`
  is still explained. This reverses #T4JV's "a syntax error is always explained" for contractions
  only (its QA checklist item 3 no longer holds; the test now uses `echo 'unfinished`).
- A lone `-` or `--` is not counted as a flag.
- `REPLY_WORDS`: replies and sentence openers that are never a mistyped program (`ok`, `no`,
  `yeah`, `cool`, `lets`, `maybe` …) skip the typo test.
- `_resolve`: a word that is letters plus a trailing `?` (`really?`, `hmm?!`) is not a glob, so it
  is "command not found" and goes to the agent. `ls -d /us?` and `p* --version` are unchanged.
- `classify`: a lone `yes` or `nice` goes to the agent, like the lone loop builtins
  (`LONE_REPLY`). With arguments (`yes | head -3`, `nice -n 10 ls`) they are shell.

`tests/test_router.py`: the owner's line and 40 other prose lines must be quiet; 14 real slips
(`gti status`, `pyton -m x`, `ls -la | grpe x`, `./run.sh` missing, `Docker ps`, `GTI status`,
`echo 'unfinished` …) must keep the note; question words route to the agent; lone `yes` too.

## Known gaps

- `hmm; not sure` still gets `command not found: hmm`: `;` is a real shell separator and the line
  is left to the shell rules.
- `"yeah" is fine` (straight double quotes) is still explained — straight quotes are shell quotes.
- `gti, status` and `pyton,` are now quiet: a comma on the first word is read as a sentence, even
  after a typo. Judged the right trade.
- A lone `wait` still runs the `wait` builtin (harmless: it returns at once with no jobs).

## QA checklist

1. **The owner's line.** In auto mode submit
   `yeah, see if there is a clear issue to resolve. if not, lets unblock and start backfilling at full capacity`:
   it goes to the agent with the ✦ echo and **no** note under it.
2. **Quiet.** `ok.`, `Yeah`, `Sure`, `don't break the build`, `let's go`, `yeah—do it`, `ok`,
   `no`, `cool`, `Continue`: agent, no note.
3. **To the agent, not the shell.** `really?`, `ready?`, `yes`: agent (before, they ran in the
   terminal; `yes` printed y without end).
4. **Explained.** `gti status`, `pyton -m x`, `ls -la | grpe x`, `./missing.sh`, `Docker ps`,
   `echo 'unfinished`: the note is still printed.
5. **Shell.** `yes | head -3`, `nice -n 10 ls`, `ls -d /us?` still run in the terminal.
6. **Tests.** `RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest tests.test_router` (39)
   passes. The before/after table in the evidence folder can be regenerated with its `corpus.py`.
