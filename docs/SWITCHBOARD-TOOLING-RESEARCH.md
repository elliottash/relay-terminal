# Switchboard as the project's tooling hub: tests, profiling and what else fits (research, 2026-09-20)

Research date: 2026-09-20. Two surveys feed this: an external one of the products that already ship
each shape (primary docs, URLs inline), and a read-only survey of this checkout (file:line refs read,
not guessed). Nothing here is implemented; no sequencing is given, because that belongs on a card.

**How to read the claims.** A statement with a URL was read in that vendor's own documentation on
2026-09-20; a statement with a `path:line` was read in this tree the same day, while other sessions
were editing it, so line numbers may have drifted by tens of lines — function names and quoted
signatures are the stable anchors. What could not be confirmed from a primary source is marked
*unverified* and collected in §7; local commands actually run are called out where they appear.

## 1. The question the owner asked

Four things, in the owner's order: **(a)** a `## Tests` section on a card listing the tests that
belong to it, with a **Check** button that checks them — are they stale, are they slow; **(b)** a
**Test suites** pane beside the Switchboard, listing tests with their stats (failure history,
execution time); **(c)** a **Profile** button that profiles the project; **(d)** an answer to "what
other project/SWE tooling could smoothly be added?"

Read together they are one request, not four. The Switchboard stops being a card board and becomes
**the project's control panel**: every engineering activity that today happens by typing a command
into a pane becomes a button or a pane beside the board, and — the part that makes it worth doing
rather than a dashboard — **its results are written back into the cards**, so the agent that picks a
card up and the QA session that verifies it read the same numbers the button produced. A test result
in terminal scrollback cannot gate anything; the same result under `## Tests` can. The standing rule
for all four is the owner's: copy a proven tool's shape rather than design one.

## 2. Ground truth in this repo

**Card sections are parsed generically.** Heading-driven, in two places that agree: `_HEADING_LINE_RE`
/ `section_span()` / `section_text()` / `append_body_section()` at `board.py:2078-2116`, and
`_SECTION_RE` / `section_headings()` / `_section_span()` at `board_tools.py:128, 456-493`. Any `## `
heading is writable today through `board_update_card {append_section|replace_section}` (spec
`board_tools.py:213-218`, implementation `BoardTools._update`, `:1887`), so a `## Tests` section
needs **no format extension at all** — though a new heading does need adding to `AGENT_SECTIONS`
(`board_tools.py:114-117`), or every agent write is logged as rewriting the owner's own text.

**There is no section renderer in the GUI.** The card payload already carries `"sections":
section_headings(card.body)` (`BoardTools._read`, `board_tools.py:1692-1724`, sent as `board_card`
by `BoardCommands.dispatch`, `board_protocol.py:1133-1139`); the GUI stores it in
`CardDetail::m_sections` (`src/BoardPane.cpp:1843-1845`) and uses it for exactly one decision,
`hasPlan()` (`:1726`). The body is drawn wholesale as Markdown into one `QTextBrowser`
(`CardDetail::render`, `:2654`). No per-section widget, no registry; the only precedent for "a
widget about one aspect of the card, outside the document" is `m_verifyLine` (`:1452-1459`), a
`QLabel` shown only in a QA lane.

**`links` is the untyped bag.** `front.setdefault("links", {"plans": [], "commits": [], "evidence":
[], "related": [], "github": None})` (`board.py:1487`, again `:1774`), with merge and split writing
`merged_from` / `split_into` and friends dynamically (`board.py:2205-2241`). **Nothing validates the
set of `links` keys**, while `check` does refuse an unknown *top-level* field (`Board._check_card`,
`board.py:1240-1242`, against `ALLOWED_FIELDS`, `:225-231`). So `links.tests: [...]` passes `check`
today untouched; a top-level `tests:` is a `WORK_FIELDS` + `FIELD_ORDER` change plus a
`tests/test_board.py` update.

**A background runner exists; a test store does not.** `backend/relay_core/jobs.py` has
`JobTable.start/get/wait/take_output/snapshot/peek/stop/running` (`jobs.py:82-263`), caps
`MAX_RUNNING = 8` / `KEEP_FINISHED = 16`, process groups so `stop()` kills the tree, a wire protocol
(`jobs`, `jobs_list`, `job_output_get`, `job_stop`, `observe_protocol.py:102-120`) and a GUI half in
`src/JobsPanel.{h,cpp}` — a pure `JobsModel` over `JobRow {id, command, running, stopped, exitCode,
…, elapsedMs}` drawn as `"running · 3:12"` / `"exit 0 · 0:41"`, tested headlessly by
`tests/jobs_test.cpp`. **That is the closest existing shape to "a test with a duration and a
pass/fail state."** Against it: **nothing writes or reads any test-run record** (no JSON, no SQLite,
no file under `$XDG_DATA_HOME/relay`, whose only consumer is `src/Logging.h:6,22`); **there is no
test discovery anywhere** (`project_probe.py`, 1745 lines, detects *issue trackers to import*; greps
for `test_command`, `pytest`, `ctest`, `cargo test`, `framework` return zero hits); and **there is
no profiler** — no py-spy, cProfile, valgrind, gprof, hyperfine or flamegraph in `scripts/`,
`tests/`, `backend/` or `docs/`, `perf` appearing only in `docs/ENGINE-PERF.md` and `strace` once in
`docs/ARCHITECTURE.md:988`.

**The suite's shape.** `CMakeLists.txt:7` is `include(CTest)`, pattern uniform
(`add_executable(relay-<name>-tests tests/<name>_test.cpp)` + `add_test(NAME <name> ...)`,
`CMakeLists.txt:523-526`): 66 C++ targets top-level plus `relay-engine-tests`
(`engine/CMakeLists.txt:125-129`, under `RELAY_BUILD_ENGINE=ON`), and **the whole Python suite is
one ctest entry** — `add_test(NAME backend-and-bash COMMAND ${Python3_EXECUTABLE} -m unittest
discover -s tests -v)` (`CMakeLists.txt:826`, `TIMEOUT 600`). **68 ctest tests total**; 112
`tests/test_*.py`; **no `pytest.ini`, `pyproject.toml`, `setup.cfg` or `conftest.py` anywhere** and
`import pytest` fails — the suite is stdlib `unittest`, `backend/relay_core` deliberately
stdlib-only. What CTest leaves on disk: `build/Testing/Temporary/CTestCostData.txt` **exists**, 19
lines of `<test-name> <n-runs> <avg-seconds>`, beside `LastTestsFailed.log` and `LastTest.log` — all
per-build-dir, gitignored (`.gitignore:4` = `/build*/`) and wiped by a clean build, so a hint rather
than a store. Only 20 of 68 tests have cost data, because sessions run `ctest -R <name>` subsets per
CLAUDE.md; **that gap is the "never run" signal, for free.**

**The machine.** aarch64; `perf` and `ninja` installed, **no** py-spy, valgrind or ccache.
`/proc/sys/kernel/perf_event_paranoid` = **4** (a Debian/Ubuntu extension meaning "prevent all use
of perf events by unprivileged users"; upstream defines only -1/0/1/2 —
https://docs.kernel.org/admin-guide/perf-security.html), `/proc/sys/kernel/yama/ptrace_scope` = 1.
Compiler **g++ 13.3.0**, no clang: `-ftime-trace` and `-fproc-stat-report` were both tried and
**rejected**. The generator is Unix Makefiles (ninja unused), and `CMakeLists.txt` pulls in Qt
Widgets plus optional Pdf/PdfWidgets, DBus, Network, Test — **there is no QtWebEngine**, settling
the viewer question in §4. Results would land where evidence does: `docs/qa_evidence/` holds 265
dated `<YYYY-MM-DD>-<slug>/` directories referenced from `links.evidence`.

## 3. Tests: the proven shapes

### 3.1 Two UI shapes, and the owner asked for both

**Shape A — the tree explorer** (VS Code Testing API, JetBrains, Xcode): a lazily-resolved tree,
status icon per node, duration inline, gutter decorations, "rerun failed" as the primary action. It
answers *what is red right now*. The data model to copy is VS Code's
(https://code.visualstudio.com/api/extension-guides/testing): a `TestItem` carries `id`, `label`,
**`uri` + `range`** — a test item *points at a source location*, the minimum needed to call a test
stale — plus `children`, `tags`, `busy`, `error`, with `resolveHandler` for lazy discovery. The
structural idea to lift: **one test tree, several `TestRunProfile`s** with `kind` ∈ `Run` / `Debug` /
`Coverage`, so Check and Profile are extra profiles over the same tree, not separate features.

**Shape B — the analytics table** (Buildkite Test Engine, Datadog Test Optimization, Codecov Test
Analytics, Trunk): a sortable table whose **row is a test, not a run**; columns = reliability / flake
rate / avg or p95 duration / last failure; click a row for its history. It answers *which tests are
slow, flaky or rotting*. The owner's **Test suites pane is Shape B whole**; the card's **Tests
section + Check is a per-card slice of Shape B plus one Shape A action**. Three JetBrains features
are worth taking that VS Code lacks (https://www.jetbrains.com/help/idea/test-runner-tab.html):
**Sort By Duration** as a toolbar toggle rather than a separate report, **Show Inline Statistics**,
and **test history: last 10 sessions**. A third shape is the most informative per pixel for a
*board-level* pane: **TestGrid** (https://github.com/GoogleCloudPlatform/testgrid), rows = test
targets, columns = runs newest-left, cells coloured by result, so a flaky test is visually a striped
row — a `QTableView` plus a delegate, with a "clustered failures" toggle grouping identical messages
so twenty red rows read as one problem.

### 3.2 "Stale" is six things, and four need no test run

| Staleness | Signal | How to detect locally |
|---|---|---|
| **Never runs** (not collected) | present in the file, absent from the last N runs' JUnit XML | diff collected ids vs the file's parsed test names; here, absence from `CTestCostData.txt` |
| **Disappeared** (deleted/renamed) | in the card's list, absent from collection | set difference — `EnricoMi`'s "tests removed" does exactly this |
| **Skipped forever** | `<skipped>` in every run for N runs | count consecutive skips; pytest `@skip`, ctest `DISABLED` property |
| **Orphaned from its code** | the files it covered are gone, or it never executes the card's changed files | a testmon/TIA-style coverage map — or simply: the card's `commits:` touched files no test in its list covers |
| **Obsolete snapshot/golden** | a `.snap`/golden entry whose test no longer exists | Jest reports obsolete snapshots after a run, and only a **full** run prunes them (https://github.com/jestjs/jest/issues/5005) |
| **Asserts nothing / kills no mutants** | `Assertionless Test` smell; surviving mutants | test-smell detectors (https://testsmells.org/pages/testsmells.html; survey https://arxiv.org/pdf/2104.14640) and mutation testing |

The first four are cheap and deterministic; the last two are not — the survey above finds older
smell detectors "misclassified over 70%", so a smell is a hint and not a verdict, and mutation
testing is far too slow to sit behind a button. That survey also grounds why any of this matters:
**83% of test classes in 110 OSS projects carry at least one smell**.

### 3.3 The metrics are settled and quotable

- **Reliability.** Buildkite defines it verbatim as `reliability = passed_runs / (passed_runs +
  failed_runs) * 100`, at suite level and over executions at test level
  (https://buildkite.com/docs/test-engine/test-suites). Its default view groups executions by test
  with a configurable Display column set, filterable by owner and label (`flaky`, `slow`,
  `feature-test`); it ingests 16+ collectors **plus JUnit XML**, retained 120 days.
- **Flaky.** Datadog's definition is the cleanest and is implementable locally: a test that "exhibits
  both a passing and failing status across multiple test runs *for the same commit*"
  (https://docs.datadoghq.com/tests/flaky_test_management/). States `is_flaky`, `is_new_flaky`,
  `is_known_flaky`; page columns average duration, **first flaked / last flaked with commit SHA**,
  commits flaked, failure rate since flakiness began.
- **Flake score without repetition.** TestGrid scores it "based on the number of **transitions from
  passing to failing** (and vice versa), with more weight given to more recent transitions", sorting
  rows by it descending — no retries and no same-commit repetition, so it works from the ordinary
  sequential runs a local board will actually have. Its tab vocabulary is **PASSING / FAILING
  (consistent failures) / FLAKY (neither)**, matching Trunk's split between **flaky** (intermittent)
  and **broken** (consistently failing = a real regression, https://docs.trunk.io/flaky-tests) —
  which matters for Check, because a red test is not necessarily a flake.
- **Slow.** Everyone uses a *history-relative* measure, not an absolute threshold (pytest ships
  `--durations=N`, https://docs.pytest.org/en/stable/how-to/usage.html), and p50 **and** p95 are both
  needed: a bimodal test (fast normally, 30 s when it retries a socket) is invisible in a mean.
- **The vocabulary to speak in** is cargo-nextest's. A test exceeding `slow-timeout` (default
  **60 s**) is printed with a **`SLOW`** marker *while it is still running*
  (https://nexte.st/docs/features/slow-tests/), progressing SLOW → TERMINATING → TIMEOUT; one that
  fails then passes within its retry budget is **FLAKY** (https://nexte.st/docs/features/retries/);
  the summary reads **"1 test run: 1 passed (1 flaky)"** / **"4 passed (1 slow)"**. Three conventions
  transfer verbatim: **SLOW and FLAKY as first-class statuses beside pass/fail/skip**, **"(1 slow)"
  in the summary line**, and **a `list` mode separate from a `run` mode**
  (https://nexte.st/docs/machine-readable/) — which is what makes a card's Tests section checkable
  without running anything.

### 3.4 The per-test record

The minimum field set, from Buildkite's columns, Datadog's per-test record and TestGrid's score:

```yaml
id:            "ctest:panelayout"          # stable key: runner + invocation
name:          "panelayout"
runner:        ctest | unittest | manual
file:          tests/panelayout_test.cpp   # from JUnit <testcase file line>, or a ctest source scan
labels:        [panes]                     # ctest LABELS / pytest markers
first_seen:    2026-09-14
last_run:      2026-09-20T11:03:12Z
last_result:   pass | fail | skip | error | timeout
runs:          37                          # over the retained window
pass_fail_skip: [35, 1, 1]        # counts over that window
duration_p50:  0.022
duration_p95:  0.031
last_failure:  {at: ..., commit: 5263b353, message: "...", excerpt: "..."}
transitions:   3                           # pass<->fail flips, recency-weighted -> flake score
cards:         [SDXE, BXCN]                # reverse index, built from the cards' ## Tests
source_hash:   "sha256:..."                # of the test function/file body
```

`source_hash` is lifted from **asv**, which stores a per-benchmark `version` = a hash of the
benchmark's own code in `results/benchmarks.json` and treats history across a version change as
incomparable. On a test it says: when it changes, the test was edited, so its pass rate and
percentiles from before describe a *different* test, and Check should say "edited, history reset"
rather than a flake score built from two tests. It is also the cheapest staleness inverse — a test
whose `source_hash` is a year old while every file it covers has changed is a review candidate.

### 3.5 Storage: a store, not source

The vendors converge on an append-only execution log with everything above computed as a fold over
it: `{ts, runner, id, result, duration, commit, run_id}`, one object per **test execution**, JSONL.
**Gitignore it.** Bencher, Codecov and Buildkite all treat execution history as a store rather than
source (retention: Buildkite 120 days, Codecov 60), and the numbers that matter get **committed into
the card by Check** — a `### Check <date>` block under `## Tests`, or a thread entry of kind
`evidence` (that kind already exists). That is the Trunk shape, a finding becoming a durable
artefact, which suits a board whose cards are files. The local precedent is `issues/import-state.json`
written by `board.atomic_write` (`board.py:935`), with `issues/.private/` for a per-machine file —
`Board.card_paths()` only walks `*.md` two levels down, so `check` ignores such files already.

### 3.6 What ctest gives for free, and the one real Python constraint

For the C++ half, nearly everything (https://cmake.org/cmake/help/latest/manual/ctest.1.html, cmake
3.28.3 here): **`--output-junit <file>`** emits JUnit XML from the existing 68 tests with zero new
code; `--rerun-failed` reads `LastTestsFailed.log`; `-N` / `--show-only` lists without running (and
`--show-only=json-v1` gives name → command, the honest discovery source here); `--repeat
until-fail:N` is a flake finder and `until-pass:N` a retry, both built in; and `LABELS`, `WILL_FAIL`,
`DISABLED`, `SKIP_RETURN_CODE`, `FIXTURES_*` are already a per-test data model. A Test-suites pane
needs **no new C++ instrumentation** — a parser for two files CTest writes, plus `--output-junit`.

The constraint is the Python half: with no pytest there is no `--durations`, no `--lf`, no
`.pytest_cache`, no testmon, no xdist. The stdlib equivalent is **about 40 lines** — a
`unittest.TestResult` subclass recording `startTest`/`stopTest` timestamps and outcomes and writing
JUnit XML, giving the Python half the same `<testcase name classname time file line>` records ctest
gives the C++ half, one format, no new dependency. (pytest *can* collect `unittest.TestCase` classes
unchanged, so adopting it is a one-dependency decision that unlocks all of the above — the owner's
call against a deliberately stdlib-only backend, not an obvious gap.) Worth copying as the shape of
that state file: `.pytest_cache/v/cache/lastfailed` and `.../nodeids` (all collected ids from the
last run), `nodeids` being what "did this test disappear?" needs
(https://docs.pytest.org/en/stable/how-to/cache.html). **JUnit XML is the interchange**
(https://github.com/testmoapp/junitxml, "the de facto standard format to exchange test results
between tools"): `<testsuites>` → `<testsuite>` → `<testcase>`, with `file` and `line` on
`<testcase>` the link to source and `<properties>` the escape hatch for anything custom, e.g.
`card=SDXE`. Codecov ingests **JUnit XML only** (https://docs.codecov.com/docs/test-analytics).

### 3.7 Test ↔ card linkage, and the reporting discipline

Two tracker products have solved the linkage and disagree usefully. **Xray for Jira** makes tests
*Jira issue types* linked to requirements by the link type "Tests" / "is tested by", and crucially
**the issue key travels in the automation artefact** — a Cucumber feature file carries its Jira key
(https://www.getxray.app/blog/xray-test-management-for-jira); the Relay analogue is `card=SDXE` in
JUnit `<properties>`, a `# card: SDXE` comment, or a ctest `LABELS card-SDXE`. **Zephyr Scale** makes
the link **bidirectional** — on the Jira issue and on the test case's Traceability tab — and ships a
filter for **"issues whose requirements have no tests linked to them"**
(https://zephyrdocs.atlassian.net/wiki/spaces/ZFJCLOUD/pages/1433796610/New+Traceability+Matrix),
which on a board where agents land work and QA verifies it is the verification backlog. Three shapes
matter: the link is **stored on the card**, the id **travels in the artefact**, and **"cards with no
tests" is a first-class view**.

For what Check reports back, the discipline is GitHub's Checks API
(https://docs.github.com/en/rest/checks/runs): `output.{title, summary, text, annotations}`,
annotations carrying `path`, `start_line`, `end_line`, `annotation_level` ∈ notice/warning/failure,
**max 50 annotations per request**, and **at most 3 `actions`** each `{label ≤20, description ≤40,
identifier ≤20}`. *Title + summary + a handful of annotations + at most three buttons* is a good
constraint for a card section; `external_id` is the field that would carry `#R9G7`. The reference
implementation of "JUnit XML → a human summary" is `EnricoMi/publish-unit-test-result-action`
(https://github.com/EnricoMi/publish-unit-test-result-action): counts and duration, a ±delta against
the base branch, and **test changes vs an earlier commit — new tests, removed tests, flaky tests,
newly skipped/unskipped** (capped by `test_changes_limit`, default 10). "Tests removed" and "newly
skipped" are exactly the staleness signals the owner asked for, computed by **diffing two JUnit XML
files** — no server involved.

One last borrowing, from Develocity's Predictive Test Selection
(https://docs.develocity.ai/predictive-test-selection/): it always selects tests that are **new,
recently modified, recently failed, or recently flaky** — a four-line heuristic with no ML, and the
cheap 90% of the coverage-based impact analysis that §6 rates XL for this repo.

## 4. Profiling

### 4.1 The IDE shape

JetBrains is consistent across IntelliJ, CLion and PyCharm and is the shape to copy: **"Profile
'<run configuration>'"** sits in the same dropdown as Run and Debug — **profiling is a mode of an
existing run configuration, never a separately configured thing** — and results land in a Profiler
tool window with four tabs everywhere: **Flame Graph, Call Tree, Method List, Timeline**
(https://www.jetbrains.com/help/idea/read-the-profiling-report.html,
https://www.jetbrains.com/help/clion/cpu-profiler.html,
https://www.jetbrains.com/help/pycharm/profiler.html). Snapshots are **files**: *Run | Open Profiler
Snapshot* reopens one, CLion imports and exports Brendan Gregg folded stacks, PyCharm writes `.pstat`
and has **"Compare With Baseline…"** (green = improved, red = regressed). VS Code's JS story adds the
storage pattern: on stop **the `.cpuprofile` is saved into the workspace** and opened by a viewer
registered for that extension, with **Table / Flame / Left-heavy** views
(https://code.visualstudio.com/docs/nodejs/profiling). The detail that matters most here: **CLion
runs `perf` on Linux and, on first launch, detects that `perf_event_paranoid` must be < 2 and
`kptr_restrict` = 0 and offers to fix them** — with paranoid=4 here, that detect-explain-offer is the
whole interaction. Zed, by contrast, has a DAP debugger but **no profiler integration**
(https://zed.dev/docs/debugger), so there is no competitor shape in this space.

### 4.2 "Profile the project" is three different buttons here

| Target | Command | Artefact | Feasible today? |
|---|---|---|---|
| Python tests | `py-spy record --format speedscope -o <evidence>/tests.speedscope.json -- python3 -m unittest discover -s tests` | speedscope JSON | needs `pip install py-spy` (an aarch64 manylinux wheel exists for 0.4.2); **no root needed** because py-spy is the parent. Zero-dep fallback: `cProfile` + a ~30-line pstats→speedscope converter |
| The GUI (`build/relay`) | `perf record -g --call-graph dwarf -- build/relay`, then `perf script \| stackcollapse-perf.pl` | folded stacks → speedscope | **blocked**: `perf_event_paranoid=4`, `ptrace_scope=1`. A sampling profiler can only profile a process **it launched**, never attach to a running pane. Needs a **stop** control — profiling a GUI is start/do/stop, like Xcode's record button |
| The build | per-target times from the Ninja generator's `.ninja_log` via ninjatracing (https://github.com/nico/ninjatracing), or a `CMAKE_CXX_COMPILER_LAUNCHER` timing wrapper under Make | Chrome trace JSON / a table | **`-ftime-trace` is not available** (g++ 13.3, verified by trying it), so ClangBuildAnalyzer's per-template report is out. Per-target only. The Ninja route also means `-G Ninja` on the shared `build/` — every session's build directory — which is the owner's call |

Given `src/main.cpp` is one translation unit that "takes minutes to compile" (CLAUDE.md), **the
build is the most likely thing "profile the project" means**, and it is the one with no viewer
problem: per-target compile times are a table. Two CMake facts for that path: `cmake
--profiling-format=google-trace --profiling-output=<path>` (both flags required, CMake 3.18+)
profiles **the configure step only** — CMake language execution, not compilation
(https://cmake.org/cmake/help/latest/manual/cmake.1.html) — and `cmake --build` has no profiling
flag at all, it dispatches to the generator. **No de-facto CI action reports build-time
regressions**, unlike runtime benchmarks; large projects roll their own.

### 4.3 The format, and the viewer question

**speedscope JSON** is the interchange to target
(https://github.com/jlfwong/speedscope/blob/main/src/lib/file-format-spec.ts):
`{$schema, shared:{frames:[{name,file?,line?,col?}]}, profiles:[…]}`, a profile being `sampled`
(`samples: number[][]` of frame indices with a parallel `weights: number[]`) or `evented`, carrying
`unit` and `startValue`/`endValue`. It is ~40 lines of Python to emit, py-spy emits it natively
(`--format` is exactly `flamegraph|raw|speedscope|chrometrace`) as do pyinstrument and stackprof, and
it **imports** Chrome/Firefox/Node profiles, pprof, Java Flight Recorder, Instruments traces,
`perf script` output and folded stacks. Behind it sit **pprof protobuf**
(https://github.com/google/pprof) for continuous profiling and **folded stacks** (`frame;frame
count`, https://www.brendangregg.com/flamegraphs.html) as the oldest, universal plain-text fallback.

**Show the table before the flame graph.** Every continuous-profiling product arrived at this
independently: Sentry leads with **"Slowest Functions"** and "Most Regressed/Improved Functions"
widgets; Pyroscope's view toggle is **Flame graph / Top table / Both**
(https://grafana.com/docs/pyroscope/latest/view-and-analyze-profile-data/flamegraphs/); Datadog's
tabs are Flame Graph / Timeline / Call Graph / Timeseries and Table / Profile List
(https://docs.datadoghq.com/profiler/profile_visualizations/); Parca calls its flame graph an icicle
graph and puts a Diff column of sample-count deltas beside it (https://www.parca.dev/docs/).
speedscope's own third mode is named for the pattern — **Sandwich**, *a table of all functions with
their times; selecting a row shows callers above and callees below* — beside **Time Order** and
**Left Heavy**. The functions table is most of the value, and it is a `QTreeView`.

With **no QtWebEngine** there is no in-app HTML viewer, and three honest options. (1) **Vendor the
speedscope bundle and open it in the system browser** with `QDesktopServices::openUrl`: verified MIT,
latest **1.25.0 (2025-12-03)**, **987 KB unpacked, 19 files, one runtime dependency**, and the GitHub
release ships a self-contained zip whose `index.html` opens directly in Chrome or Firefox with no
server — the README states *"The profiles are not uploaded anywhere — the application is totally
in-browser."* Deep-linking is a hash fragment `#profileURL=<url-encoded>&title=<url-encoded>` (needs
CORS if remote). (2) **Draw a native Qt view from the JSON** — matches the panes-not-overlays rule.
(3) Add QtWebEngine — not recommended. A fourth option is a legal question, not a technical one:
**Hotspot (KDAB)** (https://github.com/KDAB/hotspot), a **Qt, GPLv2+** GUI over `perf.data`, whose
licence is compatible with this repo's AGPL-3.0-or-later if a widget were ever vendored.

### 4.4 The valuable variant is "profile and compare"

Profiling once produces a number nobody can interpret; every product's real product is the diff.
Sentry's regression detection is an *issue type*, **"Function Duration Regression"**, on **p95 over
a 14-day before/after window**
(https://docs.sentry.io/product/issues/issue-details/performance-issues/function-regressions/), with
**"Differential Flamegraphs"**, red = slower/new, blue = faster
(https://docs.sentry.io/product/profiling/differential-flamegraphs/); Pyroscope's Diff page merges
one flame graph, **red = slower, green = faster**, normalised as % of total so unequal sample counts
stay comparable; PyCharm has "Compare With Baseline". For a card board the natural framing is
**"this card's commit against its parent"** — CodSpeed's PR comment in a board idiom.

### 4.5 What gets committed

Committing a raw `.speedscope.json` beside a card is unusual but not unprecedented. The one real
precedent for perf data living **in git** is `benchmark-action/github-action-benchmark`
(https://github.com/benchmark-action/github-action-benchmark), which checks out `gh-pages`, updates
`dev/bench/data.js` with each run and **commits it back**, comments on alert and can fail CI
(`alert-threshold` defaults to a deliberately loose `"200%"`). Everyone else externalises: CodSpeed
posts a PR comment with per-benchmark % change (https://codspeed.io/docs); Bencher POSTs Reports
under a Testbed × Branch × Measure → Threshold → Alert model
(https://bencher.dev/docs/explanation/benchmarking/); criterion.rs and pytest-benchmark write into
gitignored directories; attaching a profile to a PR happens only via generic
`actions/upload-artifact`. So the sanctioned shape is *"a small JSON of numbers committed, plus a
comment with the delta"*, not "20 MB of samples committed": **the summary table into the card** (top
functions, self/total, wall time) and **the raw profile under `docs/qa_evidence/<date>-<slug>/`**
referenced from `links.evidence` — a convention this repo already runs on.

## 5. How agent products surface verification

Seven products were surveyed — Warp, Cursor, Claude Code, Devin, Codex, Jules, Copilot — and **nobody
parses test output into a per-test panel. Seven for seven.** The aggregated signal is always **CI
checks on a PR**, and the in-product surface is either raw terminal output (Warp Blocks, Codex logs,
Copilot's session log) or a **visual artifact**: Devin's chaptered annotated screen recording, Jules'
Playwright screenshot, Cursor's PR artifacts ("screenshots, videos, and log references… so you can
see exactly what changed and **how the agent verified its work**",
https://cursor.com/docs/cloud-agent/capabilities), Warp Factories' per-agent screenshot or video.
**If Relay renders parsed per-test results on a card, that is a differentiator rather than catch-up.**

Two evidence atoms are worth copying verbatim. **Codex's citations**: *"Codex provides verifiable
evidence of its actions through citations of terminal logs and test outputs, allowing you to trace
each step taken during task completion"* (https://openai.com/index/introducing-codex/) — every claim
in the summary links to the bytes that prove it, which is what a `needs-verification` column needs.
And **Devin's pre-declared expectation**: its timeline annotations are written *before* the action,
TDD-style, because *"it's much harder to rationalize an unexpected result as a pass"*
(https://cognition.com/blog/testing-development). Together they turn "agent says done" into something
a QA session can check mechanically. Chromium's flake sheriffs reached the same conclusion from the
other end: paste the log into the bug rather than linking it — links rot, and a card in git does not.

**A separate critic or verifier agent is now standard, and every vendor gives the same reason: the
writer must not grade itself.** Jules ships two, a Critic Agent inside generation and, since
2026-01-26, a Planning Critic — note the causality, **the critic exists because plan approval was
made optional** (https://jules.google/docs/changelog/). Cursor documents a **Verification agent**:
*"independently validates whether claimed work was actually completed… You are a skeptical
validator… Do not accept claims at face value"* (https://cursor.com/docs/subagents). Claude Code's
best practices name the gap — *"Claude stops when the work looks done. Without a check it can run,
'looks done' is the only signal available, and you become the verification loop"* — with four
escalating gates and the instruction to *"have Claude show evidence rather than asserting success:
the test output, the command it ran and what it returned, or a screenshot"*
(https://code.claude.com/docs/en/best-practices). Copilot gets a second opinion from Copilot code
review before finishing the PR; Warp has an explicit critic pattern.

**Reviewer bots converge on tiered severity plus a status check that does not block.** Cursor Bugbot
publishes a check run whose conclusion defaults to **`neutral`**, findings tagged high/medium; Claude
Code's Managed Code Review uses **🔴 Important / 🟡 Nit / 🟣 Pre-existing** and ends its Details text
with machine-readable `bughunter-severity: {"normal":2,"nit":1,"pre_existing":0}`
(https://code.claude.com/docs/en/code-review); Devin Review splits severe/non-severe and posts a
commit status beside CI; Codex **"flags only P0 and P1 issues"**; Copilot labels High/Medium/Low.
OpenAI states the rule plainly — tune for **precision over recall**, because "a system that is slow,
noisy, or cumbersome will be bypassed" (https://alignment.openai.com/scaling-code-verification/), so
a `needs-qa` column should carry a **severity tally, not a boolean**.

**Only Warp Factories has a true board.** A work item is "a single request the factory acts on… **It
keeps its identity from intake to handoff**, however many agents contribute to it along the way"; a
foreman dispatches Triage / Spec / Implement / Review agents and **sends work back to an earlier
agent when revisions are needed** (https://docs.warp.dev/factories/how-factories-work/). Everyone
else has a list plus the GitHub PR list — Devin's `blocked` as a first-class status, Cursor's tiny
ACTIVE/IDLE/ARCHIVED (https://cursor.com/docs/cloud-agent/api/endpoints), Codex rows with a
`+57 / −27` diff-size badge, Claude Code's `claude agents` columns Pinned / Ready for review / Needs
input / Working / Completed with a PR label coloured by check status
(https://code.claude.com/docs/en/agent-view). **The Switchboard's columns are already a superset of
every status vocabulary above**; what it lacks is the per-card *detail* those products show — plan,
step log with tool calls, diff, and log citations behind every claim. Two Claude Code mechanisms are
the most transferable of anything here, and §6 returns to both: the **`TaskCompleted` hook that exits
2 to veto** a task being marked complete (https://code.claude.com/docs/en/hooks), and agent teams'
**file locking on claim with dependency-blocked claims**.

## 6. What other tooling fits, ranked

Rated for **fit** with a Markdown-cards-in-git board that agents write to, and for **cost** in this
codebase (stdlib-only Python backend, Qt widgets, no network dependencies, panes not overlays).
"Placement" is the proven shape: per-card section, board pane, or a button.

| # | Tool | What it shows / does | Copy from | Placement | Fit | Cost |
|---|---|---|---|---|---|---|
| 1 | **`attachments:` on the card** | `{title, subtitle, url, iconUrl, metadata}` deduped by **url as the idempotency key**, so any producer can write and rewrite without storing ids | Linear attachments (https://linear.app/developers/attachments) | per-card section | ★★★★★ | **XS** — one front-matter key; everything below writes through it instead of inventing a field each |
| 2 | **Test suites pane + card Tests/Check** | the owner's (a) and (b) | Buildkite columns + TestGrid grid + nextest vocabulary | pane + per-card section + button | ★★★★★ | **M** — ctest data is free, Python needs a 40-line JUnit writer, the pane is a `QTableView` |
| 3 | **Flaky tests become cards** | one card per flaky test, auto-filed on alarm, **auto-moved to done on recover**; sorted by *PRs impacted*; failures grouped by unique reason | Trunk + Buildkite monitors (https://buildkite.com/docs/test-engine/workflows/monitors) + LUCI `BugManagementPolicy` (separate activation/deactivation thresholds, so it cannot flap) | board pane row → card | ★★★★★ | **S**, once (2) stores history |
| 4 | **`issues/dependencies.md` — a Renovate dashboard** | one generated Markdown file, section headings as columns, **a checkbox is the entire interaction**, payload in an HTML comment; `dependencyDashboardApproval` = nothing happens until ticked | Renovate Dependency Dashboard (https://docs.renovatebot.com/key-concepts/dashboard/) | board pane over a generated file | ★★★★★ | **S** — pure Markdown-in-git; the scanner (`pip-audit`, OSV) is the only new code |
| 5 | **Release notes from cards** | add `release_type`, `breaking`, and a **user-visible summary distinct from the body** (Linear's documented failure mode: engineer-written titles make bad notes); a Release pane groups done-since-tag by type; bump = max | Changesets (a Markdown file in git *is* the changeset), Linear Releases, `.github/release.yml` label grouping | board pane + 3 card fields | ★★★★★ | **S** |
| 6 | **Per-card check/CI section** | `status` + `conclusion` + `output{title, summary, annotations[]}` + **≤3 actions**; `external_id` carries the card id | GitHub Checks API; Linear's per-target-branch status rules | per-card section | ★★★★☆ | **S** if it renders local command runs; **M** if it syncs GitHub |
| 7 | **Hotspot pane (churn × health)** | columns `change_frequency`, `lines_of_code`, `number_of_defects`, `code_health.{current,month,year}_score`; per-row **X-Ray** (function-level, on demand) and **make a card** | CodeScene's REST shape (https://docs.enterprise.codescene.io/versions/5.1.10/integrations/rest-api.html); `code-maat` CSV as the OSS ancestor; `git log --numstat` is all churn needs | board pane | ★★★★☆ | **S** for churn-only; **L** for a real health score |
| 8 | **TODO/FIXME pane with write-back** | ripgrep + `git blame` for age/author; one action, **promote to card**, which writes `// TODO(#AB12): …` back into the source; deleting the comment closes the card | todo-to-issue-action's `INSERT_ISSUE_URLS` — the id lives in the code, no side database (https://github.com/alstr/todo-to-issue-action); imdone-core (tag = column); PEP 350's `t:` field (https://peps.python.org/pep-0350/) | board pane + button | ★★★★☆ | **S** |
| 9 | **Benchmark compare on a card** | "compare this card's commit against its parent": per-benchmark Δ with a significance test | Google Benchmark `tools/compare.py` (Mann-Whitney U, needs `--benchmark_repetitions=9`); criterion's ±2% noise threshold wording; CodSpeed's PR comment | button → per-card section | ★★★★☆ | **M** — needs benchmarks to exist first; **diff-shaped, so no history store** |
| 10 | **Coverage patch % per card** | "patch coverage" = coverage of *only the lines this card changed*, plus Codecov's third status `changes` (coverage moving **outside** the diff) | Codecov patch/project/changes statuses (https://docs.codecov.com/docs/commit-status); components as path filters for `src/` vs `backend/` | per-card badge + board pane trend | ★★★☆☆ | **M** (Python) / **L** (C++: gcov, and `lcov -c -i` first or files no test touched vanish) |
| 11 | **Docs staleness** | `owner` + `reviewed` + `covers: [globs]` in doc front matter vs `git log -1 --format=%cI -- <paths>`; a check listing outdated docs | Google's g3doc freshness block (https://abseil.io/resources/swe-book/html/ch10.html); Swimm's auto-sync vs mark-outdated split and its "Approve Auto-sync" button | button + board pane | ★★★☆☆ | **XS** — and **no maintained tool does exactly this**, so it is cheap novelty |
| 12 | **Profile button** | the owner's (c) | JetBrains "Profile <run config>" + snapshot files + Compare With Baseline; speedscope as the format | button + pane | ★★★☆☆ | **M** for the Python/build table; **L** for a native flame graph; blocked for GUI runtime until a sysctl |
| 13 | **Ownership / last-toucher** | rank authors by lines touched in the changed hunks' *previous* versions | Gerrit `reviewers-by-blame` — the only open implementation; GitHub's own ranking is undocumented beyond one sentence | button | ★★☆☆☆ | **XS** — but marginal on a single-owner repo |
| 14 | **Per-file lint debt board** | ranked table of files by lint/type debt, CodeScene-shaped | **nothing ships this** — `ruff --statistics` groups by *rule*, Sonar's Issues view is a flat stream; mypy's `--lineprecision-report` is the only per-file debt measure found | board pane | ★★☆☆☆ | **M**, and it is invention — take it last, in the hotspot shape |
| 15 | **Test-impact analysis** | map test → source files it executed; run only the intersection | Azure DevOps TIA (https://learn.microsoft.com/en-us/azure/devops/pipelines/test/test-impact-analysis), pytest-testmon's per-block checksums (https://testmon.org/blog/determining-affected-tests/), Develocity/Launchable | button on a card | ★★☆☆☆ | **XL** here — no pytest, no clang, so per-test coverage costs a custom runner on both halves. **The cheap 90% is Develocity's rule with no ML: always run tests that are new, recently modified, recently failed or recently flaky** |

**Two mechanisms worth lifting regardless of which of the above ship.** A **`TaskCompleted`-style
veto on entering `done`** — Claude Code's hook exits 2 to refuse a task being marked complete, so
the board analogue is that a card cannot leave `needs-verification` while its `## Tests` section has
a *gone* or *never run* entry, or while its last check failed; that turns Check from a report into a
gate, which is the point of the column. And **file locking on claim, with dependency-blocked
claims** (agent teams), relevant to `board_claim` and to the shared-checkout problem
`scripts/land.py` solves by other means.

**Two card-format additions the research argues for independently.** **`Specifications` and
`Forbidden Actions` sections on `plan` cards**: Devin's Playbooks have fixed sections — Overview,
**Procedure**, **Specifications** ("what should be true once Devin is done" — postconditions),
Advice and Pointers, **Forbidden Actions**, What's Needed From User
(https://docs.devin.ai/product-guides/creating-playbooks) — where Relay's plan cards carry Goal /
Findings / Steps / Risks / Verify, with no *Forbidden Actions* and no machine-checkable
*Specifications*, which is what a QA session checks. And **a user-visible release summary field**
distinct from the card body (row 5): the one field that turns a done column into a changelog, fixing
the failure mode Linear documents — issue titles written for engineers make bad release notes.

## 7. Unverified / flagged

**Local facts verified, not inferred:** `perf_event_paranoid=4` and `ptrace_scope=1` were read from
`/proc`; `-ftime-trace` and `-fproc-stat-report` were rejected by the installed g++ 13.3;
`import pytest` fails; `CTestCostData.txt` exists with 19 rows; there is no QtWebEngine in
`CMakeLists.txt`; py-spy has an aarch64 manylinux wheel (0.4.2) and **Scalene does not**. The
codebase line numbers were read in a tree three other sessions were editing — anchors plus drift.

**Not verified in the external survey, and relied on nowhere above.** Profiling: IDEA's
snapshot-import wording for external `.jfr`; whether PyCharm's Call Graph needs graphviz; CLion
Callgrind integration (only Memcheck is documented); whether PyCharm profiling is Professional-only
after the 2025 unification; the column labels in Sentry, Datadog and Pyroscope (self/total semantics
confirmed, headers not quotable); Austin's "MOJO" format; ninjatracing's `--embed-time-trace`
spelling; Hotspot's Qt version; an npm package for the Perfetto UI. Trackers: Linear's branch-name
template and whether CI check state shows on an attachment; GitHub Projects v2 automations beyond
"item closed → Done"; CodeScene's hotspot window, its cutoffs and a schema for delta analyses;
CodSpeed's use of cachegrind. Agent products: Cursor's row columns; Jules' per-row fields, status
enum and branch naming; Copilot's agents-page columns; Codex's list columns and best-of-N range;
whether Devin polls CI unprompted. Jules' "9.5% reduction in task failure rates" is its own number.

**Inference, flagged as such:** that `py-spy record … -- python3 -m unittest discover` works is an
inference — pyinstrument documents `-m pytest`, py-spy's README never mentions a test runner, though
the target is just a CPython process. Nothing in §3 or §4 was run against this repo's suite.

**Decisions this research cannot make, because they are the owner's.** Whether to adopt pytest (one
dependency against a deliberately stdlib-only backend, unlocking durations, `--lf`, testmon and xdist
at once); whether to switch the shared `build/` to `-G Ninja`, which is what makes build profiling
possible and changes every session's build directory; whether to relax `perf_event_paranoid`, without
which GUI runtime profiling cannot happen at all; and whether a vendored ~1 MB MIT speedscope bundle
opened in the system browser is acceptable, or the flame graph must be painted in Qt to stay inside
the panes-not-overlays rule.
