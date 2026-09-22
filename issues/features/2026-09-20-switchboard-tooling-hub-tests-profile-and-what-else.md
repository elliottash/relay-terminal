---
id: 7BM4
type: work
status: needs-verification
labels: [feature, switchboard, tests, profiling]
component: [gui, worker]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
rank: zzzzzzzzzzzzzzzy
created: '2026-09-20'
source: owner, Claude Code session, 2026-09-20
links: {plans: [], commits: [c8b0a8d2, 912ab11a, eb0a9b76, 4ac7b57d, 71355e7a, db035cfd, 5e306871, 8ad92248, 6348ebef, 6bb87f04, cf7d1a5f, 97a019fc, 0152697f, f56a6ea0, 432a17f0, a10bb2a4, fd45d116, f86266da, b81c861a, fd7a9a71, 936bc28a, 4818709e, 2bd23344, e34b1df6, 21f5b001, 20c4e29e, d8b255157167a80c2693833019a8964bce6d0330, 0014fa103f756ab06b0fd4670ec256626eb19ac3, 34030318c2d20632c979787895d765026f37083f, b15421d6f71690c72cd94d12b6c582f1c404ecf5], evidence: [docs/qa_evidence/2026-09-21-verify-7BM4/report.md, docs/qa_evidence/2026-09-20-test-suites-pane/, docs/qa_evidence/2026-09-20-card-tests-check/, docs/qa_evidence/2026-09-20-profile-button/, docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md, docs/qa_evidence/2026-09-21-7BM4-repair/, docs/qa_evidence/2026-09-21-verify-7BM4/, docs/qa_evidence/2026-09-21-7BM4-attribution/], related: [R9G7, SDXE, PF4K, YZ8G, 561P], github: null}
---
# Switchboard as the project's tooling hub: a Tests section with Check, a Test suites pane, a Profile button, and what else fits

## Issue
how about: add a tests section to a card, that lists the associated tests. that has a check button next to it, which will check them for staleness / slowness.

do you also think there should be a test suites pane, potentially as an auxiliary pane attached to the switchboard. that lists tests and stats (you can click to get failure histories, exec time, etc).

why not have a profiling button as well? so you click a Profiling button from the swtichboard, and it profiles the project.

do you understand what i am doing? are there other project / SWE tooling that we can smoothly add to the swtichboard? do research on what other tools do

## Decisions
- 2026-09-20, owner, on #AQ6X: "yes, make those reconciliations, lets go the signals route" — flaky-tests-become-cards is struck from this card; the run history gains a `tree_digest` field (empty on a clean tree, else a short digest of `git diff HEAD`), so an execution names the code it ran on in this shared checkout.
- 2026-09-20, owner, on the four questions below: "i agree with those recommendations." So: the
  Profile button asks for its target and ships the build first; Check is a gate on leaving
  `needs-verification`, with a recorded override; the pane lists the attached project's tests
  only; after the three surfaces come the dependencies file, release notes, hotspots and TODO
  mining. (Flaky-tests-become-cards was struck on 2026-09-20: it is signal promotion on #AQ6X.)
- 2026-09-20, owner: "can you help me set up my machine and sphinxpad for the test suites /
  profiling system." Both machines are runners: spark (aarch64, Ubuntu 24.04, g++ 13, Qt5) and
  sphinxpad (x86_64, Ubuntu 26.04, clang, Qt5 and Qt6). Setup is `scripts/relay-tooling-setup`.

- 2026-09-20, owner, on recording profiling (this card's `t:ea`). The question started as
  *"should we also keep a record of whether an implementation has been profiled"*, passed through a
  per-card `profiled_by` stamp, and landed on **no card-indexed profiling state at all**:
  *"yeah maybe not"*, and then the reason — **"the issue there is, many old cards become
  irrelevant to a project."**
  1. **No per-card profiled stamp, and no new front-matter field.** A `verified_by`-style stamp was
     considered and dropped. `verified_by` is written once and never invalidated when the feature
     later changes; a profile stamp that decayed would turn a card from a record of work into a
     live view of the code, and closed cards would start reopening.
  2. **No recurring queue is ever built by scanning cards.** Cards age out: when this board was
     last audited against the code, 8 of 43 open cards described work that was already done.
     Anything asked repeatedly over a project's life must be indexed on an object whose lifetime
     matches the code, not on work items.
  3. **The benchmark registry is that object.** A benchmark is a test whose result is a number
     rather than a pass, so it belongs in the Test suites pane and the `## Tests` section under the
     same source-hash staleness already decided here for tests. *"Profile all unprofiled cards"*
     becomes *"run the stale benchmarks"*. The owner's own brief for Check already says
     "staleness / slowness", so the slow dimension was in scope from the start.
  4. **Card → benchmark is allowed; benchmark → card is refused.** A card may cite a benchmark as
     provenance, one line in `## Tests`. Nothing queries from that side, so when the card stops
     describing anything the citation dies quietly with it. The registry must never hold pointers
     back to cards, or the object meant to outlive them inherits their rot.
  5. **Profiling generates cards rather than being tracked by them.** A profile run that finds a
     hotspot files a card, closed when the hotspot goes: forward in time, always relevant,
     self-closing. Identical machinery to the flaky-tests-become-cards item already queued here,
     pointed at a different signal.

## Plan
**Goal** — The Switchboard becomes the project's control panel, not only its tracker: every
recurring engineering activity (running and auditing tests, profiling, later dependencies,
releases, hotspots) is a button or a pane beside the board, and what it produces is written into
cards, so the agents that claim cards and the QA sessions that verify them read the same numbers
the owner sees. Three concrete surfaces ship first: a `## Tests` section on a card with a
**Check** button, a **Test suites** pane attached to the Switchboard, and a **Profile** button on
the board's tool row.

**Research** — `docs/SWITCHBOARD-TOOLING-RESEARCH.md` (2026-09-20): what VS Code, JetBrains,
Buildkite, Datadog, Trunk, Codecov, ctest, nextest, TestGrid, Sentry, Pyroscope, speedscope,
Renovate, CodeScene and the seven agent products (Warp, Cursor, Claude Code, Devin, Codex, Jules,
Copilot) do, and what this repo already has. The findings below are the ones that shape the plan.

**Findings**

1. Cards already parse body sections generically (`board.section_span`, `board_tools.section_headings`)
   and `board_update_card {append_section|replace_section}` writes any `## ` heading, so a
   `## Tests` section costs one word in `AGENT_SECTIONS` (`backend/relay_core/board_tools.py:114`).
   `links` is an untyped bag and `check` accepts `links.tests` today. The GUI has **no** section
   renderer — `CardDetail::render` sets the whole body as Markdown (`src/BoardPane.cpp:2661`) and
   the only section-aware widget is `hasPlan()` + the verify line (`:1452`, `:1726`); the Tests
   strip copies that.
2. There is **no per-test data anywhere**. The only trace is CTest's gitignored
   `build/Testing/Temporary/CTestCostData.txt` (name, runs, mean seconds) and `LastTestsFailed.log`.
   The Python suite (3,350 cases, stdlib `unittest`, no pytest installed) is **one** ctest entry,
   `backend-and-bash`, so per-test results on the Python side need a ~40-line `TestResult` that
   writes JUnit XML; `ctest --output-junit` already works (CMake 3.28).
3. A background runner with a wire protocol and a GUI model already exists:
   `jobs.JobTable` (`backend/relay_core/jobs.py:82`, process groups, stop, streamed output) and
   `src/JobsPanel.h`'s headless `JobsModel`/`JobRow`. That is the shape of "a test with a duration
   and a state" and the runner every button should use.
4. Board-level buttons live in the helper panel's tool row (`HelperChatPanel::addToolWidget`,
   `src/HelperChat.cpp:800`); the two templates are `board_check` → `board_problems` →
   `showFindings` (fast, deterministic list of `{path, message, severity}`) and `board_cleanup`
   (request → tagged turn → streamed progress → summary). Check copies the first; Profile the second.
5. A new pane type is a ten-step checklist (`ToolPane::Kind`, `defaultPaneType`, `panestatus::kinds`,
   `node()`/`buildNode`, a Keymap action) and `openInternalsPane`/`linkInternalsPane`
   (`src/RelayWindow.h:3998-4048`) plus `listenToHelper`/`sendToHelper` (`:5042`, `:5052`) are the
   attachment pattern: one board worker per tab, broadcast to every listener.
6. "Stale" is six separable conditions; four need no test execution: **gone** (in the card's list,
   absent from `ctest -N` / collection), **never run** (absent from history), **skipped forever**
   (`DISABLED` / `@skip`, or skipped in every stored run), **orphaned** (the card's commits touched
   files no listed test covers). Slow and flaky come from history: p50 and p95 (a bimodal test is
   invisible in a mean), `reliability = pass/(pass+fail)` (Buildkite), flaky = pass and fail at
   the **same commit** (Datadog), flake score = recency-weighted pass↔fail transitions (TestGrid).
   asv's per-benchmark source hash applies to tests: when the test's source changes, its history
   resets rather than producing a flake score built from two different tests.
7. "Profile the project" is three different buttons in this repo. **Corrected 2026-09-20 after
   reading #PF4K:** `perf_event_paranoid=4` on both machines blocks plain `perf`, but passwordless
   `sudo -n perf` works on both, so the app target is not blocked — the tool runs `sudo -n perf`,
   hands the output file back to the user, and explains itself only where sudo asks for a
   password. The build has no `-ftime-trace` on spark's g++ 13.3 and `build/` is Unix Makefiles
   (no `.ninja_log`), so on spark it is per-target times from a compiler-launcher timer; sphinxpad
   has clang, so the per-header and per-template report (ClangBuildAnalyzer) runs there. The
   Python tests profile with `py-spy` (no root) or stdlib `cProfile`. Every profiling product shows a **table before the flame graph**, and with
   no QtWebEngine the table is a `QTreeView` while the flame graph opens speedscope in the browser.
8. None of the seven agent products parses test output into a per-test panel: they show raw logs
   or a recording. Parsed per-test results on a card are a differentiator. Two atoms to copy:
   Codex's citations from the summary to the log bytes, and Devin's pre-declared expectation.
9. The board-native ideas from elsewhere are not dashboards: Renovate's dependency dashboard is
   one Markdown file where a checkbox is the only verb; Trunk, Buildkite and LUCI file one issue
   per flaky test and close it when it recovers. Both are "a tool writes cards", which the
   Switchboard's agents already do. (Superseded for tests: a failing or flaky test is a *signal*,
   #AQ6X, a keyed record over this card's history file, promoted to a card only on impact.)

**Design**

- *(a) `## Tests` on a card.* A Markdown list, one line per test: an **invocation** plus an
  optional source path (`` `ctest -R panelayout` — tests/panelayout_test.cpp ``;
  `` `tests/test_board_chat.py::BoardChatTests::test_steer` ``; `manual: docs/qa_evidence/...`).
  Hand-editable, diff-friendly, no new format; `links.tests` as a machine mirror only if a stable
  key turns out to be needed. **Check** is *list, not run*: it answers gone / never run / skipped
  forever / orphaned from collection and git alone, then slow / flaky from history, and offers at
  most three actions — **Run these**, **Add the tests this card's commits touched**, **Open the
  failing one**. Its result is appended to the card as a dated `### Check` block under `## Tests`
  (a durable artefact, the Trunk shape), not a toast. Once it exists, it is the gate: a card does
  not leave `needs-verification` while its Tests section has a gone or never-run entry or a failed
  last check (the Claude Code `TaskCompleted` veto, in board form). Silent when nothing moved.
- *(b) Test suites pane.* A sibling pane opened from the Switchboard's tool row and the palette
  (`tests.open`; no default key — Ctrl+Shift+T is New tab in every preset), attached through `listenToHelper`/`sendToHelper`. Default view is the
  TestGrid grid: rows are tests, columns the last N runs newest-left, cells pass/fail/skip/never;
  beside it Buildkite's columns trimmed to reliability %, p50, p95, runs, last run, cards; header
  line in nextest's words (`68 tests · 64 passed (2 slow, 1 flaky) · 3 never run · 12.4 s`); sort
  by flake score, p95, name, last failure; filter over name and label. Click a row for its
  history, last failure excerpt, linked cards and file. Row actions: Run, Rerun until fail
  (`ctest --repeat until-fail:10`), Open source, **Make a card**, Attach to card. One board-level
  report from day one: *cards with no tests*, which is the verification backlog. The pane
  replaces the hand-kept inventory in `docs/VALIDATION.md`.
- *(c) Profile button.* One button on the tool row that asks which target (the build, the Python
  tests, the app), because they are different things here; JetBrains' shape: the result is a
  snapshot file under `docs/qa_evidence/<date>-profile-<target>/` plus a native table pane
  (function or target, self, total, wall) and an "Open flame graph" that hands the speedscope
  JSON to the browser. When run from a card, the summary table is appended to the card and the
  raw file linked from `links.evidence`. The valuable variant is *compare*: this card's commit
  against its parent, red slower and green faster. The app target detects the paranoid sysctl and
  explains rather than failing.
- *Storage.* Per-execution history is an append-only JSONL under the board's private root
  (`issues/.private/tests/history.jsonl`, gitignored), one object per test execution
  `{ts, runner, id, result, duration, commit, run_id}`; every statistic is a fold over it, and
  the numbers that matter are committed into cards by Check. Discovery is a bounded, offline
  `test_probe.py` in the `project_probe.py` idiom (`ctest --show-only=json-v1` and the
  `tests/test_*.py` glob), so any project with ctest or unittest works.
- *Where the buttons go.* All in the helper panel's tool row beside Check and Clean up
  (SWITCHBOARD-DESIGN §4.13); the pane header stays untouched. Three or four buttons is the cap:
  past that, the row becomes a menu.

**Steps** — one Opus subagent per area, each landing its own files through `land.py`; the first
three phases touch no file that #R9G7 (deliver workflow) currently holds.

1. **Backend: discovery, history, results** (new `backend/relay_core/test_probe.py`,
   `test_history.py`, `tests/junit_result.py` writer, `tests/test_test_probe.py`,
   `tests/test_test_history.py`, `scripts/test.sh` gaining a JUnit output flag, `.gitignore`).
   Discovery of ctest and unittest tests; JUnit XML in from both runners; the JSONL store and its
   fold (p50, p95, reliability, transitions, source hash, last failure); a `check_card(tests,
   commits)` function returning the four staleness verdicts plus slow/flaky, pure and tested
   without the GUI.
2. **GUI: Test suites pane** (new `src/TestSuitesPane.{h,cpp}`, `tests/testsuites_test.cpp`,
   `CMakeLists.txt`, `src/PaneStatus.*`, `src/Keymap.h`, `src/WindowState.cpp`). Headless model
   in the `JobsModel` split, grid + columns + detail, sort and filter, row actions sending through
   the helper seam. Last, in its own commit: the ~10 lines in `src/PaneChrome.h` (`ToolPane::Kind`)
   and `src/RelayWindow.h` (the opener), after #R9G7 lands.
3. **Worker protocol for tests** (`backend/relay_core/tests_protocol.py` new, wired from
   `board_protocol.py` in one line each; `docs/AGENT-SESSIONS-PROTOCOL.md` §31): `tests_list`,
   `tests_run {ids}` through `JobTable` with streamed `tests_event`s, `tests_history {id}`,
   `tests_check {card}`; an agent-facing `board_tests_check` and `tests_run` tool gated to the
   page agent and the pane's agent, and two lines in `board_policy.md`.
4. **Card Tests section and Check** (`board_tools.py` AGENT_SECTIONS, `src/BoardPane.cpp`
   CardDetail strip and button, `tests/boardpane_test.cpp`, `docs/SWITCHBOARD-FORMAT.md`). The
   dated `### Check` block, the three actions, and the `needs-verification` gate. Fourth because
   it needs the four hottest files; scheduled after #R9G7 has landed.
5. **Profile** (new `scripts/relay-profile` in the `relay-debug` shape, `docs/PROFILING.md`,
   the button in `src/HelperChat.cpp`, a `profile_run` message copying `board_cleanup`'s
   request→stream→summary, the table pane reusing the Test suites pane's table widget). Build
   target first (works today), then Python tests, then the app with the sysctl explanation.
6. **Docs and QA**: `docs/SWITCHBOARD-DESIGN.md` §4.14 (tool row buttons, Tests section, the
   pane), `docs/VALIDATION.md` inventory replaced by a pointer to the pane, evidence under
   `docs/qa_evidence/2026-09-2x-test-suites-pane/`, and the `QA checklist` on this card.

**Questions for the owner** (each with a recommendation)

1. Does "profile the project" mean the build, the app, or the test suite? Recommendation: the
   button asks, and ships the build target first because it works on this machine today and is
   what "minutes to compile" points at; the app target follows once `perf` is allowed.
2. Should Check become a gate on leaving `needs-verification`, or stay a report? Recommendation:
   a gate, with a one-click override recorded in the thread, because a report nobody must read
   is what every CI product ends up ignoring.
3. Should the pane list only this project's tests, or every attached project's? Recommendation:
   the attached project only, like the board itself.
4. Which of the "what else" list follows first? Recommendation, in order: flaky tests become
   cards (nearly free once history exists), a Renovate-shaped `dependencies.md` with checkboxes,
   release notes from cards with a user-visible summary field, then churn hotspots and TODO
   mining with write-back. Coverage and test-impact analysis are last: costly here without
   pytest or clang.

## Tasks

- [x] Backend: test discovery, JUnit in, JSONL history and fold, `check_card` verdicts, tests <!-- t:5f -->
- [x] GUI: Test suites pane with headless model, grid, columns, detail, row actions, registration <!-- t:xe -->
- [x] Worker protocol §31: tests_list / tests_run / tests_history / tests_check, agent tools, policy lines <!-- t:pg -->
- [x] Card: `## Tests` section strip, Check button, dated `### Check` block, three actions, verification gate <!-- t:q5 -->
- [x] Profile: scripts/relay-profile, button, streamed run, table pane, evidence under qa_evidence <!-- t:ea -->
- [x] Docs and QA: SWITCHBOARD-DESIGN §4.14, VALIDATION inventory pointer, protocol §31, QA checklist <!-- t:em -->

## Tests
- `ctest -R testsuites` — tests/testsuites_test.cpp
- `ctest -R cardtests` — tests/cardtests_test.cpp
- `ctest -R profilepane` — tests/profilepane_test.cpp
- `ctest -R windowstate` — tests/windowstate_test.cpp
- `tests/test_test_probe.py`
- `tests/test_test_history.py`
- `tests/test_junit_runner.py`
- `tests/test_tests_protocol.py`
- `tests/test_profile_protocol.py`
- `tests/test_relay_profile.py`
- manual: docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md

### Check 2026-09-21 21:37
- missing-evidence · ctest:testsuites — no run of ctest -R testsuites for this revision, from any host, and no attached result
- missing-evidence · ctest:cardtests — no run of ctest -R cardtests for this revision, from any host, and no attached result
- passed · ctest:profilepane — ctest -R profilepane passed for this revision on spark-dcc9, 2026-09-22T01:37:20Z
- passed · ctest:windowstate — ctest -R windowstate passed for this revision on spark-dcc9, 2026-09-22T01:12:46Z
- missing-evidence · unittest:tests.test_test_probe — no run of tests/test_test_probe.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_test_history — no run of tests/test_test_history.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_junit_runner — no run of tests/test_junit_runner.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_tests_protocol — no run of tests/test_tests_protocol.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_profile_protocol — no run of tests/test_profile_protocol.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_relay_profile — no run of tests/test_relay_profile.py for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md
- notice · ctest:testsuites — ctest -R testsuites has never run here
- notice · ctest:cardtests — ctest -R cardtests has never run here
- notice · unittest:tests.test_test_probe — tests/test_test_probe.py: 20 of 20 never ran here (test_names_ids_labels_and_disabled, test_source_from_the_command_path_convention, test_source_from_add_executable_when_the_name_does_not_match…)
- notice · unittest:tests.test_test_history — tests/test_test_history.py: 68 of 69 never ran here (test_append_and_read_round_trip, test_a_batch_is_one_write_and_appends_never_rewrite, test_every_line_is_one_json_object_in_the_wire_shape…)
- notice · unittest:tests.test_junit_runner — tests/test_junit_runner.py: 17 of 17 never ran here (test_writes_one_testcase_per_test_with_outcomes, test_classname_file_and_line_point_at_the_source, test_classname_matches_the_probes_unittest_id…)
- notice · unittest:tests.test_junit_runner — tests/test_junit_runner.py: 2 of 17 are skipped for good (test_expected, test_unexpected)
- notice · unittest:tests.test_tests_protocol — tests/test_tests_protocol.py: 101 of 101 never ran here (test_list_carries_the_contract_keys_and_nothing_surprising, test_cards_without_tests_lists_work_in_flight_only, test_a_cards_tests_section_names_the_test_on_its_row…)
- notice · unittest:tests.test_profile_protocol — tests/test_profile_protocol.py: 14 of 14 never ran here (test_started_progress_finished, test_second_run_is_refused_while_one_is_in_flight, test_stop_ends_the_run…)
- notice · unittest:tests.test_relay_profile — tests/test_relay_profile.py: 21 of 21 never ran here (test_ninja_log_becomes_a_per_output_table, test_an_output_built_twice_counts_once_at_its_last_time, test_an_empty_log_is_a_sentence_not_a_traceback…)
- notice · unittest:tests.test_relay_profile — tests/test_relay_profile.py: 11 of 21 are skipped for good (test_it_succeeded_and_printed_where_it_wrote, test_the_five_files_are_there, test_the_summary_table_names_every_object…)
history: thread
### Implementer rerun 2026-09-21
- `python3 -m unittest tests.test_board_tools.SignatureTests` — 11 passed.
- `python3 -m unittest tests.test_board_tools` — 257 passed.
- Six listed backend modules — 237 passed.
- Four listed CTest suites — 4 passed.
- `python3 scripts/relay-board.py check` — no diagnostic for #7BM4; repository-wide pre-existing diagnostics remain.
## QA checklist
Independent verifier: gpt-6-astra via Codex (Relay a2), 2026-09-21 America/New_York.
Checked revision: `0241d05ef19393e84c0ef98655c9687466a92efc`, clean archive export;
Qt 5 / Linux aarch64. Evidence: `docs/qa_evidence/2026-09-21-verify-7BM4/`.
The prior implementer's checklist was not used as verification evidence.

- `Done means`: **missing evidence** — this section is absent; no predeclared outcome lines exist to check.
- `ctest -R testsuites`: **passed** — fresh targeted binary, `gui-tests.log` (1/4).
- `ctest -R cardtests`: **passed** — fresh targeted binary, `gui-tests.log` (4/4).
- `ctest -R profilepane`: **passed** — fresh targeted binary, `gui-tests.log` (2/4).
- `ctest -R windowstate`: **passed** — fresh targeted binary, `gui-tests.log` (3/4).
- `tests/test_test_probe.py`: **passed** — included in the 237-test clean-source run, `backend-tests.log`.
- `tests/test_test_history.py`: **passed** — same run, `backend-tests.log`.
- `tests/test_junit_runner.py`: **passed** — same run, `backend-tests.log`.
- `tests/test_tests_protocol.py`: **passed** — same run, `backend-tests.log`.
- `tests/test_profile_protocol.py`: **passed** — same run, `backend-tests.log`.
- `tests/test_relay_profile.py`: **passed** — same run, `backend-tests.log`.
- `manual: docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md`: **failed** overall — fresh short-card simulation proves Check/Done gate, flaky history, build profile and attachment (`scenario/01-card.png` through `10-attached.png`), but the actual long #7BM4 card reports 0 tests because its body is truncated and its Check findings are clipped (`05-wait.png`, `08-max.png`, `10-card.png`). Human-only questions remain unanswered. The old walkthrough's retired-test blocking expectation is stale.

- tests: passed (revision 0241d05ef19393e84c0ef98655c9687466a92efc)
- simulation: played (evidence docs/qa_evidence/2026-09-21-verify-7BM4/)
- staged: docs/qa_evidence/2026-09-21-verify-7BM4/

Unresolved: long-card body truncation; clipped Check findings on that card; missing Done means;
verifier updates/QA transition overwrite implemented_by with the verifier identity (exact calls in
`kimi-board-calls.json`); answer submission replaces the original Human QA questions in the
disposable card (`kimi-final-card.md` versus `kimi-answer-card.md`); all three owner judgments below.
Real-model Verify (64 tools) and Try it (17 tools) both completed; the GUI accepted an explicitly
automated observation and recorded/revealed it on disk. No owner judgment was supplied. The long
card hid Try it task/question/Open and the revealed result; see `25-try-written.png`,
`26-answer-submitted.png` and `flow-summary.json`. GLM's earlier attempt failed HTTP 429.
Reviewed by gpt-6-astra on 2026-09-21: 5 findings.

## Human QA
The owner is put into a staged project in which this card's three problems are happening; an AI
played it first (`docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/ai-pass.md`). Brief:
`docs/qa_evidence/2026-09-20-switchboard-tooling-hub/HUMAN-QA.md`. Answers go under each question,
in the owner's words.

1. **The agent says it is done.** Did the card stop you from accepting work that was not done, and did it tell you why quickly enough that you would use it?
2. **CI goes red about once a week.** Could you name the flaky test, since when, and on which machines, in under a minute, without reading a log?
3. **The build got slow.** Do you now know which file to look at, and is that answer on the card for the next person?


## Verdict
Not ready to close. Clean revision `0241d05ef193` passes all 237 named backend tests and
all four named CTest suites. The fresh orders simulation proves the short-card gate,
flaky-test history and actual build-profile attachment. The real long card still hides its
Tests section through truncation and clips its Check findings. Missing Done means and
unanswered Human QA judgments remain. The actual Kimi Verify run completed (64 tools, outcome done);
Try it completed (17 tools), and its answer field accepted a clearly labeled automated observation.
The full sequence was exercised, but fails: truncation hides Try it task/question/Open/reveal,
verifier writes overwrite implementer attribution, and answer submission drops the original Human
QA questions in the disposable copy. The shared card retains those questions unanswered.
Details: fresh evidence `report.md`, `flow-summary.json`, and before/after `kimi-*.md` snapshots.

## Execution Summary
Session handoff, 2026-09-21: a2 completed the real Verify → Try it → automated-answer sequence on clean 0241d05e, after GLM HTTP429 and a Kimi fallback. 237 backend tests, four GUI suites and fresh gate/history/profile/attachment scenarios passed. Full baseline report: docs/qa_evidence/2026-09-21-verify-7BM4/report.md (not an all-green verdict). Three baseline failures are already repaired: d8b25515 fixes desktop body truncation and clipped findings; 1f3a7af0 fixes lost prior Human QA. Parent clean-build repair proof: docs/qa_evidence/2026-09-21-7BM4-repair/ (177 backend tests, two GUI suites, 20-test long-card live drive). The verifier's baseline checklist is retained as historical evidence, not a claim the repaired revision still fails. Next: independently recheck the generated Try it handoff on exact repaired GUI/backend; fix/file the remaining verifier-identity overwrite in board_tools.py (_update/_move stamp implemented_by during verification); record missing Done means from original Issue/Decisions before any new implementation. All three original Human QA questions remain intact and unanswered. Do not close or invent owner answers. No verification process remains running.
2026-09-21 attribution repair: `board_move_card` now preserves an existing `implemented_by` when a verifier moves the card into QA, while unsigned cards retain worker/guest stamping. Added a regression covering verifier section update plus QA transition. Evidence: `docs/qa_evidence/2026-09-21-7BM4-attribution/README.md`.

## Done means
- A card’s complete `## Tests` section remains visible and Check reports stale, slow, flaky, failed, and missing evidence clearly enough to gate an unproven completion, including on long cards.
- The attached Test suites pane discovers the project’s tests and exposes run history, reliability, duration, failure detail, and card actions.
- Profile offers build, tests, and app targets; a completed run shows its hotspot table and can attach durable evidence to a card.
- Verification and Human QA updates preserve the original implementer attribution and all existing unanswered owner questions; failure is any silent attribution or question loss.
