# #XG2G evidence: edit_file and ranged read_file on files past 128 KiB

Commits `17625451` and `7b7f258b`. Everything below was run on a clean `git archive` export of
`7b7f258b` (2026-09-25), not in the shared checkout.

## Targeted tests

`cd tests && PYTHONPATH=../backend python3 -m unittest -v test_tools test_tool_output_bounds test_ssh_remote`
(the lines about big files and misses, plus the summary)

```
test_a_missed_edit_names_the_line_and_column_it_diverges_at ... ok
test_a_whole_read_of_a_big_file_is_refused_and_names_the_range ... ok
test_edit_file_ambiguous_then_replace_all ... ok
test_edit_file_works_on_a_file_past_128_kib ... ok
test_a_range_of_a_file_past_128_kib_is_exact_and_bounded ... ok
test_big_output_is_bounded_for_the_model_with_counts_and_a_handle ... ok
test_a_big_remote_file_reads_by_range_and_edits ... ok
  test_command_shaped_lines_are_typed_on_the_host (text='cp a b') ... FAIL
  test_command_shaped_lines_are_typed_on_the_host (text='kubectl get pods in the namespace') ... FAIL
FAIL: test_command_shaped_lines_are_typed_on_the_host (text='cp a b')
FAIL: test_command_shaped_lines_are_typed_on_the_host (text='kubectl get pods in the namespace')
Ran 102 tests in 8.584s
FAILED (failures=2)
```

Both `RemoteRouterTests.test_command_shaped_lines_are_typed_on_the_host` failures also fail on a
clean export of `main` from before this card (router routing, unrelated to file tools).

## Real file: a copy of src/Pane.h

```
src/Pane.h: 1072855 bytes
read_file from_line=9006 to_line=9007: ok, total_lines=17296
edit_file with a planted extra ')': old_string was not found in the file. The closest match first differs at line 9006, column 67: the file has ";\n\n    // ----- steering, away" where old_string (its line 1) has ");\n\n". Read the file again and copy the exact text, including whitespace and indentation.
edit_file exact: 1 replacement, preview 508 chars
whole read_file: File exceeds the 128 KiB preview/read limit for a whole-file read. Read it in parts with from_line/to_line; edit_file works on it as it is.
```
