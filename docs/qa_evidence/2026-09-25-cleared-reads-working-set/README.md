# #5NDQ evidence — landed dc0303ae

Clean export of dc0303ae (`git archive dc0303ae | tar -x`), then:

```
$ python3 -m pytest tests/test_tool_output_bounds.py -q
23 passed
$ python3 -m pytest tests/test_tool_output_bounds.py -v -k "WorkingSet or reread"
tests/test_tool_output_bounds.py::ExecutorBounds::test_range_reread_of_a_running_background_job PASSED [ 14%]
tests/test_tool_output_bounds.py::ClearingUnit::test_read_file_stub_names_the_reread PASSED [ 28%]
tests/test_tool_output_bounds.py::WorkingSetClearing::test_edited_file_and_repeated_range_survive_and_stubs_name_path_range_hash PASSED [ 42%]
tests/test_tool_output_bounds.py::WorkingSetClearing::test_protection_is_capped_newest_first PASSED [ 57%]
tests/test_tool_output_bounds.py::WorkingSetClearing::test_read_key_knows_read_file_and_plain_sed PASSED [ 71%]
tests/test_tool_output_bounds.py::WorkingSetClearing::test_stub_says_when_the_file_changed_after_the_read PASSED [ 85%]
tests/test_tool_output_bounds.py::ClearingInTheAgent::test_reread_same_range_is_counted_and_says_whether_it_was_cleared PASSED [100%]
```

tests/test_system_prompt.py: 3 SizeTests budget failures reproduce identically on a clean export of 54018502 (before this change); not caused by #5NDQ.
