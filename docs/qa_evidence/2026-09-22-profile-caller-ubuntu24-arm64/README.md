# PF14: Ubuntu 24.04 arm64 recurrence

Release gate for #R6BS, source `59a51d92578b2c77284e2ecb9ffcde98f7671b7d`.

- Run: https://github.com/elliottash/relay-terminal/actions/runs/35671890154
- Failed job: `106570137102`, Ubuntu 24.04 arm64, Python 3.12.
- Artifact: `deb-1` (`10671752683`), `out/logs/build-ubuntu-24-04.log`.
- Check: `test_relay_profile.ConverterTests.test_pstats_round_trips_through_speedscope`.
- Both backend attempts failed this caller-total assertion. All other CTest entries passed
  (88/89); the aggregate backend-and-bash entry failed.

[failures.txt](failures.txt) preserves the raw pstats dictionaries and resulting speedscope
objects from both occurrences, with original log line numbers. The diagnostic added in
`9729f050` made these inputs available. The full original log remains in the run artifact.
The first failure compares middle.total=0.000005 with leaf.self=0.004782. Its raw profile
contains zero call counts on the expected middle→leaf edge and an unexpected socketserver
service_actions frame attributed to builtins.sum. This is evidence to investigate, not a
confirmed cause. No assertion was weakened and no fix is claimed.

After the complete run finished, its failed jobs were rerun once using
`gh run rerun 35671890154 -R elliottash/relay-terminal --failed`, as authorized for this known
intermittent. At the time of this record, that retry is still running. A passing retry would
be release-gate evidence, not proof that PF14 is resolved.
