---
id: 9DYD
type: work
status: needs-qa-llm
labels: [feature]
component: [router]
milestone: 0.1-preview
workstream: routing
assignee: implemented by Claude Opus 5 (1M context), 2026-09-17
rank: u6
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist below and records it under `docs/qa_evidence/`
source: owner report 2026-09-17 — "'look' is the classic example of a command i mentioned that would also show up in natural language. double check there arent others like that -- try to deploy a subagent to make an inclusive list." Follows the glob fix in commit 9a915b7.
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Router: an inclusive list of commands that are also English words

## Problem

`look at some of my other letters in ~/admin/Advisees.*.docx for my writing style` ran as a shell
command: `look` is installed (bsdextrautils) and the glob suppressed the ambiguity check. The glob
half is fixed; `ENGLISH_COMMANDS` was still hand-written and had gaps, so the same failure was
waiting behind `just`, `from`, `route`, `at`, `prove`, `tidy`, `dump`, `restore`, `truncate`,
`shred`, `cancel`, `eject`, `browse`, `bind`, `prune`, `transform`, `zip`, `accept`, `reject`,
`disable`, `spell`, `talk`, `pass`, `sample`, `resume`, `batch`, `bundle`, `jot`, `wipe`, `strip`,
`screen`, `tar`, `suspend` and `unzip`.

## Behavior as implemented

- `ENGLISH_COMMANDS` is now derived from the machine, not from memory: the 4327 executables on PATH,
  `compgen -b`, `compgen -k` and `busybox --list`, intersected with
  `/usr/share/dict/{american,british}-english` (241 hits), then classified by hand into "could start
  an English request" (added), "English but never sentence-initial" (left out, with a recorded
  reason) and "already present". 196 → 232 words. `base` (no program of that name anywhere) was
  removed and a duplicate `look` collapsed. 20 of the additions are not installed here and come from
  documentation (macOS/BSD, CUPS, busybox, `just`, Bundler, HTML Tidy, zsh's `where`), because Relay
  also runs on other machines.
- `SIGNAL_WORDS` grew 84 → 200: indefinite pronouns and reflexives at weight 2; the rest of the
  common prepositions, auxiliaries, modals, adverbs and quantifiers at weight 1. One weight-1 word is
  still not enough to ask, so `shutdown now`, `make all` and `watch more` stay in the shell.
- `BARE_WORD_ODD` gained `find patch strip truncate shred prove tidy spell sum transform`.
  `LITERAL_TEXT` gained `banner`.
- The rules themselves are unchanged: a word only matters when it resolves as a command on this
  machine, flags, quotes, operators and expansions still zero the score, and an ambiguous input sets
  `needs_assist` so `route_assist` decides rather than the local guess being silently wrong.
- `tests/test_router.py` gained `ROUTING_TABLE`: 99 inputs (54 English sentences, 45 real commands
  using the same words), plus tests that every ambiguous sentence sets `needs_assist`, that no plain
  command asks the model, and a 3248-input sweep of every English-word command in ordinary argument
  shapes.

## Implementer check (not a QA verdict)

`docs/qa_evidence/2026-09-17-router-english-commands/` has the derivation, the classification of all
241 candidates with a reason for each exclusion, and per-case before/after output.

| | before (HEAD f908d16) | after |
|---|---|---|
| sentences routed to the agent | 25 / 54 | 54 / 54 |
| commands kept in the shell | 45 / 45 | 45 / 45 |
| overall | 70 / 99 | 99 / 99 |

False-positive sweeps: 232 words × 14 ordinary argument shapes = 0 would ask the model; 232 words ×
144 weight-1 signal words as the only argument = 5680 hits, all of them from the pre-existing
`BARE_WORD_ODD`/`go`-subcommand rules and none from the new signal words.

`./scripts/test.sh`: 382 tests, OK (376 before; no keyring, no network).

## QA checklist

1. In Auto mode, type the owner's sentence
   `look at some of my other letters in ~/admin/Advisees.*.docx for my writing style`: the label says
   AGENT and the assist is asked, not run in the shell.
2. Type each of `just run the tests again`, `from the logs tell me what failed`,
   `route the request through the proxy`, `tidy up the imports in this module`,
   `truncate the log file to zero`, `zip up the build output`: each goes to the agent. Note that
   `just`, `tidy` and a few others only become ambiguous if installed — check at least the ones this
   machine has (`route`, `truncate`, `zip`, `from`, `prune`, `screen`, `strip`, `tar`, `unzip`).
3. Ordinary shell use must be untouched: `ls`, `ls -la`, `git status`, `make -j`, `make test`,
   `find . -name x`, `grep -rn TODO src`, `watch -n1 ls`, `time make`, `sort -u words.txt`,
   `head -n 20 log.txt`, `open .`, `zip -r out.zip dir`, `unzip archive.zip`, `truncate -s 0 app.log`
   all run in the terminal with no "checking" label.
4. Borderline pairs, same word both ways: `split the file by lines` vs `split file.txt`;
   `time the build` vs `time make`; `watch the logs` vs `watch -n1 ls`; `find the config` vs
   `find . -type f`. The sentence goes to the agent, the command to the shell.
5. Nothing may be decided silently when it is genuinely ambiguous: for the sentences in step 2,
   confirm `needs_assist` is set (the composer shows "checking…") rather than the local guess being
   used without a model check.
6. Check the exclusions for disagreement: `classification.tsv` lists 116 words left out with a
   reason each. `strings` and `defaults` are flagged as close calls. Say whether any exclusion looks
   wrong for how the owner actually types.
7. Re-run `./scripts/test.sh` and confirm 382 tests pass with no keyring or network access.
