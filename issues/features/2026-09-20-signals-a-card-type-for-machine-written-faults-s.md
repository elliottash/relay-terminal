---
id: AQ6X
type: work
status: discussing
labels: [feature, switchboard, tests]
waiting_on: owner
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

## Proposal (revised, for discussion)
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
None yet: twelve questions are on the thread, each with a recommendation.
