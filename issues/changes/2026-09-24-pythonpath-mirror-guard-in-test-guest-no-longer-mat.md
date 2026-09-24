---
id: M7QK
type: work
status: inbox
labels: [bug, tests, mirrored-in-cxx]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: pane bd9e4ae0 delivering #Q8TM, 2026-09-24
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# pythonPath mirror guard in test_guest no longer matches the C++ source

## Issue

`tests.test_guest.MirroredInCxx.test_the_backend_directory_is_appended_to_pythonpath_only_once`
fails on a clean export of main. Measured 2026-09-24, main at `79dd56a`:

```
git archive HEAD | tar -x -C /tmp/pristine
cd /tmp/pristine && PYTHONPATH=backend python3 -m unittest \
  tests.test_guest.MirroredInCxx.test_the_backend_directory_is_appended_to_pythonpath_only_once
→ ValueError: substring not found
  File "/tmp/pristine/tests/test_guest.py", line 140, in …
  guard = text.index('if (!pythonPath.split(QDir::listSeparator(), Qt::SkipEmptyParts).contains(backendDir))')
```

The mirror test guards the dedup that appends `backend/` to `PYTHONPATH` once in the C++
terminal startup. That C++ shape was refactored on main and the guard literal no longer occurs
in the source, so the guard is now vacuous-by-crash. The fix needs a look at the current
`startTerminal` pythonPath code: if the dedup still exists in a new shape, update the guarded
literal (and what it asserts around it); if the dedup was dropped, that is the real regression
the guard was for. Not fixed in passing while delivering #Q8TM.
