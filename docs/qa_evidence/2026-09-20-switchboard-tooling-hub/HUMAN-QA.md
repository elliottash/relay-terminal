# Human QA walkthrough: the Switchboard as the project's tooling hub (#7BM4)

Written 2026-09-20 for the owner, who asked "help me do human QA on it because I don't really
understand this issue". It is also the first worked example for #YZ8G (a QA-support agent): the
shape an agent should produce when it hands a finished card to a person — what the feature is
for, what to press, what you should see, and where to write what you saw.

## What this feature is, in three paragraphs

Every card on the Switchboard can now say **which tests prove it**, in a `## Tests` section:
one line per test, written the way you would run it. A **Check** button on the card page reads
that list and answers, without running anything, whether those tests still exist, have ever run,
are skipped for good, or were failing or flaky the last time they ran. Its answer is written into
the card as a dated block, and it is a **gate**: a card in Needs verification cannot move on to
QA or Done while a listed test is missing, never run or failing, unless you give a reason, which
is recorded.

Beside the board, a **Test suites** pane lists every test the project has (4,550 here), with a
grid of its recent runs, its reliability, how long it takes, its last failure, and which cards
name it. Runs from this machine and from another machine sit side by side. A flaky or failing
test can become a card with one button.

A **Profile** button on the same row asks what to profile — the build here, the build on another
machine, the Python tests, or the app — writes the snapshot under `docs/qa_evidence/`, shows a
table of where the time goes, opens a flame graph in the browser if you want it, and can attach
the table to a card. Design: `docs/SWITCHBOARD-DESIGN.md` §4.14. Commands behind it all:
`docs/PROFILING.md`.

## Before you start

- **Which Relay.** The feature is on `main`; the Relay you have running predates it. A clean
  export of current `main` **crashes at startup on a fresh profile** (card #561P, another
  session's model-catalog change). Until that card lands, use the binary the build gate made from
  `f72960e2`, which starts and has everything below except one later wording fix:
  `/tmp/claude-1000/land/tooling-host/verify/build/relay --workspace ~/repos/relay-terminal`.
  Once #561P is fixed: `git archive main | tar -x -C <dir> && cmake -S <dir> -B <dir>/build
  -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build <dir>/build -j16 --target relay`, then run
  `<dir>/build/relay`. Do not use the shared `build/relay`: it holds every session's
  uncommitted code.
- **Quit your running Relay first**, or accept that this second instance will not save its
  layout (they share one lock).
- **Open this repository in a tab** and press **Ctrl+Shift+S** for the Switchboard. The tool row
  at the bottom of the list reads **Check · Clean up · Tests · Profile**. If the Switchboard is
  narrower than about 900 px, the new panes open *below* it; widen the window or maximise the
  pane (the ⊞ button in its header) to see more rows.
- Nothing below types into a terminal pane. Every step is a click or a keystroke on the board.

Write what you saw next to each step: **OK**, or what was different. A step that says "you
should see" and you do not is a finding; do not try to work out why.

## A. The Test suites pane (5 minutes)

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

## B. The card's Tests section, Check and the gate (10 minutes)

Card **#7BM4** already lists its own tests, so use it.

| # | Do | You should see | Saw |
|---|---|---|---|
| B1 | On the Switchboard, type `7BM4` in the filter and click the row **#7BM4 Switchboard as the project's tooling…** | The card page. Above the body, a strip: **Tests 11 listed · never checked** with a **Check** button. | |
| B2 | Press **Check**. | Within a couple of seconds, findings appear under the strip: four `ctest -R …` lines "has never run here", six Python files "N of N never ran here (…)", two "are skipped for good", and one "manual evidence … HUMAN-QA.md" line (this file). Actions offered: **Run these**. Scroll the body: a new `### Check <today's date>` block sits under `## Tests` with the same lines, and the thread has an *evidence* entry. | |
| B3 | Press **Run these**. Wait — it runs four C++ suites and six Python files, about a minute. | The board's notice line reports the run's end ("… passed …"). Open the Test suites pane: `testsuites`, `cardtests`, `profilepane`, `windowstate` and the Python cases now show a green cell. | |
| B4 | Press **Check** again. | The never-run findings are gone. What remains: the two "skipped for good" warnings and the manual line if this file is not yet on disk where it says. The strip says "checked <time> · …". | |
| B5 | On the card's status picker (it says **Needs verification**), choose **Needs QA**. | Because the remaining findings are warnings, not failures, the move goes through and the card lands in Needs QA. *(If you did this before B3, you should instead see the move refused: a notice naming the never-run tests, the picker snapping back, and an **Override…** button that asks for a reason.)* | |
| B6 | Optional, to see the gate refuse: edit the card (`e`) and add a line `- \`ctest -R does-not-exist\`` under `## Tests`, press **Check**, then try to move the card to **Done**. | Check reports `ctest -R does-not-exist is not in the project any more` (a failure). The move is refused with that test named; **Override…** asks for one line; after you type one, the card moves and its thread gains a *decision* entry quoting your words. Remove the line afterwards. | |

## C. Profile (5 minutes, plus a build)

| # | Do | You should see | Saw |
|---|---|---|---|
| C1 | Press **Profile** on the tool row. | A menu with four entries, each with one line under it: **Build (this machine)**, **Build (another machine)**, **Python tests**, **The app**. Nothing in it names a specific computer. | |
| C2 | Choose **Build (this machine)**. This configures and builds `relay` in a Ninja directory of its own under `/tmp/claude-1000/`; cold, it takes a few minutes, and the notice line streams progress. | A pane titled **Profile: Build (this machine)** with a header like `N steps · … wall · … of compile time · <host>`, a table Output / Compile / Share sorted by time, with `src/main.cpp.o` first at roughly three quarters of the total, and two buttons: **Open flame graph**, **Attach to card…**. | |
| C3 | Press **Open flame graph**. | Your browser opens a local speedscope page (a `file://` URL, nothing uploaded) showing the build as lanes. | |
| C4 | Press **Attach to card…**, pick **#7BM4**. | The notice says "Added the profile to #7BM4"; the card gains a `## Profile` section with the table and `links.evidence` names `docs/qa_evidence/<today>-profile-build`. | |
| C5 | Profile again, choose **Build (another machine)**. | A small dialog asks for the machine "as ssh names it" with an empty default the first time. Press Cancel: nothing runs. (If you enter your laptop's name, it builds there and the table comes back with that host in the header; the name is remembered for next time and appears nowhere in the repository.) | |
| C6 | Profile, choose **Python tests**. | After the suite runs (a few minutes), a pane with a functions table (name, file:line, self %, total %), py-spy's speedscope file under the evidence directory, and the same two buttons. | |

## D. The second machine (optional, needs a host you can ssh to)

```
scripts/relay-tooling-setup --install                      # here
ssh <host> 'bash -s -- --install' < scripts/relay-tooling-setup
scripts/relay-remote-tests --host <host> --qt 6 -R '^(jobs|board)$'
```

You should see: both setup runs end in `0 needed tool(s) missing, 0 smoke test(s) failed`; the
remote run builds (four minutes cold), reports `100% tests passed`, and leaves a folder under
`issues/.private/tests/incoming/<host>-…/`. Press **Refresh** in the Test suites pane: `jobs`
and `board` now show an execution whose host is that machine.

## E. Your verdict

- If every step you tried says OK: move #7BM4 to **Done** from the card page (the gate will let it
  through once B3 has run), and the thread records you as the verifier.
- If a step differed: write the step number and what you saw as a comment on #7BM4 (the reply
  box on the card page, Ctrl+Shift+Enter comments without asking the agent), and move the card
  back to **Executing**. One comment per finding.
- Things already known, not findings: the app profile target has no screenshot yet; #561P blocks
  fresh-profile launches of current main; the agent-facing `tests_check` tool writes no block.

## For #YZ8G: what a QA-support agent should copy from this

1. **Start with the "what is this" paragraphs**, written for someone who did not follow the work.
2. **Name the binary and the profile** to test with, and say why not the obvious one.
3. **Number every step; give each a "you should see"** concrete enough to disagree with, and a
   blank column for what was seen. The person records, the agent interprets.
4. **Use the project's own data** (this board, this card) so the steps are real, and make the
   feature verify itself where it can (#7BM4 lists its own tests and gates its own move).
5. **Say where the verdict goes** and which card status follows from it.
6. **List what is already known** so it is not reported twice.
