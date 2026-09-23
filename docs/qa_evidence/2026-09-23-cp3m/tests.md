# #CP3M implementer evidence — 2026-09-23

## `python3 -m unittest tests.test_agent.GuestAggregateUsageTests -v`
```
test_guest_turn_aggregate_never_becomes_the_context_measurement (tests.test_agent.GuestAggregateUsageTests.test_guest_turn_aggregate_never_becomes_the_context_measurement) ... ok
test_relay_provider_usage_still_calibrates_the_tracker (tests.test_agent.GuestAggregateUsageTests.test_relay_provider_usage_still_calibrates_the_tracker) ... ok
test_unchanged_transcript_reports_no_reduction (tests.test_agent.GuestAggregateUsageTests.test_unchanged_transcript_reports_no_reduction) ... ok

----------------------------------------------------------------------
Ran 3 tests in 0.003s

OK
```

## `python3 -m unittest tests.test_agent tests.test_guest_context_meter tests.test_guest_harness_provider`
```
----------------------------------------------------------------------
Ran 121 tests in 5.686s

FAILED (failures=1)
```

The one failure (WorkerTests.test_malformed_request_does_not_crash) is pre-existing on HEAD, in backend/worker.py:763, untouched by this change — filed as #1Q2F.
