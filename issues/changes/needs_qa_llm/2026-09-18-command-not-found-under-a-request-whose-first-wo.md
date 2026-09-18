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
links: {plans: [], commits: [54f4c0c, c09bdde], evidence: ['docs/qa_evidence/2026-09-18-command-not-found-sentence-punctuation/'], related: [T4JV], github: null}
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
that printed the note and 7 that ran in the shell; after the first round, 2 and 1 (both since
fixed, below).
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

Second round (the owner's rule "fix clear gaps; do not list them" — these three were listed as
known gaps in the first round):

- **Semicolons in a sentence** (`_semicolon_prose`). `hmm; not sure` printed
  `command not found: hmm`. A line whose only shell syntax is `;` followed by a space or the end
  (no other operators, quotes, globs or flags) is quiet when no `;`-part starts with an attempted
  command. A part is prose when its first word is a reply or function word (`REPLY_WORDS`,
  `SIGNAL_WORDS`, `SENTENCE_LEAD`: "not sure", "the other one"), when `explain_invalid` would be
  quiet about that part alone ("try again", "don't know"), or when it starts with an English-word
  command that `assist_signals` scores as a sentence ("let me think"). The line must also carry at
  least one of those words, so `xyzzy; frob` keeps its note.
  **`hmm; ls` keeps the note**: its second part starts with `ls`, a real command that would have
  run, so the line reads as a command with a stray word in front — and the note is what tells the
  user why `ls` did not run. `gti; ls`, `cd /tmp; mkae`, `ok; Docker ps`, `hmm; not sure -v` are
  explained too; `echo hi; ls` runs in the shell as before.
- **A quoted first word** (`_unquote_first_word`). `"yeah" is fine` printed
  `command not found: yeah`. A first word that is letters only, in straight single or double
  quotes, with at most sentence punctuation after the closing quote, is judged as the line without
  those quotes: `"yeah" is fine` and `'ok' then` are quiet, `"gti" status` is still a slip, and
  `"./run.sh"` or `"pip4" install x` (not letters only) keep the note. `"ls" -la` runs, as before.
  More quotes later in the line (`"yeah" is "fine"`) keep the note.
- **Lone replies that are builtins** (`LONE_REPLY`). A lone `wait` ran the builtin, which returns
  at once with no background jobs. Now `wait`, `true` and `false` alone go to the agent with
  "“wait” on its own is a reply; sent to the agent", like `yes` and `nice`; all three do nothing a
  person can see at a prompt and are everyday answers. A lone `done`, which went to the agent with
  bash's "syntax error near unexpected token `done'" under it, joins them — it is the commonest way
  to say a step is finished. `wait %1`, `wait $!`, `true && ls` stay shell, and `/shell wait` still
  runs it. Left in the shell, on purpose: `times` (prints real output, not a reply), `test`,
  `exit`, `shift` (not replies; `exit` is meant literally), and `ls`, `pwd`, `clear`, `history`,
  `jobs` (useful on their own). The other lone keywords (`fi`, `then`, `do`) still show their
  syntax error: nobody answers the agent with them.

`tests/test_router.py`: the owner's line and 40 other prose lines must be quiet; 14 real slips
(`gti status`, `pyton -m x`, `ls -la | grpe x`, `./run.sh` missing, `Docker ps`, `GTI status`,
`echo 'unfinished` …) must keep the note; question words route to the agent; lone `yes` too.
Second round: 10 semicolon sentences quiet and 9 command-shaped `;` lines explained; 4 quoted
first words quiet and 4 quoted commands unchanged; lone `wait`/`true`/`false`/`done` to the agent,
`wait %1`, `true && ls`, `ls`, `pwd`, `clear`, `history`, `jobs`, `times` in the shell.

Corpus (evidence folder, 84 prose lines, 32 slips, 12 shell lines): prose with a note, or run in
the shell — 68 and 9 at f0cd78f, 11 and 3 after the first round (`round2-before.txt`), 0 and 0
now. Every slip except the two below keeps its note in every column; every shell line stays shell.

## Left as they are

- `gti, status` and `pyton,` are quiet: a comma on the first word is read as a sentence, even
  after a typo. Judged the right trade — the comma is far more often prose than a slip, and the
  agent still gets the line.

## Follow-up, 2026-09-18: the tests asked this machine what was installed

Reported by session relay-terminal-71 and verified on a second machine: four of the cases above
passed here only because this machine happens to have docker. The router answers "is this word a
command?" from the machine's own PATH (`on_path`, `path_executables`), so `Docker ps` is a
capitalised real command where docker is installed and a nonsense word where it is not — and the
cases that assert the note is printed (`test_sentence_punctuation_is_not_a_mistyped_command`,
`test_a_semicolon_in_a_sentence_is_not_a_command` with `ok; Docker ps`) flipped. Reproduced on
`193bcfd` by running the suite with a PATH of every program on this machine except `docker` and
`dockerd`: those two cases fail. With `PATH=/nonexistent`, 20 of the 42 fail, including
`test_table_routes_correctly` and `test_ambiguous_sentences_ask_instead_of_guessing_silently`.

The rules of #T4JV and #W954 are unchanged; what changed is where the lookup comes from.
`backend/relay_core/router.py` now has one place that answers "does this program exist?", a
`Commands` source with two questions on it — `has(word)` and `names()`, the candidate list for the
one-edit typo test. `PathCommands` is the default and reads this machine's PATH exactly as before
(same per-directory cache, same `shutil.which` fallback), and `FixedCommands` answers from a table.
`classify`, `check_runnable` and `explain_invalid` take either one as their existing `path`
argument — a PATH string, as the worker and the GUI still send, or a `Commands` — so there is one
knob rather than two to disagree, and the internals pass the source down instead of a PATH string.

`tests/test_router.py` states what is installed instead of asking: a module-level `INSTALLED`
table (an ordinary Linux box, plus the programs the typo cases are slips of) and a `classify`
wrapper that applies it to every case, so no case can quietly come to depend on the machine again.
`test_shell_commands` also names the repo as its cwd, for the one case that is a path
(`./scripts/build.sh`) rather than a command.

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
6. **Semicolons.** `hmm; not sure`, `ok; let me think`, `nope; try again`: agent, no note.
   `hmm; ls`, `gti; ls`, `cd /tmp; mkae`: agent, note printed. `echo hi; ls` runs in the terminal.
7. **Quoted first word.** `"yeah" is fine`, `'ok' then`: agent, no note. `"gti" status`: note.
   `"ls" -la` runs in the terminal.
8. **Lone replies.** `wait`, `true`, `false`, `done`: agent, no note, reason "… on its own is a
   reply". `wait %1`, `true && ls`, `ls`, `pwd`, `jobs`, `times`: terminal.
9. **Tests.** `RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest tests.test_router` (42)
   passes. The before/after table in the evidence folder can be regenerated with its `corpus.py`.
10. **Machine independence** (the follow-up above). The same run passes with the PATH taken away —
    `env -i HOME=$HOME RELAY_KEYRING=off PYTHONPATH=$PWD/backend PATH=/nonexistent /usr/bin/python3 -m unittest tests.test_router`
    — and with a PATH holding nothing but programs named after the words the cases call nonsense
    (`symlink`, `resume`, `gti`, `yeah`, `hmm` …). All three runs agree. In the app nothing
    changes: on a machine with docker, `Docker ps` and `ok; Docker ps` still print their note, and
    on one without it they are still quiet — that reading of the machine is the router's job and is
    deliberately untouched.
