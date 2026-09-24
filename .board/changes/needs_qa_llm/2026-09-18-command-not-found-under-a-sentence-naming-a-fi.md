---
id: N3WC
type: work
status: needs-qa-llm
labels: [bug]
component: [router]
milestone: desktop-alpha
workstream: routing
assignee: agent
implemented_by: Oz (Warp), 2026-09-18
rank: zzzzzx
created: '2026-09-18'
acceptance: 'A sentence that names a file gets no "command not found" note under its ✦ echo when a word before the last carries sentence punctuation; mistyped commands with file operands keep theirs; `tests/test_router.py` passes'
source: owner report in session, 2026-09-18
links: {plans: [], commits: [], evidence: ['docs/qa_evidence/2026-09-18-command-not-found-sentence-naming-a-file/'], related: [T4JV, W954, 8G17], github: null}
---
# "command not found" under a sentence that names a file

## Issue
im still seeing this bug where it says "command not found" for agent prompts. i have reported it
about 10 times, can you find the cause

Screenshot with the report: the ✦ echo of
`new card: the introduction shouldnt show any in that` (wrapped) with `command not found: new`
under it.

## Cause

The submitted line (recovered from the session file, turn `da613e73`; the echo showed only the
first ~53 of its 159 chars):

```
new card: the introductory agent instructions file was still showing when i didnt have any. it shouldnt show any in that case. just init the default RELAY.MD
```

The note's gate is `explain_invalid` → `_meant_as_command` (#T4JV, #W954, #8G17): the line reads
as writing, then each sentence is asked whether it was meant as a command. For a first word that
is a plain English word, a line longer than three words is a sentence *unless it names a file*
(`router.py`'s `len(words) > 3 and not files`), so that `gti stauts report.txt` keeps its note.
`RELAY.MD` made that file test true — `_names_a_file` counts any word containing a dot — so the
word-count guard stood aside, and the one-edit typo test fired on the first word: `new` is one
edit from `net` and `znew`, both on this machine's PATH. Hence `command not found: new` under a
21-word, three-sentence request.

The live log shows the mechanism directly (`live-log-excerpt.txt`): while the line was still
being typed the preview routes said "Reads like a request", and the note appeared at the
keystroke that completed `RELAY.MD` — the moment the sentence "named a file". The guard's file
carve-out was written for short slips with operands; nothing in it asked whether the words
around the file are a sentence. Every earlier card in this family fixed the line that was
reported; this one fixes the reading that let a sentence reach the typo test at all.

## Change

`backend/relay_core/router.py`, `_meant_as_command`: sentence punctuation stuck to a word before
the last — an alphabetic word ending in `, : ; . ? ! …` ("card:", "any.", "case.") — is a
sentence break, and the segment is writing, whatever it names. This is the existing
last-word rule ("three things:", "two questions,"; #8G17) applied to the words before it, with
`.` included in that position: a full stop that ends a word mid-line ends a sentence, while one
on the *last* word ("gti stauts.") still does not. A dotted filename with a stray trailing mark
(`config.yaml.`) is not alphabetic, so it cannot trip the rule; `..` and version strings cannot
either. Real slips are untouched: `gti stauts report.txt`, `gti stauts.`, `gti status`, `cs ..`
all keep the note.

`tests/test_router.py`: `test_a_sentence_naming_a_file_is_not_a_mistyped_command` — the owner's
verbatim line (with `net` passed as a known command, since the tests' INSTALLED table has no
program one edit from "new" and this machine has two), three more prose lines that name
`relay.md` / `scripts/train.py`, and the four keep-the-note cases above.

## QA checklist

1. **The owner's line.** In auto mode submit
   `new card: the introductory agent instructions file was still showing when i didnt have any. it shouldnt show any in that case. just init the default RELAY.MD`
   on a machine with `net` or `znew` installed (this one has both): it goes to the agent with
   the ✦ echo and **no** note under it.
2. **Quiet.** `gti notes: the commit should explain why relay.md changed, and nothing else` and
   `gti log shows relay.md was committed. can you check`: agent, no note.
3. **Explained.** `gti stauts report.txt`, `gti stauts.`, `gti status`, `cs ..`: the note is
   still printed.
4. **Tests.** `RELAY_KEYRING=off PYTHONPATH=backend python3 -m unittest tests.test_router` (43)
   passes, and passes with the PATH taken away
   (`env -i HOME=$HOME RELAY_KEYRING=off PYTHONPATH=$PWD/backend PATH=/nonexistent /usr/bin/python3 -m unittest tests.test_router`).
5. **Corpus.** `repro.py` in the evidence folder prints the eight cases; `before.txt` (4 wrong
   of 8) and `after.txt` (0 wrong of 8) were captured against the tree with and without the
   change. `./scripts/test.sh` shows no router failures; its one failure
   (`test_remote_wire.py`: unclassified `board_init_request`/`board_state`) comes from other
   uncommitted board work in the tree and fails identically without this change.
