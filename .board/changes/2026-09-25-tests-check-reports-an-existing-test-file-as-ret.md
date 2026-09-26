---
id: R7HD
type: work
status: planned
labels: [bug, tests, switchboard]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
source: 'Claude Code pane 987d2a1a, 2026-09-25, while landing #WNKN'
links: {plans: [], commits: [], evidence: [], related: [WNKN], github: null}
---
# tests_check reports an existing test file as retired and manual evidence as "not there" when it exists

## Issue
Running `tests_check` on #WNKN at revision 245d2efe reported `tests/test_tools.py is not in the project any more` (the file exists, 48 tests, 31 KB) and `manual evidence docs/qa_evidence/2026-09-25-wnkn-foreign-hunks/README.md is not there` (the file exists at the repo root). The manual check in `backend/relay_core/test_history.py` ~1114 calls `Path(listed_file).exists()` on a repo-relative path, so it resolves against the board worker's cwd, not the repository. The retired verdict comes from `resolve(entry, records_list)` finding no record for `unittest:tests.test_tools`, so a pytest-style file that has never been collected as a unittest scope reads as retired rather than never-run. Both are warnings, not blocks, but they send an implementer to fix a card that has nothing wrong.

## Done means
- A `## Tests` line whose file exists in the repository is never reported as retired (`is not in the project any more`), however `resolve()` came up empty — it reads as never-run / missing-evidence instead, and the `Replace retired check` action is not offered for it.
- Manual evidence paths are resolved against the project root, not the board worker's cwd: a `manual: docs/…` line whose file exists under the repo produces no `not there` finding, no matter what directory the worker runs in.
- A genuinely deleted test file still reports retired, and a manual line whose file is genuinely missing still reports `not there`.
Failure shows as the #WNKN symptom repeating: `tests_check` on a card that lists an existing test file or existing evidence directory emits the false retired / not-there warnings.

## Plan
**Goal** — `tests_check` stops emitting the two false warnings seen on #WNKN: an existing test file reported as retired, and existing manual evidence reported as `not there`.

**Findings** (verified by reading the code, 2026-09-25):

- `backend/relay_core/test_history.py`, `check_card()` (~line 1113, the `manual` branch of the per-entry findings loop): `Path(listed_file).exists()` resolves a repo-relative path (e.g. `docs/qa_evidence/2026-09-25-wnkn-foreign-hunks/README.md`) against the board worker's cwd, not the project. `H.check_card` takes no root parameter; its caller `backend/relay_core/tests_protocol.py:497` (`TestsProtocol.check_card`) has `self.project` and currently cannot pass it down.
- Same file, `line_status()` (~line 1001): when `resolve(entry, records_list)` returns `[]`, it sets `retired = True` with `{invocation} is not in the project any more`, and the findings loop (~line 1118) emits a `failure` finding plus the `Replace retired check` action. But `resolve()` returns `[]` for two different situations: the test is gone, or discovery never produced a record for it — never collected, or cut off by the discovery cap (`backend/relay_core/test_probe.py:9`, `MAX_TESTS = 5000`, and this project sits at exactly 4,908 unittest + 92 ctest = 5,000). `tests/test_tools.py` exists (verified: 4 TestCase classes) yet `unittest:tests.test_tools` has no record, so it reads as retired. Card TC5K (`.board/changes/2026-09-22-test-discovery-cap-false-retirement.md`, status discussing) reports the same false-retirement symptom from the truncation side; its "Done means" is met by the same file-existence fallback.
- `parse_test_line()` (test_history.py:782) puts a repo-relative `file` on the entry for the unittest and manual spellings, and for a `ctest` line with a ` — path` tail. A bare `ctest -R X` has no `file`.

**Steps**:

1. In `backend/relay_core/test_history.py`, add a `root: Path | None = None` keyword argument to `H.check_card`; `None` keeps today's cwd behaviour so existing callers and tests are unaffected. Add a small helper, e.g. `_listed_exists(listed_file, root)`: absolute paths are checked as-is; relative paths are checked against `root` when given, else cwd.
2. Manual evidence: in the `manual` branch, replace `Path(listed_file).exists()` with `_listed_exists(listed_file, root)`. In `tests_protocol.py:505`, pass `root=self.project` (or whatever keyword name step 1 chose) into `H.check_card`.
3. Retired verdict: in `check_card`, after `found = resolve(entry, records_list)` comes back empty, check whether the entry has a `file` that `_listed_exists` confirms. If the file exists, do not set `retired`: treat the line as never-run instead — the same missing-evidence wording used for a discovered-but-never-run entry ("no run of …" / "has never run here"), no `failure` finding, no `Replace retired check` action. Keep the retired verdict when the file does not exist (genuinely deleted) and when the entry has no `file` at all (bare `ctest -R X` without a path tail) — there is nothing cheap to check, and a renamed ctest target reading as retired is acceptable. Implement the downgrade in `check_card` around the `line_status` call rather than complicating `line_status`'s signature.
4. Card TC5K: add a `board_comment` (kind `note`) there saying R7HD's file-existence fallback fixes its symptom; leave TC5K's status to the owner — do not close it from this card.

**Risks**:

- The never-run wording must not imply the test passed anywhere; reuse the existing missing-evidence phrasing rather than inventing a new status.
- A card that lists a test it intends to delete will now read as never-run instead of retired until the file is actually gone — correct behaviour, but worth a sentence in the commit message.
- No owner decision needed.

**Verify** — the test homes are `tests/test_test_history.py` (direct `H.check_card` cases at ~475–630, including the existing manual `no-such-directory` case at :601) and `tests/test_tests_protocol.py` (`TP.check_card` cases at ~1293–1313):

- New cases in `tests/test_test_history.py`, each with an explicit tmp `root`: (a) a unittest file line whose file exists under `root` but has no records → no retired finding, no `Replace retired check` action, never-run status; (b) a `manual:` line whose file exists under `root` but not under cwd → no `not there` finding (run with cwd elsewhere, e.g. `monkeypatch.chdir`); (c) a `manual:` line genuinely missing → still `not there`; (d) a file line pointing at a deleted file → still retired.
- One case in `tests/test_tests_protocol.py`: `TestsProtocol.check_card` with the worker's cwd changed away from the project and a `manual:` evidence file under `self.project` → clean.
- Run `pytest tests/test_test_history.py tests/test_tests_protocol.py -k check_card` (or the matching unittest invocation), then re-run `tests_check` on #WNKN and confirm both false warnings are gone and the card reports no retired / not-there findings.
