---
id: 7BM4
type: work
status: executing
labels: [feature, switchboard, tests, profiling]
component: [gui, worker]
assignee: claude-code
rank: zzzzzzzzzzzzzzzy
created: '2026-09-20'
source: 'owner, Claude Code session, 2026-09-20'
links: {plans: [], commits: [c8b0a8d2, 912ab11a, eb0a9b76, 4ac7b57d, 71355e7a, db035cfd, 5e306871, 8ad92248, 6348ebef, 6bb87f04, cf7d1a5f, 97a019fc, 0152697f, f56a6ea0], evidence: [docs/qa_evidence/2026-09-20-test-suites-pane/], related: [R9G7, SDXE, PF4K], github: null}
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
- [ ] Card: `## Tests` section strip, Check button, dated `### Check` block, three actions, verification gate <!-- t:q5 -->
- [ ] Profile: scripts/relay-profile, button, streamed run, table pane, evidence under qa_evidence <!-- t:ea -->
- [ ] Docs and QA: SWITCHBOARD-DESIGN §4.14, VALIDATION inventory pointer, protocol §31, QA checklist <!-- t:em -->
