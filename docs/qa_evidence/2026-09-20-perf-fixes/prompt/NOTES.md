<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# The system prompt and the tool schemas, per section — #GMCF decision 2 (from #PF4K finding 6)

Owner's decision: **"do not cut rules; cut the catalogue."** No rule in `relay_core.agent.SYSTEM`,
in the Switchboard policy, in the todo rules or in the app rules was reworded here. What changed is
the skills catalogue, where the policy is cached rather than re-parsed, and where the one line that
changes while a conversation runs now sits.

## How to reproduce

```
python3 docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py          # this machine's skills
python3 docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py --json   # the numbers below
PYTHONPATH=backend RELAY_KEYRING=off python3 -m unittest tests.test_system_prompt -v
```

"before" is a clean export of `main` at `97a019fc` with the same script copied in; "after" is this
checkout. Both on spark, against the same 60 skills found in the owner's default skill directories.
Tokens are `relay_core.context.estimate_tokens` (4 chars/token), the same estimate the context chip
shows.

## Before → after, bytes

| section | no board | with a Switchboard |
| --- | --- | --- |
| SYSTEM | 5,019 → 5,019 | 5,019 → 5,019 |
| workspace line | 38 → 38 | 38 → 38 |
| **skills catalogue** | **5,253 → 4,988** | **5,253 → 4,988** |
| instructions (AGENTS.md/CLAUDE.md) | per project (0 here) | per project (0 here) |
| todo rules | 1,450 → 1,450 | 1,450 → 1,450 |
| board rules (policy + header) | — | 5,090 → 5,066 |
| app rules | 883 → 883 | 883 → 883 |
| own-session rules | 334 → 334 | 334 → 334 |
| board session note (new volatile tail) | — | 0 → 37 |
| plan-mode note (plan mode only) | 1,481 → 1,481 | 1,481 → 1,481 |
| **system prompt, build mode** | **12,977 → 12,712** | **18,067 → 17,815** |
| **system prompt, plan mode** | 14,458 → 14,193 | 19,548 → 19,296 |
| **tool schemas (22 / 32 tools)** | 15,370 (unchanged) | 28,448 (unchanged) |
| **per request, build mode** | **28,347 → 28,082** | **46,515 → 46,263** |

In tokens: 3,227 → 3,146 of prompt with no board, 4,493 → 4,415 with one; 7,064 → 6,983 and
11,593 → 11,515 including the tool schemas. **−81 and −78 tokens on every request of every step.**

The catalogue's real change is not the 265 bytes. Before, the 6 KB cap was spent on 150-character
descriptions, so **25 of 60 skills reached the model with a trigger and 35 arrived as a bare name**
under "descriptions omitted for length" — a name says nothing about when to load it. After,
**60 of 60 have a trigger line** and the section is smaller. Per-tool schema sizes are in the
script's output; the largest are `board_update_card` (2,522 B), `board_create_card` (1,964),
`ask_user` (1,799), `update_todos` (1,793) and `run_command` (1,648).

## Byte-stability (`tests/test_system_prompt.py`)

The prompt and the tool list are now pinned byte-identical across the turns of a conversation and
across worker restarts (three `PYTHONHASHSEED`s, same configuration, same sha256). Both a provider
prompt cache and llama.cpp's prefix cache key on the prefix, so a line that moves in the middle
throws away the cached work for everything below it.

Volatile items found, and what was done:

1. **What this pane holds** — `Your session: <token>. You hold: #ABCD.` sat in the Switchboard
   header, above 5 KB of policy, and changes as the pane claims cards. Moved to the very end of the
   prompt as `board_tools.session_note`; `prompt_section` is now byte-stable for the conversation.
2. **The plan-mode note** was already last and stays last; `set_mode` now changes only the tail.
3. **The skills catalogue order** followed the index, whose import directories are sorted by
   *mtime* — touching an import folder would reshuffle the lines with no change of content. The
   catalogue is sorted by name now; the index keeps its search order, which is what decides
   duplicates.
4. Checked and already stable: SYSTEM, the workspace line, the todo rules, the app rules, the
   own-session rules, the instruction blocks, and all 32 tool schemas (no sets, no clocks, no ids,
   no counters, no directory listings).

`policy_text()` re-read and re-regexed `board_policy.md` on every call; it is cached on the file's
`(mtime_ns, size)`, so an edited policy still takes effect without restarting the worker.
1,000 calls: **37.5 ms → 19.6 ms** (what is left is the `stat` that makes the edit visible).

## Not done here

`scripts/eval-requests.py` cannot run without live provider keys: `Run.__init__` takes its key from
the keyring (`keystore.lookup(preset)`) and exits when there is none, and its scenarios check files
a real model edited, so a stub cannot stand in. It was read, not run — no live keys were used for
any of the above.
