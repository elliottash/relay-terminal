---
id: XG2G
type: work
status: needs-verification
labels: [bug, agent, tools]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: abd3775d-30c3-4815-96ce-705ba1b7e3d7
rank: zzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: ['17625451', 7b7f258b, 8ab46f8d], evidence: [docs/qa_evidence/2026-09-25-xg2g-large-file-edit/], related: [5NDQ, 234Z], github: null}
---
# edit_file refuses files over 128 KiB, so agents patch src/Pane.h with ad-hoc Python scripts that miss

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** `backend/relay_core/tools.py:476` raises `File exceeds the 128 KiB preview/read limit.` `src/Pane.h` is 17,103 lines. In session `3f4a20ad` (#234Z) `read_file` was refused once and `edit_file` four times (worker log, `outcome=refused`). The agent fell back to **11** `python3 - <<'EOF' … s.replace(old, new)` scripts on `Pane.h`. They have no preview, no checkpoint entry of their own and no exact-match diagnostics. One missed on an extra `)` and cost two more steps. A file edited by a shell script is also invisible to anything that tracks the agent's edits (checkpoints, land claims, see the land.py cards).

**How to address.**
1. `edit_file` on a large file: require the unique exact match as now, but build the preview from a window around the match (say ±40 lines), not the whole file. The 128 KiB limit exists for the preview, and the edit does not need it.
2. `read_file` with `from_line`/`to_line` on a large file should read just that range. Only a whole-file read needs the limit.
3. On a failed match, say which part of `old_string` first diverges (line and column), so the retry is one step.
4. Keep splitting `Pane.h` (#243T moved 18 bodies out; it is still 17k lines). That is a separate card, but it is the root cause.

**Done means.** `edit_file` and ranged `read_file` succeed on a 500 KiB file in a test; a mismatch reports the first diverging line.

## Done means
`edit_file` and a ranged `read_file` (`from_line`/`to_line`) work on a file of hundreds of KiB — the size of `src/Pane.h` — locally and over `host:`, so no agent needs a `python3 - <<'EOF'` fallback to patch a big file. A failed match reports the line and column where `old_string` first diverges, so the retry is one step. Failure shows as today's refusals: "File exceeds the 128 KiB preview/read limit" on an edit, or a not-found error with no position.

## Plan
**Goal.** `edit_file` and ranged `read_file` stop refusing files over 128 KiB (`src/Pane.h` is ~600 KB at 17,103 lines), and a failed exact match says where `old_string` first diverges. Whole-file reads, `write_file` content limits and every other guard are unchanged. Splitting `Pane.h` further is #243T, not this card.

**Findings.**
- `Workspace.read_bytes` (`backend/relay_core/tools.py:478`) refuses `st_size > MAX_FILE` with `File exceeds the 128 KiB preview/read limit.` Local `read_file` calls it at execute (`:1095`); `edit_file` and `write_file` call it for the old text at prepare (`:742`) and again at execute (`:1108`) — so both tools refuse a big file before the range or the edit is ever tried.
- `_edited` (`:1002`) refuses again when the edited result would pass `MAX_FILE` (`The edited file would exceed the 128 KiB limit.`), so the read cap alone is not the whole fix.
- Ranged reads already slice correctly in `_read_result` (proved under the cap by `tests/test_tool_output_bounds.py:137`); the range just never reaches it on a big file.
- The caps travel to the ssh host: `_remote_read` (`:894`) and `_remote_before` (`:903`) pass `cap=MAX_FILE + 1` to `remote_files.read_script` (`backend/relay_core/remote_files.py:79`).
- A miss in `_edited` says only `old_string was not found in the file` — no line or column.
- The edit preview is already a `difflib.unified_diff` (`_write_prepared`, `:744`), so a small edit in a big file previews small; the ±40-line window the planning notes suggested is not needed.

**Steps.**
1. Add `MAX_LARGE_FILE = 8 * 1024 * 1024` beside `MAX_FILE` (`:47`). Give `Workspace.read_bytes` a `limit` parameter (default `MAX_FILE`) used for both the `st_size` and the post-read check; keep the binary (NUL) and utf-8 checks. A refused plain read should now say to pass `from_line`/`to_line`.
2. Local `read_file` (`execute`, `:1095`) reads with `limit=MAX_LARGE_FILE` when the args carry a range, `MAX_FILE` otherwise; `_read_result` then slices exactly as today. The prepare-time range validation (`_range_args`, `:704`) is unchanged.
3. The old-text reads for `edit_file`/`write_file` at prepare (`:742`) and execute (`:1108`) pass `limit=MAX_LARGE_FILE`, and `_edited`'s result-size check becomes `MAX_LARGE_FILE` with an updated message. Files past 8 MiB still refuse, saying so.
4. Remote: `_remote_read` passes the large cap when the call is ranged and `_remote_before` always does (an edit needs the whole file); a plain remote whole read keeps `MAX_FILE`. Check `read_script`'s error names the cap it hit.
5. In `_edited`, when `found == 0`: anchor on `old_string`'s longest line, take up to ~50 `str.find` candidates, pick the window with the best `difflib.SequenceMatcher` ratio, and report the 1-based line and column of the first differing character with both texts at that point — e.g. `closest match is line 1234, column 9: the file has "foo)", old_string has "foo()"`. Keep today's sentence, and today's whole message when nothing is close.
6. Keep the model's context bounded: a ranged read whose content passes `MODEL_RESULT_CHARS` gets the same head/tail stub as a whole read (`model_result`, `backend/relay_core/tools.py:295`; the reread marker already names the range args). The existing 10-line-range assertion at `tests/test_tool_output_bounds.py:150` still passes, since only oversized ranges stub.
7. Update the `read_file`/`edit_file` descriptions in `TOOLS`: whole-file reads stop at 128 KiB, ranged reads and edits work to 8 MiB.

**Risks.** A huge range on a now-readable file could feed the model megabytes — step 6 is the guard. A ranged remote read still ships the whole file over the wire before slicing; accepted at ≤8 MiB, server-side slicing would be a follow-up. Files over 8 MiB still refuse by design (split the file). No owner decision needed.

**Verify.** New tests: `tests/test_tools.py` — `edit_file` unique replacement on a ~500 KiB file (small preview, mode kept); the not-found error names the diverging line and column; the whole-file read refusal names the range args. `tests/test_tool_output_bounds.py` — a ranged read of the 500 KiB file returns exactly those lines, and an oversized range's model copy stays ≤ `MODEL_RESULT_CHARS`. `tests/test_ssh_remote.py` — the existing 128 KiB refusal (`:364`) still holds for a whole read, while a ranged read and an `edit_file` on a big remote file succeed. Run `python3 -m pytest tests/test_tools.py tests/test_tool_output_bounds.py tests/test_ssh_remote.py -q`. Python-only change: `land.py commit`'s byte-compile is the build gate. Manual: `edit_file` and a ranged `read_file` on `src/Pane.h` in a scratch pane.

## Tests
Run in `tests/`: `PYTHONPATH=../backend python3 -m unittest -v test_tools test_tool_output_bounds test_ssh_remote` (the repo has no pytest; this is the plan's command in the runner `scripts/test.sh` uses).

- `test_tools.ToolTests.test_edit_file_works_on_a_file_past_128_kib`: unique edit on a ~560 KiB file, preview under 2,000 chars, mode kept.
- `test_tools.ToolTests.test_a_whole_read_of_a_big_file_is_refused_and_names_the_range`: whole read refused naming `from_line/to_line`; a 3-line range returns exactly those lines.
- `test_tools.ToolTests.test_files_past_8_mib_are_still_refused`: edit and ranged read of an 8 MiB + 1 file refuse with "8 MiB".
- `test_tools.ToolTests.test_a_missed_edit_names_the_line_and_column_it_diverges_at`: the #234Z extra `)` (two-line and one-line `old_string`), tab-vs-spaces before the anchor line, and no position when nothing anchors.
- `test_tool_output_bounds.ExecutorBounds.test_a_range_of_a_file_past_128_kib_is_exact_and_bounded`: exact range on a ~560 KiB file; an 18,000-line range's model copy stays within the cap and names `next_from_line`.
- `test_ssh_remote.RemoteFileToolTests.test_a_big_remote_file_reads_by_range_and_edits`: over `host:`, a whole read is still refused, and a ranged read and an `edit_file` succeed. `test_read_refuses_what_the_local_read_refuses` (the 128 KiB whole-read refusal) is unchanged and passes.

Result on a clean export of `7b7f258b`: 102 run, 2 failures, both `RemoteRouterTests.test_command_shaped_lines_are_typed_on_the_host`, which fails the same way on `main` from before this card.

Manual, on a copy of the real `src/Pane.h` (1,072,855 bytes): ranged read OK, exact edit OK (508-char preview), a planted extra `)` reported at line 9006, column 67. See the evidence README.
