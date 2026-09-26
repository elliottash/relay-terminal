---
id: PWY9
type: work
status: planned
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# PYTHONPATH-once source guard test fails on HEAD

## Issue
`tests.test_guest.GuestSourceTests.test_the_backend_directory_is_appended_to_pythonpath_only_once` fails: the guard expects the PYTHONPATH append once, but `src/PaneRuntime.cpp` startTerminal's PYTHONPATH handling no longer matches (no working-tree diff on that file, so it fails on HEAD). Found while testing card #PCJY.

## Done means
`tests.test_guest.MirroredInCxx.test_the_backend_directory_is_appended_to_pythonpath_only_once` (the card names the class by an old name, `GuestSourceTests`; there is only this one) passes on a clean export of main, and it passes by finding the dedup guard in the C++ file it reads — not by being weakened or removed. It still asserts both halves of the rule: the `contains(backendDir)` guard precedes the `qputenv("PYTHONPATH"` append, and that append occurs exactly once in that file. Failure would show as `ValueError: substring not found` (test reads the wrong file) or an assertion (dedup dropped or duplicated in the C++).

## Plan
**Goal.** Make the mirror guard in `tests/test_guest.py` read the file the pythonPath dedup actually lives in, so the test passes and still guards the dedup. One-test change. **This card describes the same failure as #M7QK** (filed 2026-09-24, already planned); one fix lands both — see Risks for the dedupe question.

**Findings.** The dedup was not dropped — it moved. In the #243T split of `src/Pane.h`, the `startTerminal` pythonPath code went to `src/PaneRuntime.cpp`, where the guard literal still exists verbatim at lines 691–693:

```cpp
const QString pythonPath = qEnvironmentVariable("PYTHONPATH");
if (!pythonPath.split(QDir::listSeparator(), Qt::SkipEmptyParts).contains(backendDir))
    qputenv("PYTHONPATH", (pythonPath.isEmpty() ? backendDir : pythonPath + QDir::listSeparator() + backendDir).toUtf8());
```

But `MirroredInCxx.PANE` (tests/test_guest.py line ~73) points at `src/Pane.h`, so `text.index(...)` in the failing test (line ~140) throws `ValueError`. Every other literal that class reads is still in `src/Pane.h` (verified by search), so only this one test changes files. `src/PaneRuntime.cpp` contains exactly one `qputenv("PYTHONPATH"` (line 693; the line-904 PYTHONPATH write is `shellEnvironment <<`, not `qputenv`), so the `count == 1` assertion still holds against it. `src/main.cpp:358` has another `qputenv("PYTHONPATH"` but the test never reads that file. There is no `GuestSourceTests` class; the card means `MirroredInCxx`.

**Steps.**

1. `python3 scripts/land.py begin <me> tests/test_guest.py`
2. In `tests/test_guest.py`, add a second path constant beside `PANE`, e.g. `PANE_RUNTIME = Path(__file__).resolve().parents[1] / "src" / "PaneRuntime.cpp"`, and change only `test_the_backend_directory_is_appended_to_pythonpath_only_once` to read `self.PANE_RUNTIME` instead of `self.PANE`. Keep the guard literal, the `guard < append` assertion and the `count == 1` assertion exactly as they are. Extend the docstring with one clause noting the code moved to `PaneRuntime.cpp` in the #243T split.
3. From the repo root: `PYTHONPATH=backend python3 -m unittest tests.test_guest -v` — the whole file passes, including the fixed test.
4. Land: `python3 scripts/land.py commit <me> -m "test_guest: read pythonPath dedup guard from PaneRuntime.cpp"` (Python-only, so the C++ verify-build gate does not apply; land.py still byte-compiles it).

**Risks.** Duplicate of #M7QK: both cards plan the same one-test fix. Whoever runs first lands it; the other card should be merged into that one (`board_merge_cards`) or closed. Question for the owner: run one card and merge the other into it — #M7QK is the older and already Planned. If another session has moved the code again, `search_files` for `pythonPath.split` before editing and point the constant at wherever it lives.

**Verify.** `PYTHONPATH=backend python3 -m unittest tests.test_guest` (whole file) passes. Clean-export check from the issue: `git archive HEAD | tar -x -C <scratch> && cd <scratch> && PYTHONPATH=backend python3 -m unittest tests.test_guest.MirroredInCxx`.
