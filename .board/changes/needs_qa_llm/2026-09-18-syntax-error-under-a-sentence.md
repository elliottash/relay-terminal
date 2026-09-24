---
id: 8G17
type: work
status: needs-qa-llm
labels: [bug]
component: [router]
milestone: desktop-alpha
workstream: routing
assignee: agent
implemented_by: Claude Fable 5.1 (Claude Code), 2026-09-18
rank: zzzzzx
created: '2026-09-18'
acceptance: 'A sentence that bash calls a syntax error (parentheses, quotation marks, a code span, several lines) gets no note under its ✦ echo and no bash complaint in the route line; a broken command keeps both; `tests/test_router.py` passes'
source: owner, in a Claude Code session, 2026-09-18
links: {plans: [], commits: [6c8f0c2], evidence: ['docs/qa_evidence/2026-09-18-syntax-error-under-a-sentence/'], related: [W954, T4JV], github: null}
---
# "syntax error" under a sentence with a parenthesis in it

## Issue
auto detect error:

✦ check my provider (glm), am i out of credits or rate limited
syntax error: syntax error near unexpected token `('

that seems like an obvious one, how did it get through? think harder and try to make a better detector.

## Cause

How it got through: `explain_invalid` (cards #T4JV, #W954) only ever questioned one kind of note,
"command not found". For anything else it answered `return bool(reason)`, under the comment "a
syntax error is about a command either way". That is false: bash calls a good deal of ordinary
writing a syntax error, and a parenthesis is the commonest case. #W954 had already met this once —
an apostrophe ("don't") is an unterminated string to bash — and carved out contractions, then
semicolons, then a quoted first word, each as its own exemption in front of the blanket rule. The
rule itself stayed, so every other piece of punctuation still printed the note.

The line was routed correctly (agent). The defect is the note under the echo, and the same text in
the route line under the composer while typing ("AGENT · Not a runnable command (syntax error …)").

Measured on a corpus of ordinary sentences (evidence folder): 77 of 85 printed a note before.
Beside parentheses: quotation marks (`the "fast" tier is slow` → "command not found: the"),
Markdown code spans (`run \`make test\` and fix what fails` → "command not found: run"), any
message of more than one line, and sentences that start with a function word one edit from a
program (`the other one` → `tee`, `not sure` → `nl`).

## Change

`backend/relay_core/router.py`. The exemptions are replaced by one reading of the line, used for
"syntax error" and "command not found" alike. The burden of proof is now the same for both: the
note needs evidence that a command was meant.

- `_as_writing()` reads the line the way a reader does and takes out the punctuation of writing:
  list markers, emoticons, apostrophes inside words and plural possessives, `code spans`,
  quotation marks that hug their words, parentheses that hug their words, arrows (`->`, `=>`), and
  splits it into sentences at line breaks, a semicolon before a space, and ` & `. What writing
  never uses is still shell outright: `| & < > $ \ { } =`, globs, an unbalanced quote, `f()`,
  `$(`, `hmm;ls`.
- `_meant_as_command()` judges each sentence: flags; a first word that runs here (unless the rest
  is a sentence — `test the provider and tell me what it says`, `if not, lets unblock`, `yes
  (both)` — while `echo …`, `for i in …`, `ls`, `cd` stay commands); a name that is not a word; a
  word one slip from a command. A function word first (`the`, `not`, `am`) is a sentence, a bare
  number first is nobody's program, `and/or` is not a path unless it exists, `three things:` is a
  sentence.
- A line that needed shell-looking punctuation removed must also carry a word only a sentence has
  (`_is_writing`: a reply or function word, a contraction, sentence punctuation), so `frob (glm)`
  and `xyzzy; frob` keep their note.
- `classify`: when the note is withheld the reason is "Reads like a request · sent to the agent",
  so the route line no longer quotes bash at a sentence either.
- `_unquote_first_word` and `_semicolon_prose` are gone (subsumed). `_contractions_only` stays:
  card #S5SH's `_remote_prose` uses it.

One expectation from #W954 is reversed on purpose: `"yeah" is "fine"` was listed as keeping its
note (a second pair of quotes fell outside that card's exemption). It is a sentence; it is quiet now.

## Evidence

`docs/qa_evidence/2026-09-18-syntax-error-under-a-sentence/corpus.py`, with `implementer-before.txt`
(router at 790fc76, before this change) and `implementer-after.txt`:

| | lines | note before | note after |
|---|---|---|---|
| sentences (tuned on) | 35 | 29 | 0 |
| sentences (held out) | 50 | 48 | 0 |
| broken commands (tuned on) | 29 | 29 | 29 |
| broken commands (held out) | 33 | 33 | 33 |
| valid commands, must stay in the shell | 7 | 7 shell | 7 shell |

The held-out lines were written after the detector. Their first run left 5 of 50 sentences with a
note (a leading number, "error:" with no function word, ` & `, `->`, `yes (both)`); those are
fixed, which makes them tuned-on lines too — a QA session should bring its own sentences. #W954's
corpus is unchanged except for `"yeah" is "fine"`. Through the real worker (`route` message): the
owner's line returns `explain_invalid: false`, `echo (hello)` returns `true`. The GUI is untouched:
`Pane.h` already prints the note only when `explain_invalid` is set.

## Left for the owner

- **Terminal mode.** With the composer fixed to the terminal, this sentence is still handed to the
  agent as a command to fix (`startFix`), not flagged "reads like a request". That hint
  (`agent_signal`) is documented as high-confidence only and decides whether a submission is
  refused; how sure the router must be before refusing in a mode the user chose is a product call.
- **A line that starts with a code span** (`\`make test\` fails on main`) is valid bash and runs in
  the shell: it is a command substitution in command position. Routing of valid lines was not
  touched here.

## QA checklist

1. **The owner's line.** In auto mode submit
   `check my provider (glm), am i out of credits or rate limited`: agent, ✦ echo, **no** note under
   it; while typing, the route line reads "AGENT · Reads like a request · sent to the agent".
2. **Quiet.** `the build is broken (again)`, `the "fast" tier is slow`, `run \`make test\` and fix
   what fails`, `the other one`, `thanks :)`, and a three-line message with a numbered list: agent,
   no note.
3. **Explained.** `echo 'unfinished`, `echo (hello)`, `(cd /tmp && ls`, `for i in 1 2; do echo $i`,
   `gti (status)`, `frob (glm)`, `sudo apt install (foo)`: agent, note printed.
4. **Shell.** `(cd /tmp && ls)`, `echo 'the (glm) provider'`, `echo \`date\`` run in the terminal.
5. **Your own sentences.** Twenty sentences of your own with parentheses, quotes, code spans or line
   breaks, and ten broken commands: report any sentence with a note and any broken command without.
6. **Tests.** `RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest tests.test_router` (42) passes.
