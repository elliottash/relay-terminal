---
id: M7QK
type: work
status: planned
labels: [bug, tests, mirrored-in-cxx]
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: pane bd9e4ae0 delivering
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

## Done means
`tests.test_guest.MirroredInCxx.test_the_backend_directory_is_appended_to_pythonpath_only_once` passes on a clean export of main, and it passes by actually finding the dedup guard in the C++ source it reads — not by being weakened or removed. The test still asserts both halves of the rule it was written for (review of 51587e3): the `contains(backendDir)` guard precedes the `qputenv("PYTHONPATH"` append, and that append occurs exactly once in the C++ source. Failure would show as the test crashing with `ValueError: substring not found` (guard literal not in the file it reads) or asserting (dedup actually dropped or duplicated in the C++).

## Plan
**Goal.** Make the mirror guard in `tests/test_guest.py` read the file the pythonPath dedup actually lives in now, so the test passes and still guards the dedup. This is a one-test change.

**Findings.** The dedup was not dropped — it moved. When `Pane` method bodies were split out of `src/Pane.h` (card #243T), the `startTerminal` pythonPath code went to `src/PaneRuntime.cpp`, where the guard literal still exists verbatim at lines 593–595:

```cpp
const QString pythonPath = qEnvironmentVariable("PYTHONPATH");
if (!pythonPath.split(QDir::listSeparator(), Qt::SkipEmptyParts).contains(backendDir))
    qputenv("PYTHONPATH", (pythonPath.isEmpty() ? backendDir : pythonPath + QDir::listSeparator() + backendDir).toUtf8());
```

But `tests/test_guest.py` `MirroredInCxx.PANE` (line ~67) points at `src/Pane.h`, so `text.index(...)` in `test_the_backend_directory_is_appended_to_pythonpath_only_once` (line ~140) throws `ValueError`. Every other literal that test class reads (`guestSpecs`, `launchers`, `handleGuestHook`, `guestHelperEnvironment`, `startGuestTail`, `setGuest(guestProgram(foregroundArgv()))`) is still in `src/Pane.h` — verified by search — so only this one test needs to change files.

**Steps.**

1. `python3 scripts/land.py begin <me> tests/test_guest.py`
2. In `tests/test_guest.py`, add a second path constant beside `PANE`, e.g. `PANE_RUNTIME = Path(__file__).resolve().parents[1] / "src" / "PaneRuntime.cpp"`, and change only `test_the_backend_directory_is_appended_to_pythonpath_only_once` to read `self.PANE_RUNTIME` instead of `self.PANE`. Keep the guard literal, the `guard < append` assertion and the `count == 1` assertion exactly as they are. Extend the test's docstring with one clause noting the code moved to `PaneRuntime.cpp` in the split.
3. Run `PYTHONPATH=backend python3 -m unittest tests.test_guest.MirroredInCxx -v` from the repo root — all tests in the class pass, including the fixed one.
4. Land with `python3 scripts/land.py commit <me> -m "test_guest: read pythonPath dedup guard from PaneRuntime.cpp"` (Python-only change, so the C++ verify-build gate does not apply; land.py still byte-compiles it).

**Risks.** None of substance: the dedup exists and the test's assertions still describe it, so there is no C++ regression to decide about. If another session has since moved the code again, `search_files` for `pythonPath.split` before editing and point the constant at wherever it lives.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_guest` (the whole file, not just the one test) passes. For the clean-export check from the issue: `git archive HEAD | tar -x -C /tmp/m7qk-check && cd /tmp/m7qk-check && PYTHONPATH=backend python3 -m unittest tests.test_guest.MirroredInCxx`.
