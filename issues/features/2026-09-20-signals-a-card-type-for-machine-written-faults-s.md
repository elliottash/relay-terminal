---
id: AQ6X
type: work
status: discussing
labels: [feature, switchboard, tests]
rank: i1
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [], related: [R9G7, 7BM4], github: null}
---
# Signals: a card type for machine-written faults such as failed tests

## Issue
do you think there should be a separate "agent-only" card type, that is for tracking, but humans arent supposed to read it.

like, cards that come from failed tests

the signals feature seems like it requires more investigation. so can you do more research on similar features in other harnesses / systems / project managers to firm it up? then we can write a detailed card under discussion. we can also connect it with #7BM4 as desired

## Research
`docs/SIGNALS-RESEARCH.md` (2026-09-20, 7b80f963): what happens after a failure is detected in Sentry, Rollbar, Alertmanager, PagerDuty, Opsgenie, Nagios, Chromium's LUCI Analysis, Mozilla Treeherder, Kubernetes TestGrid, Go watchflakes, Flutter cocoon, Trunk, Datadog, Buildkite, Linear Triage, GitHub code scanning, Dependabot, SonarQube and the AI coding products. Recommendations R1–R13 and a YAML sketch of the record are there.

**What it confirms.** The shape is an established pattern: a keyed, counted, machine-closed item in a lower layer, promoted into human work with a link both ways (PagerDuty alert→incident, code-scanning alert→tracking issue, LUCI cluster→bug, Trunk test→ticket). No AI coding product keeps such a record; in all of them the agent is stateless about failures.

**What it changes in the first proposal.**
1. Not a card file, and not in git. 320 of 823 commits in two days already touch `issues/`, and #7BM4 decided its run history is a gitignored JSONL. A signal is a fold over an append-only `issues/.private/signals/events.jsonl` beside that history; it gets a card id only when promoted.
2. Two states are too few: `pending`, `open`, `resolved` (machine-only, like GitHub's `fixed`), `dismissed` (reason, comment and a required expiry) and `removed` (the key left discovery; not a fix).
3. "Two green runs" must mean two passing *executions of that key*: most runs here are `ctest -R` subsets, and a run that never touched the test counts as nothing.
4. Flaky needs far more passes than broken (the cited systems use 10 to 100): 2 for broken, 20 for flaky.
5. Age alone never promotes; impact or repeated occurrence does.
6. When the card and the signal disagree the machine state wins: a card linked to an open signal cannot leave `needs-verification`, and a card closed while the test is red leaves the signal open.

## Proposal (revised; decided 2026-09-20, see Decisions)
- **Key** = source plus the check's own identity (`ctest:panelayout`, `unittest:<module.Class.test>`, `check:<code>:<path>`, `build:<target>`, `crash:<signal>:<top in-repo frame>`). The failure fingerprint (numbers, hex, paths, timestamps stripped) is an annotation for grouping and for "this now fails differently", never part of the key.
- **Open** on the second consecutive failing execution. Relay re-runs up to ten failed tests once: fail-fail is `broken` and opens at once; fail-pass stays `pending` with a flaky mark and opens as `flaky` on a transition count over its last 21 executions.
- **Resolve** silently on 2 (broken) or 20 (flaky) consecutive passing executions; not-built, skipped and timed-out executions advance nothing. A key unseen for 7 days is `stale` and is re-run first, not closed by a timer.
- **One cause, one item.** A build or collection failure opens `build:<target>` and the tests under it are "not evaluated"; three or more keys with one fingerprint in a run become one group; more than half of ten or more tests failing opens one `run:` signal and nothing else; at most ten individual signals per run. (These four thresholds are guesses, marked as such in the research.)
- **Reopen** within 30 days as `regressed`, skipping pending; later, a new signal naming the old one.
- **Promotion** to a bug card in `inbox`, linked both ways with a machine-owned `## Signal` section: the claiming agent gave up with a reason, or three failing runs over at least 24 hours unclaimed, or confirmed flaky. At most five promoted cards open.
- **Claims** reuse `session` (#R9G7) through `board_signals {list|claim|release|dismiss|promote}`; released when the pane closes.
- **What people see**: the count and one folded row, reusing #93WR's fold; groups before members; dismissed behind a toggle. Nothing reaches a human unasked except a promotion and a dismissal about to expire.
- **Sources** after tests: `relay-board.py check` problems and build failures first (build failures are needed for inhibition anyway), then crashes from `relay.log`, then CI; lint only against a baseline; QA verdicts never (no machine can close a judgement).
- **Relation to #7BM4**: own card, scheduled after #7BM4's step 1 (the history file and the JUnit writer, in progress now). #7BM4's "flaky tests become cards" is replaced by promotion here so it is not built twice.

## Decisions
Owner, 2026-09-20, on the twelve questions in the thread: "6 -- i think yes by default, but its optional.OK to all the others". (Question 6 in the terminal summary was thread question 9, whether agents work unclaimed signals unasked.)

1. **Store**: a local append-only file under the board's private folder, not git; signals are per machine.
2. **Not a card type**: a keyed record the board draws; it gets a card id only on promotion.
3. **Opens** pending on the first failure, open on the second consecutive failing execution of that key; Relay re-runs the failed tests once when the failed set is fast (about a minute by recorded duration), otherwise waits for the next natural run.
4. **Resolves** on 2 consecutive passing executions (broken) or 20 (flaky); a run that did not execute the key counts as nothing.
5. **Promotion** to a bug card: the claiming agent gave up with a reason, or three failing runs over at least 24 hours unclaimed, or confirmed flaky. Never age alone. At most five promoted cards open.
6. **The signal wins** over its card: a card linked to an open signal cannot leave needs-verification, and closing the card does not close the signal. The owner's override is a dismissal.
7. **Agent dismissal**: `environmental` and `flaky-known` only, with a comment and an expiry of at most 7 days; `wont-fix`, `expected` and longer expiries are the owner's; every dismissal expires.
8. **Blocking verification**: only a signal first seen in a run by the pane holding that card; older ones are listed as "open before this card".
9. **Agents work unclaimed signals unasked, by default, and it can be turned off** (owner: "yes by default, but its optional"). Reverses the recommendation of an explicit-sweep-only model. The plan fixes the mechanics; the starting point: a setting on the board (and in Options), default on, never when the board's `autonomy` is off; the pane whose run opened a signal claims it first; signals no run of a live pane owns are picked up by an idle pane agent of that project, at most three at a time per project; every pickup is a `session` claim, so it is visible and two panes never chase one test.
    - **Visible, not silent** (owner, same day: "and you get a notification that you can click on to open the agent thread, and those go into the sessions manger"). An unasked pickup runs as its own agent thread, not inside whatever the user is doing in a pane; starting one posts a notification whose click opens that thread (`NotificationCenter::postWithAction` already carries an action), amended when it finishes with the outcome (fixed and verified, gave up and promoted, dismissed); and the thread is a row in the Sessions manager, which already lists sessions and subagent threads and opens a thread's history (`SessionManager::onOpenThread`). The signal's `session` claim names that thread, so the board's chip links to the same place.
10. **Sources**: tests, `check` problems and build failures in the first version; then crashes, then CI; lint only against a baseline; QA verdicts never.
11. **Its own card**, after #7BM4's step 1 (run history and JUnit writer); #7BM4's "flaky tests become cards" is replaced by promotion here.
12. **Name**: signal.

Still to tune against real history once #7BM4's file exists: the group threshold (3 keys with one fingerprint), the red-run rule (more than half of ten or more), the cap of ten signals per run, the 21-execution flakiness window.
