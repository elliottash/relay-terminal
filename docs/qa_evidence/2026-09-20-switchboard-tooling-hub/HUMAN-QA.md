# Human QA for #7BM4: you are put into the problem, after an AI has tried it first

Owner, 2026-09-20: "in my mind, QA support is 'human QA', as in a user is put into a test case
that simulates the problem the issue was designed to address", "thats good to design for an AI to
try simulating first", and "i think verification should involve that (typically)". So this is not
a tour of buttons. It is a small staged project, `orders`, in which the three problems the card
was written to solve are actually happening, and you find out whether the feature solves them.
An AI played the same scenarios first in the real app; what it saw is in
[`scenario/ai-pass.md`](scenario/ai-pass.md), with a screenshot per step. It stopped where a
judgement starts: the last steps of each scenario, and the question at the end, are yours.

## What the feature is, in three sentences

A card can list the tests that prove it, and **Check** says without running anything whether
those tests still exist, ever ran, or failed last time, and refuses to let the card move on while
they do not prove it. A **Test suites** pane beside the board shows every test's run history,
reliability and duration across machines. A **Profile** button measures where the build, the tests
or the app spend their time and can put the answer on a card.

## Before you start

```
python3 docs/qa_evidence/2026-09-20-switchboard-tooling-hub/scenario/stage.py --fresh
/tmp/claude-1000/land/tooling-host/verify/build/relay --workspace ~/relay-qa/7bm4-orders
```

- The first line stages `~/relay-qa/7bm4-orders` (a git repository, a board, a build directory and
  ten days of seeded run history from two machines). It is safe to delete afterwards.
- The second line is the Relay to test with. Your running Relay predates the feature, the shared
  `build/relay` holds every session's uncommitted code, and a clean export of current `main`
  crashes at startup on a fresh profile (card #561P). That binary is the build gate's, from
  `f72960e2`; it has everything below except two later wording fixes (grouped never-run lines,
  short names in the gate's notice), which you will see as longer text. Quit your running Relay
  first, or accept that this second instance will not save its layout.
- In the window: **Ctrl+Shift+S** opens the Switchboard. If it is narrower than about 900 px the
  new panes open *below* it and are short; widen the window or maximise the pane (⊞ in its header).
- Write what you saw in the last column: **OK**, or what was different. Do not try to work out why.

## Scenario 1: The agent says it is done

**The situation.** An agent fixed a bug, wrote 'all tests pass' on the card and moved it to Needs verification. One listed test fails, one was deleted last week, and the Python tests have never run. Nothing on the card says so.

**Before this feature.** You would have believed the thread, or opened a terminal, worked out which tests belong to the card, and run them one by one.

| # | Do | What should happen | Saw |
|---|---|---|---|
| 1.1 | Open the Switchboard, open the card 'Order totals are wrong above 100 items'. | A strip above the body: Tests 4 listed, never checked, and a Check button. The thread says: Fixed... All tests pass. | |
| 1.2 | Press Check. | Findings within seconds, nothing is run: rounding is not in the project any more; totals_large_order failed the last time it ran; tests/test_invoice.py never ran here. A dated Check block appears under ## Tests in the card body. | |
| 1.3 | Try to move the card to Done with the status picker. | Refused. A notice names the tests (rounding, test_invoice, totals_large_order) and offers Override…; the picker goes back to Needs verification. | |
| 1.4 | Press Run these, then Check again. *(the AI pass stopped before this step; it is yours)* | totals_large_order fails for real (expected 1500000 cents, got a negative number); the invoice tests now pass and drop out of the findings; rounding is still gone. | |
| 1.5 | Decide: move the card back to Executing, or press Override… and give a reason. *(the AI pass stopped before this step; it is yours)* | Either the card returns to Executing, or it moves on and the thread gains a decision entry quoting your reason. | |

**The question only you can answer.** Did the card stop you from accepting work that was not done, and did it tell you why quickly enough that you would use it?

## Scenario 2: CI goes red about once a week

**The situation.** For ten days the run has gone red every few days; someone reruns it and it is green. Nobody knows which test, since when, or on which machine.

**Before this feature.** You would have scrolled CI logs for the red runs and compared them by hand.

| # | Do | What should happen | Saw |
|---|---|---|---|
| 2.1 | Press Tests on the Switchboard's tool row. | Test suites pane; the summary line says 7 tests, 3 passed (1 slow, 1 flaky), 1 failed, 3 never run. The first row is inventory_sync, marked flaky, 70% reliable, with red and green cells mixed in its grid. | |
| 2.2 | Click inventory_sync. | Detail: 70% reliable over 20 runs, flake score about 5, last failure with its message (warehouse did not answer within 200 ms), and a history in which it passes and fails at the same commit on both desktop and laptop. | |
| 2.3 | Press Rerun until fail. *(the AI pass stopped before this step; it is yours)* | It fails within a few runs; the grid gains cells; the message is the same. | |
| 2.4 | Type is:slow in the filter. *(the AI pass stopped before this step; it is yours)* | report_export alone: p50 about 0.3 s, p95 about 4 s. It got slow eight runs ago, which the grid and the history show. | |
| 2.5 | Select inventory_sync and press Make a card, or Attach to card and pick 'CI goes red about once a week'. *(the AI pass stopped before this step; it is yours)* | A card in Bugs names the test in ## Tests, or the existing card gains the line. | |

**The question only you can answer.** Could you name the flaky test, since when, and on which machines, in under a minute, without reading a log?

## Scenario 3: The build got slow

**The situation.** A clean build used to be quick. Before anyone guesses, someone has to measure where the time goes.

**Before this feature.** You would have timed make by hand, or guessed.

| # | Do | What should happen | Saw |
|---|---|---|---|
| 3.1 | Press Profile, choose Build (this machine). | Progress lines in the board's notice; after under a minute a pane 'Profile: Build (this machine)' with a table Output / Compile / Share. src/report.cpp.o is first with most of the compile time. | |
| 3.2 | Press Open flame graph. *(the AI pass stopped before this step; it is yours)* | The browser opens a local speedscope page (file://, nothing uploaded) showing the build as lanes. | |
| 3.3 | Press Attach to card… and pick 'The build got slow last month'. *(the AI pass stopped before this step; it is yours)* | The card gains a ## Profile table and links.evidence names the snapshot directory under docs/qa_evidence/. | |

**The question only you can answer.** Do you now know which file to look at, and is that answer on the card for the next person?

## Your verdict

Answer the three questions in a comment on #7BM4 (the reply box on the card page;
Ctrl+Shift+Enter comments without asking the agent). If the answers are yes, move #7BM4 to
**Done**: it lists its own tests, so its own gate will ask you to run them first. If a step
differed or an answer is no, say which, and move the card back to **Executing**. Already known,
not findings: #561P; the app profile target has no screenshot yet; the agent-facing
`tests_check` tool writes no block.

## Appendix: the feature tour on this repository's own board

The scenarios above use a staged project. These tables do the same features on Relay's own board
(4,550 tests), for when you want to see them at scale. Open `~/repos/relay-terminal` instead.

### A. The Test suites pane (5 minutes)

| # | Do | You should see | Saw |
|---|---|---|---|
| A1 | Press **Tests** on the tool row. | A pane titled **Test suites** opens beside or below the Switchboard. Its summary line says about `4550 tests · … never run · …`. Columns: Name, Recent runs, Reliability, p50, p95, Runs, Last run, Cards. | |
| A2 | Type `board` in the pane's filter box. | Only tests whose name, file or label contains "board" remain: `board`, `boardpane`, `boardmodel`, and the Python `test_board…` cases. | |
| A3 | Click the `boardpane` row, then press **Run**. | The row shows *queued* then *running*; within a few seconds its grid gains a green cell at the left, Runs becomes 1, Reliability 100 %, Last run "just now". The summary line's passed count goes up by one. | |
| A4 | Click the row again and look at the detail area under the table. | The file `tests/boardpane_test.cpp` as a link, one execution line with the result, duration, commit and this machine's host name, and "no card names this test" (or the cards that do). | |
| A5 | Clear the filter and type `is:never`. Then try `is:failed`. | `is:never` lists the tests that have never run here (nearly all of them today); `is:failed` lists none, or only ones you failed on purpose. | |
| A6 | Press **Display ▾** and untick **p50**. Tick it again. | The column disappears and comes back. | |
| A7 | Drag the pane's edge to make it narrow (about half its width). | The run grid shows fewer cells, then p50, Runs and Cards leave, and the names stay readable — nothing elides to "adde…lows". Widen it: they come back. | |
| A8 | Select a row and press **Make a card**. | The Switchboard reveals a new card in **Bugs › Inbox** titled "Failing test: …" or "Flaky test: …" (or the test's name if it is fine), labelled `bug, tests`, with a `## Tests` section naming the test. Move that card to Done afterwards, or leave it: it is a real card. | |

### B. The card's Tests section, Check and the gate (10 minutes)

Card **#7BM4** already lists its own tests, so use it.

| # | Do | You should see | Saw |
|---|---|---|---|
| B1 | On the Switchboard, type `7BM4` in the filter and click the row **#7BM4 Switchboard as the project's tooling…** | The card page. Above the body, a strip: **Tests 11 listed · never checked** with a **Check** button. | |
| B2 | Press **Check**. | Within a couple of seconds, findings appear under the strip: four `ctest -R …` lines "has never run here", six Python files "N of N never ran here (…)", two "are skipped for good", and one "manual evidence … HUMAN-QA.md" line (this file). Actions offered: **Run these**. Scroll the body: a new `### Check <today's date>` block sits under `## Tests` with the same lines, and the thread has an *evidence* entry. | |
| B3 | Press **Run these**. Wait — it runs four C++ suites and six Python files, about a minute. | The board's notice line reports the run's end ("… passed …"). Open the Test suites pane: `testsuites`, `cardtests`, `profilepane`, `windowstate` and the Python cases now show a green cell. | |
| B4 | Press **Check** again. | The never-run findings are gone. What remains: the two "skipped for good" warnings and the manual line if this file is not yet on disk where it says. The strip says "checked <time> · …". | |
| B5 | On the card's status picker (it says **Needs verification**), choose **Needs QA**. | Because the remaining findings are warnings, not failures, the move goes through and the card lands in Needs QA. *(If you did this before B3, you should instead see the move refused: a notice naming the never-run tests, the picker snapping back, and an **Override…** button that asks for a reason.)* | |
| B6 | Optional, to see the gate refuse: edit the card (`e`) and add a line `- \`ctest -R does-not-exist\`` under `## Tests`, press **Check**, then try to move the card to **Done**. | Check reports `ctest -R does-not-exist is not in the project any more` (a failure). The move is refused with that test named; **Override…** asks for one line; after you type one, the card moves and its thread gains a *decision* entry quoting your words. Remove the line afterwards. | |

### C. Profile (5 minutes, plus a build)

| # | Do | You should see | Saw |
|---|---|---|---|
| C1 | Press **Profile** on the tool row. | A menu with four entries, each with one line under it: **Build (this machine)**, **Build (another machine)**, **Python tests**, **The app**. Nothing in it names a specific computer. | |
| C2 | Choose **Build (this machine)**. This configures and builds `relay` in a Ninja directory of its own under `/tmp/claude-1000/`; cold, it takes a few minutes, and the notice line streams progress. | A pane titled **Profile: Build (this machine)** with a header like `N steps · … wall · … of compile time · <host>`, a table Output / Compile / Share sorted by time, with `src/main.cpp.o` first at roughly three quarters of the total, and two buttons: **Open flame graph**, **Attach to card…**. | |
| C3 | Press **Open flame graph**. | Your browser opens a local speedscope page (a `file://` URL, nothing uploaded) showing the build as lanes. | |
| C4 | Press **Attach to card…**, pick **#7BM4**. | The notice says "Added the profile to #7BM4"; the card gains a `## Profile` section with the table and `links.evidence` names `docs/qa_evidence/<today>-profile-build`. | |
| C5 | Profile again, choose **Build (another machine)**. | A small dialog asks for the machine "as ssh names it" with an empty default the first time. Press Cancel: nothing runs. (If you enter your laptop's name, it builds there and the table comes back with that host in the header; the name is remembered for next time and appears nowhere in the repository.) | |
| C6 | Profile, choose **Python tests**. | After the suite runs (a few minutes), a pane with a functions table (name, file:line, self %, total %), py-spy's speedscope file under the evidence directory, and the same two buttons. | |

### D. The second machine (optional, needs a host you can ssh to)

```
scripts/relay-tooling-setup --install                      # here
ssh <host> 'bash -s -- --install' < scripts/relay-tooling-setup
scripts/relay-remote-tests --host <host> --qt 6 -R '^(jobs|board)$'
```

You should see: both setup runs end in `0 needed tool(s) missing, 0 smoke test(s) failed`; the
remote run builds (four minutes cold), reports `100% tests passed`, and leaves a folder under
`issues/.private/tests/incoming/<host>-…/`. Press **Refresh** in the Test suites pane: `jobs`
and `board` now show an execution whose host is that machine.


## For #YZ8G: the shape this took

1. **A staged project in which the problem is happening**, built by one script with no model and
   no network, small enough to read (`scenario/stage.py`).
2. **The scenario as data** (`scenario/scenario.json`): the situation, what one did before, steps
   with an actor (`both` or `human`), an action and an expectation, and one question a person must
   answer.
3. **An AI pass first** (`scenario/ai-pass.sh`, `scenario/ai-pass.md`): the same steps in the real
   app on an isolated profile, a screenshot per step, findings fixed or listed before a person
   spends time. It stops where judgement starts.
4. **The human brief** generated from the same data: the situation in plain words, the steps the
   AI already cleared marked as such, a column for what was seen, the question, where the verdict
   goes and which status follows.
