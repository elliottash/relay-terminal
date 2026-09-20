---
id: AQ6X
type: work
status: needs-verification
labels: [feature, switchboard, tests]
assignee: agent
rank: i1
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [e3ca0447, 2048e4f6, 143d4943, 7503201d, fab65016, fe697501, d70e8306, 45353fbf, c489ae32, 1431abe8, 1efe81e8, 61c2f5a4, 444ec3e5, c8895fec, 13181dfa, '0beeadfc', b3275c21, 9e52957c, 190ca050, 72d830f9, 1e937bcc, 9cef1ab9, 8034facd, 5fde2cd5, bc471570, 567733ae, 6abcbe7e, b07a32ba, b9756872, 134068fe], evidence: [docs/qa_evidence/2026-09-20-signals-gui, docs/qa_evidence/2026-09-20-signal-threads], related: [R9G7, 7BM4], github: null}
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

## Plan
**Goal** — A failing test (later: a check problem, a build failure, a crash) becomes one keyed *signal* the machine opens and closes, folded over #7BM4's run history; agents claim, fix and verify signals, pick up unclaimed ones unasked by default as their own notified agent threads, and a signal becomes a bug card only on impact. Humans see a count and one folded row, never a flood. Decisions 1–12 on this card are the spec; `docs/SIGNALS-RESEARCH.md` R1–R13 the reasoning.

**Findings**
1. The occurrence store exists: `backend/relay_core/test_history.py` — `Execution {ts, id, result, duration, runner, run_id, commit, host, message, excerpt, source_hash}` appended to `<board>/.private/tests/history.jsonl`; `flake_score`, `_same_commit_flake`, `records()`, `prune()`. Signals must not store occurrences again.
2. Runs reach the worker through `backend/relay_core/tests_protocol.py` (`TestsCommands`: tests_list / tests_run / tests_stop / tests_history / tests_check; ingest of `.private/tests/incoming/` exactly once). That ingest is the one place every execution passes, so the signal fold is driven from there.
3. Claims: `board_tools.py` `board_claim` / `_release_on_close` / `release_claims` and the `session` field (#R9G7); `board_claimed_elsewhere`. The GUI chip and `paneExists` (#R9G7), the fold row `Row::Fold` and `selfClosedTitle` (#93WR, `src/BoardModel.*`, `src/BoardPane.cpp`).
4. Notifications: `src/Notifications.h` `NotificationCenter::postWithAction(title, body, kind, …)` and `amend(id, …)`; worker events reach the pane's notification code in `src/Pane.h` (grep `NotificationCenter::instance().post`).
5. Agent threads: `backend/relay_core/subagents.py` `Subagents.spawn(args, parent_thread)` and `sessions.py` `thread_dir()`; the Sessions manager (`src/Conversations.h` `SessionManager`, `include_threads`, `onOpenThread`) already lists subagent threads and opens their history.
6. Board settings: `agent: {autonomy, …}` in `board.yaml` (`board.CONFIG_TEXT`); Options pages in `src/SettingsPane.*`.
7. The history has `commit` but no dirty-tree marker (research R2): in this shared checkout a commit alone does not name the code under test.

**Steps**
1. `test_history.Execution` gains `tree_digest: str` — empty when the working tree equals `commit`, else the first 12 hex of sha256 over `git diff HEAD` (paths and hunks); written by every producer (`junit_runner`, ctest ingest, `tests_run`), read by nothing yet but signals. Prune and read unchanged.
2. `backend/relay_core/signals.py` — the record and the fold. Store: `<board>/.private/signals/events.jsonl`, append-only, one object per human or agent action `{ts, key, action: claim|release|dismiss|promote|note, session?, reason?, comment?, until?, card?}`. State: `fold(executions, events, now) -> {key: Signal}` with `Signal {key, source, kind: broken|flaky|group|run|build, state: pending|open|resolved|dismissed|removed, first_seen, last_seen, count, fingerprint, regressed, stale, session, card, dismissed: {reason, until}, fixed_in, excerpt}`. Rules exactly as decided: pending on first failing execution, open on the second consecutive failing execution of that key (a key not executed advances nothing); resolve on 2 consecutive passes (broken) or 20 (flaky, per `test_history.flaky`); `removed` when the key leaves discovery; reopen within 30 days as `regressed` skipping pending, later a new signal naming the old; dismissal expires; `stale` after 7 days unseen. Fingerprint = message with numbers, hex, paths and timestamps replaced. One cause one item: `build:<target>` inhibits the tests under it (recorded not-evaluated); ≥3 keys with one fingerprint in a run form a `group`; more than half of ≥10 executed tests failing opens one `run:<runner>` signal only; at most 10 individual signals per run. Thresholds as constants with the research's citations, to tune.
3. Promotion: `signals.promote(key)` creates a bug card (tab `bugs`, the failure excerpt as `request`, label `signal`, `links.related` the promoting run) with a machine-owned `## Signal` section rewritten on every state change; the signal records `card`; a card linked to an open signal is refused a move out of `needs-verification` (`board_tools._move`, code `board_signal_open`), and closing it never resolves the signal. Auto-promotion when: the claiming agent releases with reason `gave-up`; ≥3 failing runs over ≥24 h with no claim; kind flaky confirmed. Cap: 5 promoted cards open.
4. Worker: `tests_protocol` runs the fold after every ingest and after every `signals_*` write, and pushes `signals_changed {open, pending_count, dismissed_count, promoted}`; GUI→worker `signals_list`, `signals_claim {key}`, `signals_release {key, reason}`, `signals_dismiss {key, reason, comment, until}`, `signals_promote {key}` → `signals_written {kind, key, card?}`; agent tool `board_signals {action: list|claim|release|dismiss|promote, key?, reason?, comment?, until?}` with the same refusals as `board_claim` (`board_claimed_elsewhere` with the holder's session); agent dismissal limited to `environmental` and `flaky-known`, ≤7 days. The re-run: after a run with ≤10 failures whose recorded p50 durations sum to ≤60 s, `tests_run` re-runs those keys once before the fold (decision 3). Protocol §32.
5. Verification gate (decision 8): `tests_check` lists open signals first seen in a run by the pane holding the card as `blocks`, older ones as `open_before`; only `blocks` refuses the move.
6. GUI: the bugs tab shows `▸ N signals` as the #93WR fold row shape (groups before members, dismissed behind a toggle), each signal row with kind, key, count, last seen, the `⧉ session` chip; Enter opens a signal detail (excerpt, history sparkline from `tests_history`, actions claim / release / dismiss / promote); a promoted signal's card shows the `## Signal` section as a strip like `## Tests`. Nothing else on any human surface.
7. Unasked pickup (decision 9): (a) the pane whose run opened a signal is told in the same turn (`tests_run` result carries `opened`), and its policy line says to fix it before reporting; (b) orphaned open signals (no live session's run, or released by a closed pane) are picked up by a **signal thread**: the board worker spawns a subagent thread (`subagents.spawn`, owner session = the board's tab worker, task = the signal), at most 3 running per project, only when `board.yaml` `signals: {auto_work: true}` (default true; Options › Switchboard row "Work signals unasked"), and never when `agent.autonomy` is off. Starting one posts a notification with an `open_thread` action; it is amended on finish with fixed-and-verified / gave up (promoted) / dismissed; the thread carries `signal: <key>` so the Sessions manager lists it under the project with the key as its title, and the signal's `session` is the thread's id so the board chip opens the same thread.
8. Guest path: `POLICY.md` gains a "Signals" paragraph (read `signals` via `relay-board.py signals`, claim by `assignee`); `relay-board.py signals [list|claim|release|dismiss|promote]`.
9. Sources after tests (decision 10), same fold, new producers only: `check:<code>:<path>` from `board.check`, `build:<target>` from `tests_run`'s build step; crashes and CI are follow-up cards.

**Orchestration** — Phase 1 (steps 1–5, 8: backend, protocol, tests) and Phase 2 (step 6: GUI over the protocol contract in step 4) run in parallel as two subagents; Phase 3 (step 7: threads, notification, setting) after both, one subagent, since it touches both halves. Writes stay in each subagent's own files: phase 1 never under `src/`, phase 2 never under `backend/`.

**Risks** — The four mass-failure thresholds are guesses (tune against `history.jsonl` once a week of runs exists). A background thread fixing code in the shared checkout lands through `land.py` like any session; it must claim late and dry-run (memory: land-py-claim-late). Reopen-vs-new at 30 days needs `resolved` signals kept that long (retention 30 days, decision 13 in the research). Owner question, not blocking: whether a signal thread may run when the user is typing in the same project (proposal: yes, it is its own worker).

**Verify** — `tests/test_signals.py`: the state machine table-driven over execution sequences (open on second, resolve 2/20, not-evaluated advances nothing, regressed within 30 days, new after, dismissal expiry, group and run collapse, cap 10, promotion triggers and cap 5, the verification gate); `tests/test_tests_protocol.py` for the messages and the re-run rule; `tests/test_board_tools.py` for `board_signals` refusals; GUI `tests/boardmodel_test.cpp` for the fold row and the detail; an Xvfb run with a seeded red test: the row appears, a pane claims it, the fix resolves it after two passes, the thread shows in Sessions and the notification opens it.

## Tasks

- [x] Phase 1: signals.py fold and store, tree_digest, promotion and the move gate, signals_* messages, board_signals, re-run rule, guest path, tests
- [x] Phase 2: the fold row and dismissed toggle on the bugs tab, signal page with four actions, `## Signal` strip, live evidence
- [x] Phase 3: signal threads for orphaned signals, verify_signal, notification with open-thread action, Sessions row, Options row, board.yaml `signals.auto_work`
- [x] Protocol §32 and §32.10, landed with #7BM4's §31.9 in 134068fe on the owner's instruction

## QA checklist

- [ ] Make a Python test fail and run it twice through the Test suites pane or `tests_run`: the bugs tab shows `▸ 1 signal`; the row reads `broken`, the key, `×2`.
- [ ] Fix the test and run it twice: the signal resolves silently and the row disappears; run it once only: it stays open (a not-executed key advances nothing).
- [ ] Break a shared header so many tests fail: one `run:` or `build:` signal, not one per test.
- [ ] In a pane, ask the agent to run tests that fail: the result text names the opened keys and the agent claims and fixes them in the same turn.
- [ ] With nobody claiming an open signal and `signals.auto_work` on: a signal thread starts within one fold, a notification "Working on <key>" appears, clicking it opens the thread's history, the Sessions manager lists `⚑ signal · <key>` under the project, the chip on the signal reads live; on finish the notification reads Fixed / Gave up — promoted to #ID / Dismissed.
- [ ] Turn "Work signals unasked" off in Options › Switchboard: no thread starts; `board.yaml` says `signals: {auto_work: false}`.
- [ ] Dismiss a signal from its page as `environmental` with a 3-day expiry: it moves under `▸ N dismissed`; an agent trying `wont-fix` is refused.
- [ ] Promote a signal: a bug card appears with the excerpt as its request and a `## Signal` strip; moving that card out of needs-verification while the signal is open is refused with `board_signal_open`; closing the card leaves the signal open.
- [ ] A guest agent in the project can list, claim and release signals with `relay-board.py signals` and POLICY.md says how.
