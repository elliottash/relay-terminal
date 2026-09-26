---
id: 5NDQ
type: work
status: needs-verification
labels: [bug, agent, tokens, context]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 5a98ce15-658b-4291-a7ed-d9cf438c15a7
rank: zzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [dc0303aeb78f, 295a4a8f6dc3], evidence: [docs/qa_evidence/2026-09-25-cleared-reads-working-set/], related: [VQXA, 234Z], github: null}
---
# Cleared tool results make agents re-read the same file ranges: 38 sed reads of Pane.h in one turn

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** Session `3f4a20ad` (#234Z): 98 of 225 tool results were replaced by `{"cleared": true, … command_output(job_id=…) reads it again}`. The agent then re-read what it had lost: **38** `sed -n` reads of `src/Pane.h`, several of the same range (`1690,1800` ×3, `13400,13480` ×3, `10930,11015` ×2), each one another step at ~100k prompt tokens. Clearing saved context and cost steps; steps are what the turn paid for (193 requests × ~100k tokens).

**How to address.**
1. Clear by relevance, not only age: keep results for files the agent has since *edited* or is about to edit (the checkpoint already knows which files changed), and for ranges read more than once.
2. When a result is cleared, leave a one-line index in its place: `src/Pane.h:1690-1800, sha 3a6b…, unchanged since` — so the agent knows whether a re-read would show anything new.
3. Make re-reads of an unchanged range cheap: `read_file` on a range whose file hash matches a cleared result can answer from the kept copy (restoring that one result) instead of the model reconstructing it with shell `sed`.
4. Measure: `reread_same_range` count per turn in the worker log.

**Done means.** On a replay of a long single-file turn, same-range re-reads drop by at least half with no rise in context size; the cleared placeholder names file, range and hash.

## Done means
Clearing stale tool results spares the working set instead of stubbing it: a read_file result for a file the agent has since edited, and the newest read of a range requested more than once, survive a clear, and every cleared read_file stub names path, line range, the file's sha256 at read time, and whether the file was edited after the read. A re-read of a cleared, unchanged range restores that one result instead of the model reconstructing it with shell `sed`, and each such repeat is counted by a `reread_same_range` event in the session log. Failure looks like session 3f4a20ad: a cleared read followed by `sed -n` re-reads of the same range, or a stub that names no path, range or hash — both caught by the tests in `tests/test_tool_output_bounds.py`.

## Plan
**Goal.** Clearing tool results between compactions (`context.clear_stale_tool_results`, card #0C0V) must not destroy the agent's working set. In session 3f4a20ad it stubbed 98 results, the agent re-read `src/Pane.h` 38 times with `sed -n`, and the re-reads cost more steps (~100k prompt tokens each) than the clearing saved context. Relevance joins recency as a clearing signal, the stub becomes a one-line index the agent can act on without re-reading, and repeats are measured.

**Findings.**
- `backend/relay_core/context.py`: `clear_stale_tool_results(messages, keep_groups=CLEAR_KEEP_GROUPS, over_chars=CLEAR_OVER_CHARS)` stubs every tool result older than the last 3 assistant tool-call groups once the old results total ≥ 40,000 chars (`CLEAR_OVER_CHARS`) and each result is ≥ `CLEAR_MIN_CHARS`. Recency is the only signal; nothing knows which files the agent is editing.
- `_stub(tool, arguments, content)` keeps `tool`, a 160-char `args` snippet, `chars` and a `note` from `_reread()`; for read_file the note is `read_file(path=…) reads it again` — no range, no file hash, no statement of whether the file changed since. But the real result already carries what is needed: `Tools._read_result` returns `sha256` and `total_lines`, and `from_line`/`to_line` are in the arguments.
- `backend/relay_core/agent.py:2358` `_clear_stale_tool_results(turn_id)` calls it on `self.messages` between steps (agent.py:2771), emits `tool_results_cleared` {turn_id, count, chars, keep_groups} and calls `expect_prefix_change("tool_results_cleared")`. The `clear_tool_results` option (False disables clearing) is validated, reported and settable; tests live in `tests/test_tool_output_bounds.py` (`ClearingUnit`, `ClearingInTheAgent`, `ClearingOption`).
- Writes are visible in the same transcript: `write_file`/`edit_file` calls carry `args.path` and their results carry the after-write `sha256`. Detecting "the agent has since edited this file" is a message scan, no checkpoint needed.

**Steps.**
1. `context.py` — enrich the read_file stub. In `_stub`/`_reread`, parse the read_file result JSON for `sha256` and the arguments for `from_line`/`to_line`; the stub gains `path`, `range` and `sha256`, and its note states the exact re-read call *with* range plus the change status: `unchanged since this read (no write to it afterwards)` or `the file was edited after this read`. The change status comes from scanning `messages` for a later write_file/edit_file on that path and comparing its result sha256. The stub stays one line (existing test asserts this).
2. `context.py` — relevance pass before the age pass. `clear_stale_tool_results` protects (a) the newest read_file result per path that a later write_file/edit_file in the transcript touches, and (b) the newest result of any `(path, from_line, to_line)` range that appears ≥ 2 times among all read_file calls. Protected results are neither counted nor stubbed; at most `CLEAR_PROTECT_MAX` (6, new constant) are protected, newest first, so a file-heavy turn still clears its oldest churn. When nothing is left over `CLEAR_OVER_CHARS`, no clear fires (existing all-or-none rule).
3. `agent.py` `_clear_stale_tool_results` — nothing new to pass (the scan reads the same `self.messages`); add the protected count to the `tool_results_cleared` event so over-protection is observable in the log.
4. Restore-on-reread. `_clear_stale_tool_results` saves the texts it stubbed for read_file into a bounded side store on the agent (`self._cleared_reads`, keyed by `(path, sha256)`, LRU, ≤ 2 MiB). When a read_file result arrives whose path's current sha256 matches a kept copy and the requested range is inside the kept range, put the kept text back in place of the old stub message and return a short result for the new call: `path`, `range`, `sha256`, `restored: true`, and a note pointing at the restored block above. One step, one small result, no `sed` reconstruction — and the content sits in the transcript exactly once.
5. Metric. While recording read_file calls, detect a `(path, from_line, to_line)` repeat of an earlier read_file call in the same turn (cleared or not) and emit `{"event": "reread_same_range", "turn_id": …, "path": …, "range": …}`; it lands in the session log as agent events do. The count per turn is then readable from the log the way #VQXA proposes for `reminder_injected`.
6. Tests — extend `tests/test_tool_output_bounds.py` (see Verify): protection of edited-file reads and repeated ranges, the enriched stub, the protect cap, restore-on-reread with both a matching and a changed file, and the metric event.

**Risks.**
- Over-protection can stall clearing in a file-heavy turn and push cost into compaction instead. Mitigated by `CLEAR_PROTECT_MAX` and the new `protected` count in the event; if the field shows stalling, the cap is the knob.
- The short `restored: true` result asks the model to look back at the restored block. If that reads badly in practice, the fallback is to return the full content from the kept copy (identical to a disk read) and skip the un-stub — one flag flip, no restructuring. Default is the short result: it is the card's suggestion 3.
- The relevance scan and the stub's sha256 parse are O(messages) per clear; clears already rebuild the cache prefix, so the scan is noise. Cleared read texts are in memory only, capped, never written to disk.

**Verify.** `python3 -m pytest tests/test_tool_output_bounds.py tests/test_system_prompt.py -q` — existing clearing and prefix-change tests still pass. New tests: a `ClearingUnit` case where a read of a later-written path and a twice-read range survive the clear while an unrelated old read is stubbed with `path`/`range`/`sha256` and the changed-since note; a `ClearingInTheAgent` case (Scripted provider) reproducing the 3f4a20ad shape — several read groups of one file, a write in the middle, a clear, then a re-read of an unchanged cleared range — asserting the restore and the `reread_same_range` event, and a second re-read after the file changed asserting a normal full result. Manual: one pane turn of heavy single-file editing, then check the session log for `tool_results_cleared {protected: …}` and `reread_same_range` counts.

## Tests
Landed in `dc0303ae`; run on a clean export of that commit: `python3 -m pytest tests/test_tool_output_bounds.py -q` → 23 passed. New cases:
- `WorkingSetClearing.test_edited_file_and_repeated_range_survive_and_stubs_name_path_range_hash`: a read of a later-edited file and the newest of two identical `src/Pane.h:1690-1800` reads survive a clear (`protected == 2`); the cleared read's stub carries `path`, `range`, `sha256`, `edited_since: false` and the exact ranged `read_file` call, all on one line.
- `test_stub_says_when_the_file_changed_after_the_read`: `edited_since: true` and the note says so after a `write_file`.
- `test_protection_is_capped_newest_first`: 8 twice-read ranges → exactly `CLEAR_PROTECT_MAX` (6) protected, the newest ones.
- `test_read_key_knows_read_file_and_plain_sed`: `sed -n 'A,Bp' file` counts as a range read; piped sed does not.
- `ClearingInTheAgent.test_reread_same_range_is_counted_and_says_whether_it_was_cleared`: real Agent + Scripted provider, 3f4a20ad shape: read, clear, then a `sed -n` re-read and a `read_file` re-read of the cleared range → two `reread_same_range` events (`times` 2 and 3, `earlier_cleared: true`); `tool_results_cleared` carries `protected`.

`tests/test_system_prompt.py`: 3 SizeTests budget failures reproduce identically at 54018502, before this change, so they are not caused by it.
