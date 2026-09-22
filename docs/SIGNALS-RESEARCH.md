# Signals: how machine-detected failures become tracked items (research, 2026-09-20)

Research date: 2026-09-20, for card #AQ6X (a `signal` card type: a fault a machine writes and a
machine closes, first source failed tests) and its neighbour #7BM4 (Test suites pane, JSONL run
history, `## Tests` and Check). `docs/BOARD-TOOLING-RESEARCH.md` already covers how Buildkite,
Datadog, Trunk, TestGrid and Sentry **measure** tests; none of that is repeated. The subject here is
what happens **after** a failure is detected: does it become a tracked item, under what key, what
opens it, what closes it, what reopens it, and what keeps it from drowning the people reading the
backlog. Nothing here is implemented; sequencing belongs on the card.

**How to read the claims.** A statement with a URL was read in that vendor's own documentation, or
in the project's own source or configuration where the docs are silent (marked *source*), on
2026-09-20. What could not be confirmed on a primary page is marked *unverified* and collected in
§13. A `path:line` was read in this tree the same day while other sessions were editing it, so
function names are the stable anchors. Findings in §3–§8 are numbered 1–63 in one sequence, and a
bare number in parentheses later in the document — (14, 22) — refers back to them; F1–F13 are the
cross-cutting findings of §10 and R1–R13 the recommendations of §11. Per the repo's licensing
rule, Warp is cited from its public docs only.

## 1. The question, and the short answer

The proposal on #AQ6X before this research: a `signal` card type in its own `signals/` folder, two
states (open, resolved), one signal per key with occurrences counted, a mass failure collapsed into
one signal, auto-resolve after two consecutive green runs, folded for humans into one row, promoted
to a bug card when the agent cannot fix it or it is older than a day, and claimed with `session`
like any card.

Thirty-odd systems were read. The short answer, argued in §10 and §11:

1. **The shape is right and is nobody's invention.** A keyed, counted, machine-closed item that is
   kept apart from human work and *promoted* into it is exactly PagerDuty's alert → incident, Jira
   Service Management's alert → incident, GitHub's code scanning alert → issue/campaign, Mozilla's
   internal issue → Bugzilla bug, and LUCI's cluster → rule → bug. Every one of them keeps two
   layers, and the link between them.
2. **Two states are too few.** Every system that closes items by machine has, in addition to open
   and resolved: a **pending** tier before the item counts (Prometheus `for:`, Nagios soft states,
   Mozilla's 3-in-7-days, PagerDuty auto-pause), a **human-only dismissed state with a reason and,
   in the careful ones, an expiry** (GitHub `dismissed_reason`, Alertmanager silences with a required
   `endsAt`, Sentry archive-until-escalating), and a way to say **"gone" is not "fixed"** (Semgrep's
   Removed, GitHub's stale configurations).
3. **"Two consecutive green runs" is the right rule only if a run that did not execute the test
   counts as nothing.** Here sessions run `ctest -R <name>` subsets by rule, so most runs cover
   almost nothing. Prometheus can resolve on absence because every evaluation is complete; GitHub
   code scanning refuses to, and leaves alerts open when their configuration stops running. The
   resolve rule has to be "N consecutive passes **of that key**", with a separate aging rule for
   keys nobody runs any more.
4. **"Older than a day" is the wrong promotion trigger.** Nobody promotes on age alone: LUCI
   promotes on impact over a window (10 rejected CLs in a day), Mozilla on rate (30 failures a week),
   Flutter on a rate that persists 7 days, Opsgenie/JSM on dedup count. Age without occurrences is
   how a test nobody ran becomes a bug card.
5. **It should not be files in git.** Every product surveyed keeps occurrences and machine state in
   a store and puts only the promoted item where humans work. Here 320 of the last 823 commits
   already touch `issues/`, several sessions share one checkout and one index, and #7BM4 has already
   decided the execution history is a gitignored JSONL. A signal is a **fold over that history plus
   a small state file**, and only its promotion is a card in git.
6. **No AI coding harness has this.** In all of them the persistent, deduplicated failure record
   lives in some other product (Sentry issue, code scanning alert, flaky-test table) and the agent is
   stateless about failures: it gets one as a prompt, fixes it, and CI on the PR says whether it
   worked (§8). A local signal store that agents claim from is new ground, which is a reason to copy
   the lifecycle rules from the systems above rather than invent those too.

## 2. Ground truth in this repo

**There are three card types and a type is cheap to add, a lifecycle is not.** `CARD_TYPES = ("work",
"memory", "alias")` (`backend/relay_core/board.py:170`), each with its own status → folder map
(`MEMORY_STATUS_FOLDER = {"active": "", "retired": "archive"}`, `:182`) and field set (`WORK_FIELDS`,
`MEMORY_FIELDS`, `:208-224`). A fourth type is a tuple entry, a folder map and a field tuple; what
does not exist is any code path where **the worker itself** opens, counts and closes a card without
a model or a person asking.

**Claims exist and are per pane.** `session` (2026-09-20, #R9G7) is the pane session token written
only by `board_claim` (`BoardTools._claim`, `board_tools.py:2364`); a second claimant is refused
with `board_claimed_elsewhere`, and the field is dropped when the card reaches `done`/`dropped` or
the pane closes (`docs/BOARD-FORMAT.md` §2.2). That is the whole of what a signal needs for
"two panes do not chase the same red test", and it is independent of where the signal is stored.

**The check command already emits keyed machine findings.** `Problem {code, path, message,
severity}` (`board.py:962`) from `Board.check()` (`:1185`) is a finding with a natural key
(`code` + `path`), recomputed from scratch on every run, which disappears when fixed. It is a signal
source that needs no history at all (§11.10).

**The fold the proposal refers to is presentation, not a type.** #93WR folds self-closed done cards
into one row, "N closed by the agent", by comparing `verified_by` with `implemented_by`; the cards
stay ordinary work cards. The same widget can draw a signals row whatever backs it.

**`issues/` is already the busiest path in the repository.** `git log --since='2 days ago'` on
2026-09-20: 823 commits, 320 of them touching `issues/`; 355 card files, 185 threads. Every write
goes through `scripts/land.py` because the shared index reverted other sessions' work five times in
one day (CLAUDE.md). A test run that writes or rewrites N card files is N more contested paths per
run, from every pane that runs tests.

**The store #7BM4 already plans is the signal's raw material.** `issues/.private/tests/history.jsonl`,
gitignored, one object per test execution `{ts, runner, id, result, duration, commit, run_id}`
(#7BM4 Storage; tooling research §3.5). `issues/.private/` is the per-machine root, `Board.card_paths()`
walks only `*.md` two levels down so `check` ignores other files there, and it already holds two
non-card state files (`forge-sync.json`, `forge-logins.yaml`; BOARD-FORMAT §5).

**Crashes and QA verdicts are already machine-readable.** `gui_crash signal=… name=… addr=…
build=…` followed by `gui_crash_frames_begin` in `~/.local/share/relay/logs/relay.log`
(`docs/CRASH-DIAGNOSIS.md`), and the verdict-on-close contract enforced by `board_move_card`
(BOARD-DESIGN §4). Both are candidate sources (§11.10).

## 3. Error trackers: grouping is the product

### 3.1 Sentry

1. **Identity is a fingerprint with a fallback ladder.** "All versions consider the `fingerprint`
   first, the `stack trace` next, then the `exception`, and then finally the `message`"; only in-app
   frames count, filenames are normalised (revision hashes removed), and the message fallback is the
   message "without any parameters"
   (https://docs.sentry.io/concepts/data-management/event-grouping/). Fingerprint rules are
   `matcher:expression -> values`, and `{{ default }}` exists so a rule can **subdivide** the
   default group rather than replace it
   (https://docs.sentry.io/concepts/data-management/event-grouping/fingerprint-rules/).
2. **A key change never rewrites history.** A new grouping algorithm "is only applied to new events
   going forward", and rules do not "affect already existing issues" (same page). An issue is a *set*
   of fingerprints: merge adds to the set, unmerge removes one, and "we don't infer any new grouping
   rules from how you merge issues"
   (https://docs.sentry.io/concepts/data-management/event-grouping/merging-issues/).
3. **States.** Unresolved carries a substatus: New = "created in the last 7 days", Ongoing =
   "created more than 7 days ago or has manually been marked as reviewed", Escalating = "exceeded its
   forecasted event volume", Regressed = "A resolved issue that's come up again"; then Archived and
   Resolved. The review queue is `is:for_review` = "new, regressed, or unresolved issues that
   haven't been marked as reviewed yet" (https://docs.sentry.io/product/issues/states-triage/). The
   maintainers' stated reason for the redesign: "old issues continue to clutter Issue streams", and
   users would not ignore intermittent issues because they were "worried about missing any potential
   spikes" (https://github.com/getsentry/sentry/discussions/43039 — a proposal, not product docs).
4. **Archive is a conditional mute, and the default condition is "until it gets worse".** Archive
   will "move it out of the issue stream and pause alerts on it until the issue gets worse"; the
   alternatives are forever, for a period, "Until it occurs a set number of times", or until N users
   are affected (states-triage page). The escalation threshold is per issue, from the previous
   week's hourly counts: for issues at least 7 days old `max(spike, bursty)`, where the bursty limit
   exists so cron-like bursts are not flagged; for younger issues `max_hourly * 10`
   (https://docs.sentry.io/product/issues/states-triage/escalating-issues/).
5. **Auto-resolve is by silence.** Project field `resolveAge`: "Automatically resolve an issue if it
   hasn't been seen for this many hours. Set to `0` to disable"
   (https://docs.sentry.io/api/projects/update-a-project/; the default is *unverified*).
6. **Regression is by version, not by time.** "A plain **Resolve** treats any later event as a
   regression." Resolved in a release: only events from a **greater** release regress it — "If the
   release version is using semver ... greater than the resolved version. If the release is not
   using semver, the release creation dates will be compared"; a commit message `fixes <SHORT-ID>`
   resolves the issue only "when that commit ... is part of a release"
   (https://docs.sentry.io/product/releases/,
   https://sentry.io/changelog/2023-8-7-improvements-to-resolving-issues-in-a-release/,
   https://docs.sentry.io/organization/integrations/source-code-mgmt/github/). The point: leftover
   events from old code are expected and stay quiet.
7. **Routing assigns once.** Ownership rules `type:pattern owners`, CODEOWNERS first, "The **last
   rule that matches** is used"; once an issue is assigned "future auto-assignment will be turned off
   for that issue"; and auto-assignment can be skipped when a project is "creating too many new
   issues", for which the advice is to fix grouping
   (https://docs.sentry.io/product/issues/ownership-rules/). Suspect commit = blame of the first
   in-app frame (https://docs.sentry.io/product/issues/suspect-commits/). Priority follows level
   (ERROR/FATAL high, WARNING medium, DEBUG/INFO low), escalation bumps it, and after a manual
   change "it will no longer be automatically adjusted"
   (https://docs.sentry.io/product/issues/issue-priority/).
8. **Tickets are a second layer, linked and synced.** Creation is manual ("Linked Issues") or an
   alert action "Create a new Jira issue"; Jira syncs comments, assignee and status both ways
   (https://docs.sentry.io/organization/integrations/issue-tracking/jira/); for Linear only creation
   is documented on Sentry's side
   (https://docs.sentry.io/organization/integrations/issue-tracking/linear/), and Linear documents
   the one direction it syncs: "When a Linear issue is completed, any linked Sentry issues will be
   automatically resolved" (https://linear.app/docs/sentry). Alert triggers are
   `first_seen_event`, `regression_event`, `reappeared_event`, `issue_resolved_trigger`, with a
   per-rule action `frequency` in minutes from a fixed set (0, 5, 10, 30, 60, 180, 720, 1440)
   (https://docs.sentry.io/api/monitors/create-an-alert-for-an-organization/).
9. **Volume protection.** Spike protection discards events above a per-project baseline and warns
   that sustained volume "will become your new baseline"
   (https://docs.sentry.io/pricing/quotas/spike-protection/).

### 3.2 Rollbar and Bugsnag, where they differ

10. **Rollbar's fingerprint strips what varies**: SHA1 of filenames and method names of all frames
    plus the exception class, line numbers excluded, dates and shas stripped from paths, integers of
    two or more digits stripped from messages, and "Items are divided by environment" first
    (https://docs.rollbar.com/docs/grouping-algorithm).
11. **The asymmetry that should set the default.** Under-grouping is clutter and is fixed by merging,
    which applies to "past and future occurrences"; over-grouping "is a much more serious problem ...
    items cannot be split apart" (https://docs.rollbar.com/docs/error-grouping-best-practices).
12. **Rollbar states and their notifications.** Active / Resolved / Muted; a resolved item that
    recurs "in a newer version of the code (or no version was specified)" is **Reactivated**
    (https://docs.rollbar.com/docs/status-active-resolved-muted); `resolved_in_version` is compared
    with each occurrence's `code_version` (https://docs.rollbar.com/docs/resolved-in-version).
    Auto-resolve is per environment, after N inactive days or "whenever you report a deploy", and
    **sends no email or Slack notification** (https://docs.rollbar.com/docs/auto-resolve-old-items).
    Snooze is time-limited and was added because muted items could keep occurring "for an indefinite
    period" (https://docs.rollbar.com/docs/item-snooze). Notification triggers include **10^nth
    occurrence** ("10th, 100th, 1,000th ...") and "{x} occurrences seen in {y} minutes"
    (https://docs.rollbar.com/docs/notifications).
13. **Bugsnag** reopens a Fixed error only "if the error occurs again in a version released after
    the most recent version it previously appeared in"; Ignored "can only be reopened manually"
    (https://docs.bugsnag.com/product/error-status-and-actions/). Its Jira integration creates
    issues "for each new error" or "when a threshold is reached" and syncs both ways
    (https://docs.bugsnag.com/product/integrations/issue-tracker/jira/). Snooze-until conditions
    (N more occurrences, N in M hours) were seen only in a secondary listing: *unverified*.

## 4. Alerting: a state, not an event

### 4.1 Prometheus and Alertmanager

14. **Three states, and pending is silent.** inactive → pending → firing; `for:` (default `0s`)
    "causes Prometheus to wait for a certain duration between first encountering a new expression
    output vector element and counting an alert as firing"
    (https://prometheus.io/docs/prometheus/latest/configuration/alerting_rules/). A pending alert
    whose series goes away is simply deleted, with no resolved notification (*source*,
    `rules/alerting.go`).
15. **Debounce on the way out too.** `keep_firing_for:` (default `0s`) exists "to prevent situations
    such as flapping alerts, false resolutions due to lack of data loss, etc."; without it alerts
    "deactivate on the first evaluation where the condition is not met" (same page).
16. **Resolution is absence, which is only sound because every evaluation is complete.** An alert
    ends when the rule stops returning that label set; on the Alertmanager side "Firing alerts are
    resolved once their endsAt timestamp has elapsed", and clients must "re-send firing alerts… at
    regular intervals" (https://prometheus.io/docs/alerting/latest/alerts_api/).
17. **Identity is the label set; everything descriptive is an annotation.** "Labels are used to
    deduplicate identical instances of the same alert, while annotations are used to include other
    information… such as a summary, description or a URL to a runbook"
    (https://prometheus.io/docs/alerting/latest/alertmanager/). Annotations are templated with the
    current value, so putting them in the key would make a new alert at every evaluation.
18. **Grouping timers.** `group_wait` default `30s` — "If an alert is resolved before group_wait has
    elapsed, no notification will be sent for that alert. This reduces noise of flapping alerts" —
    `group_interval` default `5m`, `repeat_interval` default `4h`
    (https://prometheus.io/docs/alerting/latest/configuration/).
19. **Inhibition is the mass-failure rule.** "An alert is firing that informs that an entire cluster
    is not reachable. Alertmanager can be configured to mute all other alerts concerning this
    cluster." Fields `source_matchers`, `target_matchers`, `equal`; an alert matching both sides
    cannot inhibit itself. Inhibited alerts are **kept**, in state `suppressed` with an
    `inhibitedBy` list (https://prometheus.io/docs/alerting/latest/configuration/,
    https://github.com/prometheus/alertmanager/blob/main/api/v2/openapi.yaml).
20. **A silence always expires.** Required fields `matchers`, `startsAt`, `endsAt`, `createdBy`,
    `comment` (same OpenAPI file). There is no acknowledge, assign or close in the API: ownership is
    deliberately someone else's layer, "Another layer is needed to add summarization, notification
    rate limiting, silencing and alert dependencies" (alerting rules page).

### 4.2 PagerDuty, Opsgenie / Jira Service Management

21. **`dedup_key` is an open-item key.** A `trigger` with the key of an open alert adds a log entry
    to it; after resolve, the same key "will create a new alert", while late `acknowledge`/`resolve`
    events are dropped; an acknowledged incident "won't generate any additional notifications, even
    if it receives new trigger events" (https://developer.pagerduty.com/docs/send-alert-event, read
    from https://github.com/PagerDuty/developer-docs/blob/main/docs/events-API-v2/02-Trigger-Events.md
    because the site would not render).
22. **Auto-pause is `for:` by another name, and only machine recoveries count.** Pause "2, 3, 5,
    10, or 15 minutes"; an alert that resolves inside the pause becomes "Resolved (Suppressed) and
    have no associated PagerDuty incident", and "Alerts that receive a resolve action made manually
    by a responder are not classified as transient"
    (https://support.pagerduty.com/main/docs/auto-pause-incident-notifications). Auto-resolve is
    "By default… not turned on"; "Snoozed incidents reset the auto-resolution timer"
    (https://support.pagerduty.com/main/docs/configurable-service-settings).
23. **Grouping rolls many alerts into one incident**: time-based, content-based (`all`/`any` over
    named fields, a rolling window that "extends each time… up to 24 hours") or intelligent
    (https://support.pagerduty.com/main/docs/content-based-alert-grouping).
24. **Opsgenie/JSM `alias`.** "There can be at most one open alert with the same alias at any time";
    a duplicate increments `Count`, and "Count property and logging… stops after 100 occurrences;
    however, de-duplication still continues"
    (https://support.atlassian.com/opsgenie/docs/what-is-alert-de-duplication/,
    https://support.atlassian.com/jira-service-management-cloud/docs/what-is-alert-deduplication/).
    Policies: delay notification "unless the deduplication count equals a particular value";
    auto-close "after a set period following its last occurrence... the close timer resets each time
    the alert is deduplicated"
    (https://support.atlassian.com/jira-service-management-cloud/docs/what-are-notification-policies/).
    Opsgenie itself shuts down on 2027-04-05; JSM carries the model
    (https://www.atlassian.com/software/opsgenie/migration).

### 4.3 Nagios and Grafana: the two ideas nobody else states as clearly

25. **Soft states are where automatic repair runs.** A state stays *soft* until the check has
    failed `max_check_attempts` times; during a soft state "the only important thing that really
    happens… is the execution of event handlers"
    (https://assets.nagios.com/downloads/nagioscore/docs/nagioscore/4/en/statetypes.html). That is a
    pending signal an agent may already fix before any human surface shows it.
26. **Flap detection is a count over the last 21 results with hysteresis.** Up to 20 transitions,
    the newest weighted 50% more than the oldest, flapping starts at or above a high threshold and
    stops only below a low one (compiled defaults 30%/20%, *source* `include/defaults.h`); while
    flapping, Nagios sends one "flapping start" and will "Suppress other notifications"
    (https://assets.nagios.com/downloads/nagioscore/docs/nagioscore/4/en/flapping.html). It is the
    same statistic as TestGrid's flake score, used to stop an item opening and closing.
27. **"Could not evaluate" is its own state.** Grafana has Normal / Pending / Alerting / Recovering
    / **No Data** / **Error**; an errored rule can "Keep Last State", and a series missing for two
    evaluations is evicted as MissingSeries rather than resolved
    (https://grafana.com/docs/grafana/latest/alerting/fundamentals/alert-rule-evaluation/).

## 5. Test failure to ticket

### 5.1 Chromium LUCI Analysis

Read from the project's proto and Chromium's live config (*source*):
https://chromium.googlesource.com/infra/luci/luci-go/+/refs/heads/main/analysis/ and
https://chromium.googlesource.com/chromium/src/+/refs/heads/main/infra/config/generated/luci/luci-analysis.cfg

28. **The key is a normalised failure reason, not the test.** The reason clusterer replaces
    base64-like runs, long hex and **every number** with `%`, applies project mask patterns and
    hashes the result; a second clusterer keys on the test name, with `test_name_rules` collapsing
    parameterised variants. When a bug is filed the cluster is frozen into a **rule**
    (`reason LIKE "<pattern>"`), and rule ↔ bug is 1:1: the rule is the durable key, and it is
    editable by a person.
29. **Nothing is filed twice.** Suggested clusters are measured "after excluding failures for which
    a bug has already been filed"; at most one bug is filed per run; and before filing it re-checks
    that the example failure still hashes to the cluster, because a mismatch "could result in
    indefinite creation of new bugs".
30. **Policies have separate activation and deactivation thresholds.** `BugManagementPolicy {id,
    owners, human_readable_name, priority, metrics[] {metric_id, activation_threshold,
    deactivation_threshold}}`, thresholds over `one_day` / `three_day` / `seven_day`; a policy "will
    activate if the activation threshold is met on *ANY* metric, and will de-activate only if the
    deactivation threshold is met on *ALL*", with the instruction to set deactivation "significantly
    lower". Chromium's production values:

    | Policy | Priority | Metric | Activate | Deactivate |
    |---|---|---|---|---|
    | `cq-rejects` | P0 | human CLs failing presubmit | 10 in 1 day | below 3 in 1 day |
    | `builds-failed-due-to-flaky-tests` | P1 | same | 7 in 3 days | below 1 in 3 days |
    | `exonerations` | P2 | critical failures exonerated | 30 in 3 days | below 1 in 7 days |

    The unit is **impact on people's work**, not failure count.
31. **Close, reopen, retire.** With no policy active the bug goes to VERIFIED; re-activation reopens
    the *same* bug; the rule is archived 30 days after that, after which a recurrence files a new
    bug. Priority is the highest among active policies, and a human priority edit switches priority
    management off. A human close that the data contradicts is handled by
    `bug_closure_invalidation_action` ∈ `no_validation | reopen_bugs | file_new_bugs`.
32. **Exoneration**: a test failing 6 of its last 10 runs, 3 consecutively, or flaking 3 times is not
    held against the change being tested (`TestStabilityCriteria`) — and the exoneration count is
    itself what files the P2 bug. "Known bad, do not blame this change, but count it."

### 5.2 Mozilla Treeherder

Read from https://github.com/mozilla/treeherder (*source*) and https://wiki.mozilla.org/Bug_Triage.

33. **Identity is the summary string**, ``Intermittent <test> | <message>``, keyword
    `intermittent-failure`, default priority P5.
34. **There is a pre-ticket tier.** An "internal issue" is a row with no Bugzilla id, found by exact
    summary; a Bugzilla bug is offered only after `requiredInternalOccurrences = 3` within a 7-day
    window. This is the proposal's signal-then-promotion, with a number on it.
35. **Escalation bands, with a gap.** The robot comments weekly; at ≥ 30 failures a week it tags
    `[stockwell needswork:owner]` and resets priority for triage; at ≥ 75 a week the deadline
    shortens; at ≥ 150 in 21 days `[stockwell disable-recommended]`; the tag clears below 20 a week
    — up at 30, down at 20.
36. **Auto-close**: "no new failures in 21 days ... and have not been reopened in the past week" →
    INCOMPLETE. **Mass bustage** is a classification, `fixed by commit`, not N bugs.

### 5.3 Kubernetes

37. **TestGrid alerts are consecutive counts both ways.** `num_failures_to_alert` consecutive
    failures open, `num_passes_to_disable_alert` "consecutive test passes to close the alert",
    `alert_stale_results_hours` alerts when results stop arriving; `max_allowed_individual_bugs`
    files **one** bug to the on-call instead of many above N failures, "useful for filing fewer
    suspected environmental failure bugs"
    (https://github.com/GoogleCloudPlatform/testgrid/blob/master/pb/config/config.proto).
38. **The issue filer never reopens.** Its dedupe key is an `ID()` string embedded in the issue body
    ("DO NOT CHANGE how this ID is formatted"); an open match is left alone, a match closed inside
    the window files nothing, otherwise a new issue lists "Previously closed issues"; top three
    clusters per run (*source*, `robots/issue-creator` in kubernetes/test-infra; deployment status
    *unverified*). Triage normalises failure text by replacing timestamps, hex, IPs, UUIDs and
    random suffixes before clustering. Human policy: "don't just close it!"
    (https://github.com/kubernetes/community/blob/master/contributors/devel/sig-testing/flaky-tests.md).

### 5.4 Trunk, Datadog, Buildkite

39. **Trunk**: status comes from monitors with **separate activation and resolution thresholds**
    (30% / 15% recommended as a "buffer zone"; pass-on-retry recovers after 7 quiet days); an upload
    with more than **80% of tests failing is excluded from detection entirely** (Infrastructure
    Failure Protection); ticketing is "One ticket per test", off by default; it "**Closes the
    ticket** when the test returns to healthy", keeps the link, reopens a ticket closed within the last 30 days
    (configurable 1–99, measured from the tracker's close date so a person's close counts too) and
    otherwise will "unlink it and create a fresh ticket"; descriptions are refreshed in place rather
    than commented on, and title edits made by people are preserved
    (https://docs.trunk.io/flaky-tests/management/ticketing/automatic-ticketing);
    quarantine overrides the exit code only when every failure in the job is quarantined, and
    "Broken tests are not quarantine candidates" (https://docs.trunk.io/flaky-tests).
40. **Datadog**: key = hash of repository id and the test's fully qualified name; states Active /
    Quarantined / Disabled / **Fixed**; Fixed automatically after 30 days without a flake, or by
    *fix verification* — the test is retried **20 times on the fix commit**; ticket creation is a
    manual "Create Work Item"; notifications are a weekly digest by code owner
    (https://docs.datadoghq.com/tests/flaky_management/).
41. **Buildkite**: alarm and recover are separate events per test and "a recover can only follow an
    alarm"; passed-on-retry recovers after "seven days or 100 executions"; actions are label, state
    (enabled / muted / skipped), webhook, Slack, create a Linear issue; above 500 events a minute
    notifications are suppressed while label and state actions continue; a **skipped test produces
    no data and so cannot auto-recover** (https://buildkite.com/docs/test-engine/workflows).

### 5.5 Repository bots

42. **Go `watchflakes`** keeps no state of its own: the key is a small matching script in the issue
    body (`#!watchflakes` / `post <- pkg == "net/http" && test == "X"`), a failure matching no issue
    files one, a failure matching a closed issue reopens it, and it never closes. Its mass-failure
    filter is the clearest anywhere: a failure is ignored when the whole builder is red, when the
    commit failed on four or more builders, or when it is part of a run of four or more failing
    commits (https://go.googlesource.com/wiki/+/refs/heads/master/Watchflakes.md).
43. **Flutter's bot** (*source*, https://github.com/flutter/cocoon): weekly, files when the flaky
    rate is ≥ 2%, **at most two issues per run**, skips a test whose issue was closed in the last 15
    days, escalates to P0 if still above threshold after 7 days, and closes after **50 consecutive
    passes**.
44. **Kibana's failed-test reporter** (*source*,
    https://github.com/elastic/kibana/tree/main/packages/kbn-failed-test-reporter-cli): key =
    `test.class` + `test.name` in the issue body, `test.failCount` incremented per failure, a closed
    issue is reopened on the next failure, no auto-close.
45. **Generic Actions** key on whatever is to hand and none closes anything:
    `JasonEtco/create-an-issue` on the exact title (`update_existing`),
    `jayqi/failed-build-issue-action` on a label (comment on the latest open issue with it),
    `xarray-contrib/issue-from-pytest-log` on title + label, updated in place.

## 6. Project managers: where machine-written items are allowed to land

46. **Linear Triage** is a separate status category that "acts as an Inbox for your team"; issues
    land there when "created through an integration (e.g. Slack, Sentry)" or by non-members. Four
    verbs: **accept**, **mark as duplicate** (merges into an existing issue), **decline**, **snooze**
    — "hide the issue from the triage queue to return at a time of your choosing, or when there's
    new activity on that issue" (https://linear.app/docs/triage). Plane's Intake is the same lane
    with the same four verbs (https://docs.plane.so/core-concepts/intake).
47. **Linear auto-close and auto-archive** are on for new teams
    (https://linear.app/changelog/2020-08-19-auto-close-and-auto-archive); auto-close waits while an
    issue is in an active cycle or has an SLA (https://linear.app/docs/delete-archive-issues). The
    default periods are *unverified*. Attachments dedupe per issue on URL
    (https://linear.app/developers/attachments), which dedupes the link, not the issue.
48. **Jira has no built-in dedupe for webhook-created issues**; the community pattern is a JQL
    lookup, then comment instead of create. JSM does the keyed part in a separate object — the
    **alert** (§4.2) — and **"Create incident" is a promotion** from one or several alerts, which
    stay attached under "Linked alerts"
    (https://support.atlassian.com/jira-service-management-cloud/docs/create-incidents-with-alerts/).
    Datadog's Jira integration: "New issues are not created by the monitor until the monitor is
    resolved" (https://docs.datadoghq.com/integrations/jira/).
49. **GitHub's swamp protection is caps and one rolling item.** Dependabot: five open version-update
    PRs by default, `groups` to batch, and it pauses entirely after 90 days with no human action
    (https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference).
    Renovate: `prConcurrentLimit` 10, `prHourlyLimit` 2, and the Dependency Dashboard — **one issue,
    rewritten in place**, whose checkboxes are the commands
    (https://docs.renovatebot.com/configuration-options/). `actions/stale`: stale at 60 days, closed
    7 days later as `not_planned`, `exempt-issue-labels` to opt out
    (https://github.com/actions/stale).

## 7. Code scanning: the nearest precedent for machine-written, machine-closed

50. **`fixed` is a state only the machine can set.** GitHub code scanning alerts are `open`,
    `dismissed` or `fixed`; the API's PATCH accepts only `open` and `dismissed`. Dismissal needs a
    `dismissed_reason` ∈ `false positive` | `won't fix` | `used in tests` (the API also lists
    `mitigated`) and takes a `dismissed_comment`
    (https://docs.github.com/en/rest/code-scanning/code-scanning). Webhook actions keep the two
    authors apart: `fixed` and `reopened` ("A previously fixed code scanning alert reappeared in a
    branch") against `closed_by_user` and `reopened_by_user`
    (https://docs.github.com/en/webhooks/webhook-events-and-payloads#code_scanning_alert).
51. **Identity is rule + stable path + a content hash, never a line number.** "The ruleId for a
    result has to be the same across analysis"; if paths change "a new alert will be created, and
    the old one will be closed"; `partialFingerprints` "identify which results are the same across
    commits and branches"
    (https://docs.github.com/en/code-security/code-scanning/integrating-with-code-scanning/sarif-support-for-code-scanning).
    Semgrep's `match_based_id` is the same idea: path, rule and the matched pattern plus an index, no
    line number (https://docs.semgrep.dev/semgrep-code/remove-duplicates).
52. **Not scanned is never fixed.** An alert closes only when absent from a later analysis of the
    same tool, category and ref; "Unless you run all configurations regularly, you may see alerts
    that are fixed in one configuration but not in another", and an alert from a configuration that
    stopped running stays open until a person deletes that configuration
    (https://docs.github.com/en/code-security/code-scanning/managing-code-scanning-alerts/resolving-code-scanning-alerts).
    Semgrep names the third case: **Fixed** = "no longer detected in the most recent scan of that
    same branch", **Removed** = gone because the rule or the file went away, and Removed findings
    "do not count toward the fix rate" (https://docs.semgrep.dev/semgrep-code/triage-remediation).
    SonarQube conflates them (Fixed when "the issue has been corrected or the file is no longer
    available") and deletes closed issues after 30 days
    (https://docs.sonarsource.com/sonarqube-server/10.4/user-guide/issues).
53. **Status is per branch, and humans see the default branch.** "Alerts may be fixed in one branch
    but not in another"; the list defaults to the default branch (same resolving-alerts page).
54. **Only new findings block.** A pull request is annotated only with "new alerts on lines of code
    changed in the pull request" above a severity threshold
    (https://docs.github.com/en/code-security/concepts/code-scanning/merge-protection); SonarQube's
    default gate is "No new issues are introduced", on new code only
    (https://docs.sonarsource.com/sonarqube-server/10.4/user-guide/quality-gates). Pre-existing
    findings are a backlog worked by campaign or assignment, never a gate.
55. **Dependabot alerts add a machine dismissal.** States `open` / `dismissed` / `fixed` /
    `auto_dismissed`; `fixed` = "A manifest file change removed a vulnerability"; rules can dismiss
    "indefinitely or until a patch is available", and `auto_reopened` brings the alert back when
    that condition changes; dismissal reasons `fix_started`, `inaccurate`, `no_bandwidth`,
    `not_used`, `tolerable_risk` (https://docs.github.com/en/rest/dependabot/alerts). Secret
    scanning, by contrast, has no machine-closed state at all: `open` / `resolved` with a human
    `resolution` (https://docs.github.com/en/rest/secret-scanning/secret-scanning).

## 8. AI coding harnesses: nobody keeps the record

The question put to each product: does it keep a persistent, deduplicated record of failures the
agent discovered, separate from human tasks; how does a failure reach the agent; how does it know the
failure is fixed. "Absent" below means the named page was read and says nothing of the kind.

56. **Claude Code.** Failures reach the model in-loop through hooks: exit code 2 from a `Stop` hook
    "Prevents Claude from stopping, continues the conversation", and from `TaskCompleted` "the task
    is not marked as completed and the stderr message is fed back to the model as feedback"
    (https://code.claude.com/docs/en/hooks). Its task list persists across compactions and, in agent
    teams, "Task claiming uses file locking" (https://code.claude.com/docs/en/agent-teams) — but
    tasks are model-written, with no key, no machine writer and no auto-close. On the web, auto-fix
    subscribes to the agent's own PR: "when a check fails … investigates and pushes a fix if one is
    clear", per PR, with duplicates handled by "Claude notes it in the session and moves on"
    (https://code.claude.com/docs/en/claude-code-on-the-web#auto-fix-pull-requests). Code Review
    has the one relevant class, "Pre-existing: A bug that exists in the codebase but was not
    introduced by this PR", and "the next run resolves the thread when the issue is fixed"
    (https://code.claude.com/docs/en/code-review) — PR-scoped, so a pre-existing finding has no home
    once the PR merges. Absent: any failure registry.
57. **Codex.** The documented flow is a GitHub Actions recipe: `workflow_run` with
    `conclusion == 'failure'`, reproduce, minimal fix, re-run, open a PR on
    `codex/auto-fix-$RUN_ID` — one PR per failed run, no record, no dedupe
    (https://learn.chatgpt.com/docs/non-interactive-mode). Automations have no CI-failure trigger
    (https://learn.chatgpt.com/docs/automations).
58. **Cursor.** Bugbot's findings are PR comments with a per-bug `resolution_status`, and its check
    passes when there are "no unresolved Bugbot comments from earlier runs"
    (https://cursor.com/docs/bugbot); how a finding becomes resolved is *unverified*. Automations
    trigger on GitHub "CI completed", Sentry "Issue created" and PagerDuty "Incident triggered";
    memory between runs is a free-text file (https://cursor.com/docs/cloud-agent/automations).
59. **Devin.** Automations start or message a session on "**Check run (CI)** … Filter by
    `conclusion = failure`", with the event payload appended to the prompt; the storm control is an
    invocation limit ("at most 10 invocations per hour"), a rate limit rather than identity
    (https://docs.devin.ai/product-guides/automations). Auto-triage "intelligently deduplicates
    repeated reports" through a shared scratchpad
    (https://docs.devin.ai/product-guides/auto-triage) — the nearest thing to a registry inside any
    harness, but its input is people's Slack reports, the dedupe is the model's judgement, and
    nothing closes an item.
60. **Jules** "fixes CI failures on pull requests it creates … operates in a loop"
    (https://jules.google/docs/changelog/2026-02-19); its suggested tasks are a machine-written
    queue with start/dismiss, but for `#TODO` comments, not failures
    (https://jules.google/docs/suggested-tasks).
61. **GitHub Copilot.** The complete loop exists, but the record is code scanning's, not the
    agent's: assign an alert to Copilot, it "validates it, and opens a pull request", and "If your
    changes fix the problem, the alert is closed"
    (https://docs.github.com/en/code-security/code-scanning/managing-code-scanning-alerts/resolving-code-scanning-alerts).
    GitHub is explicit that the two layers are not synced: "Alert and issue statuses are not
    automatically synchronized … the linked issue remains open until you manually close it"
    (https://docs.github.com/en/code-security/concepts/code-scanning/code-scanning-alert-tracking-using-issues).
    GitHub Next's CI Doctor opens one `[CI failure]` issue per root cause, where the dedupe is a
    sentence in the prompt — "If an open issue already reports the same root cause, add one comment
    … Do not create another issue" — and nothing closes it when CI goes green
    (https://github.com/githubnext/agentics/blob/main/workflows/ci-doctor.md).
62. **The rest.** Aider feeds `--test-cmd` output back in-loop and keeps nothing
    (https://aider.chat/docs/usage/lint-test.html). OpenHands has no check-run trigger
    (https://docs.openhands.dev/openhands/usage/automations/event-automations). Factory's webhook
    log marks deliveries **Deduped** (identical body), **Capped** (10 an hour) and **Skipped** (a run
    in flight) (https://docs.factory.ai/software-factory/automations). Amp has schedules only
    (https://ampcode.com/docs/markdown/orbs/automations). Warp's public docs list
    `workflow_run_completed` and `check_suite_completed` triggers with a conclusions filter and a
    `ci-failure-triage` automation — "If the cause is small and clear, open a fix PR. Otherwise open
    an issue" — where "each match starts its own run"
    (https://docs.warp.dev/factories/integrations/github/). Sweep's docs would not load.
63. **Where the record does live, and how those products verify a fix.** Trunk hands a flaky test
    to an agent by webhook or MCP tool (`fix-flaky-test`) on each Healthy → Flaky/Broken transition
    (https://docs.trunk.io/flaky-tests/agents/autofix-flaky-tests). Datadog retries the test 20
    times on a commit carrying the test's key, then waits for the merge (§5.4; its agent runs "in
    response to signals from Datadog products", https://docs.datadoghq.com/bits_ai/bits_code/).
    CircleCI's Chunk runs a fixed flaky test **10 times by default (1–20) before it opens the PR**,
    fixes at most 1–3 tests per run and caps its open PRs
    (https://circleci.com/docs/guides/test/fix-flaky-tests/). Sentry's Seer starts only when an issue
    has at least 10 events, is recent and scores as fixable, and closes through `Fixes SENTRY-317` →
    resolved in release → Regressed if it recurs
    (https://docs.sentry.io/product/ai-in-sentry/seer/autofix). Gitar stops after "three consecutive
    Gitar commits" (https://docs.gitar.ai/features/ci-failure-analysis).

**The answer.** No harness keeps the record. What they have instead is weaker in three recognisable
ways: **PR-scoped finding state** that dies with the PR (Claude Code Review threads, Bugbot
`resolution_status`); **free-text memory judged by the model** (Devin's scratchpad, Cursor's
memories file, CI Doctor's "search existing issues"); and **rate limits where identity should be**
(Devin's invocation limit, Factory's Capped). Five hand-off shapes recur: in-loop hook feedback; an
event starts a session with the payload in the prompt; a subscription on the agent's own PR; assign
or label the tracked item; a scheduled sweep of the top unresolved items. "Fixed" is known by CI
going green on the PR, by the next scan lacking the fingerprint, by N passing reruns, by a quiet
period, or by a commit assertion plus a regression watch. What is absent everywhere, and what a
local store in an agent terminal adds: a record that exists **before CI and before a PR**, that the
agent itself reads, claims and closes, that gives pre-existing failures a home outside any PR, and
that closes by re-running the exact check on the spot.

## 9. Comparison

Abbreviations: *cons.* = consecutive; *occ.* = occurrences. Empty means the docs read say nothing.

| System | Identity / dedupe key | States | Opens | Closes | Reopen / regression | Flap protection | Mass failure | How humans are shielded | Escalation to a human work item |
|---|---|---|---|---|---|---|---|---|---|
| **Sentry** | fingerprint: custom → stack trace → exception → message without parameters | unresolved (new, ongoing, escalating, regressed), archived, resolved | first event | human resolve; `resolveAge` hours unseen; commit `fixes ID` once released | any later event, or only events from a greater release | archive until escalating (per-issue forecast from last week's hourly counts) | spike protection discards above baseline | For Review queue; archive leaves the stream; alert `frequency` | alert action or manual link creates Jira/Linear/GitHub issue; status synced |
| **Rollbar** | SHA1 of frames + class, numbers and line numbers stripped, per environment | active, resolved, muted | first occurrence | human; N days inactive; on deploy — silently | occurrence with `code_version` ≥ resolved version → Reactivated | time-limited snooze | — | 10^n-th occurrence and rate notifications | rules create tracker issues; reopened on reactivation |
| **Prometheus + Alertmanager** | label set | inactive, pending, firing; suppressed | condition true for `for:` | condition absent (complete evaluation), after `keep_firing_for` | same labels fire again (no memory) | `for:`, `keep_firing_for`, `group_wait` 30 s | inhibition: source alert mutes targets sharing `equal` labels | grouping, `repeat_interval` 4 h, silences with required expiry | none by design |
| **PagerDuty** | `dedup_key`, among open alerts | triggered, acknowledged, resolved; suspended | trigger event | resolve event; optional auto-resolve timer | same key after resolve = new alert | auto-pause 2–15 min: machine-resolved inside it never becomes an incident | time / content / intelligent grouping into one incident | suppress rules; ack stops notifications | alert → incident is the promotion |
| **Opsgenie / JSM alerts** | `alias`, one open alert per alias | open, acked, closed | first event | close by alias; auto-close X after last occurrence (timer resets on repeat) | closed alias = new alert | delay until dedup count = N, or still open after X | — | `Count` instead of new alerts; log capped at 100 | "Create incident" from one or several alerts, kept linked |
| **Nagios** | host + service | OK / non-OK × soft / hard; flapping | hard after `max_check_attempts` | next OK check | — | weighted transitions over last 21 checks, high/low thresholds | host down ⇒ services not notified | one "flapping start", then silence | none |
| **LUCI Analysis** | normalised failure reason (numbers/hex → `%`) or test name; frozen into an editable rule | policy active / inactive per bug | impact over 1/3/7 days ≥ activation (10 CLs/day P0; 7 builds/3 d P1; 30 exonerations/3 d P2) | all metrics below deactivation (3; 1; 1) → VERIFIED | same bug reopened; rule archived 30 d after close, then a new bug | activation ≫ deactivation | one cause = one cluster = one bug; 1 bug filed per run | only clusters over an impact threshold become bugs | the bug *is* the promotion; priority = highest active policy, frozen by a human edit |
| **Mozilla Treeherder** | summary string `Intermittent <test> \| <message>` | internal issue → bug; classifications | 3 classifications in 7 days | 21 days without failures → INCOMPLETE | — (*unverified*) | escalation up at 30/week, down at 20 | `fixed by commit` classification; tree closure | P5 by default; robot comments weekly, daily only ≥ 15/day | 30/week → needswork, 75/week → shorter deadline, 150/21 d → disable-recommended |
| **TestGrid / k8s filer** | test row; filer: `ID()` string in issue body | alert on / off | `num_failures_to_alert` cons. failures | `num_passes_to_disable_alert` cons. passes | filer never reopens; new issue if last close older than window | cons. counts both ways | `max_allowed_individual_bugs` → one bug to on-call; top 3 clusters per run | stale-results alert separate | issue with `kind/flake`; stale 90 d → rotten 30 d → closed 30 d |
| **Go watchflakes** | matching script in the issue body | issue open / closed | 1 failure passing the filters | never (human) | any new failure reopens | — | ignored if builder all red, ≥ 4 builders failed the commit, or ≥ 4 failing commits in a row | separate Test Flakes project | it is an issue from the start |
| **Flutter cocoon** | test `name` tag in issue body | issue open / closed; `bringup: true` | flaky rate ≥ 2% over ≤ 100 commits, weekly | 50 cons. passes | new issue only if last close > 15 d ago | rate over window; 15-day grace | max 2 issues per run | test marked `bringup` (non-blocking) | P0 if still over threshold after 7 d |
| **Trunk** | test case | healthy, flaky, broken; quarantined | monitor activates (pass-on-retry ≥ 1; rate e.g. 30%) | monitor resolves (7 quiet days; rate < 15%) | ticket reopened if closed ≤ 30 d ago, else fresh ticket | separate activate / resolve thresholds | upload with > 80% failing excluded entirely | quarantine; quiet in-place ticket updates; drained at provider rate limit | one ticket per test, auto-created and auto-closed (opt-in) |
| **Datadog** | hash(repo, fully qualified test name) | active, quarantined, disabled, fixed | 1 flake | 30 days without a flake; or 20 passing retries on the fix commit | — (*unverified*) | same-commit definition | — | weekly digest by code owner; quarantine | manual "Create Work Item" |
| **Buildkite** | test | enabled, muted, skipped; alarm / recover | monitor alarm (pass+fail on one SHA; transition count) | recover: 7 days or 100 executions; recover threshold < alarm | — | separate thresholds; recover only after alarm | > 500 events/min: notifications suppressed | labels and mute | action: create Linear issue |
| **Linear Triage / Plane Intake** | none (duplicates merged by a person or suggestion) | triage → accepted / duplicate / declined / snoozed | any integration-created issue | human; auto-close when stale | snooze returns on new activity | — | — | separate inbox status outside the workflow | accept = promotion |
| **GitHub code scanning** | rule id + path + `partialFingerprints` content hash; per ref, tool, category | open, fixed (machine only), dismissed (reason + comment) | finding in an analysis | absent from the next analysis of the same tool/category/ref | reappearance → `reopened`; dismissed stays dismissed | — | — | PRs show only new alerts in the diff; default-branch view | tracking issue or campaign; assign to Copilot; not status-synced |
| **Dependabot alerts** | advisory × manifest × package | open, fixed, dismissed, auto_dismissed | advisory matches manifest | manifest change removes it | `reintroduced`; `auto_reopened` when a rule's condition changes | — | grouped PRs; 5 open PRs cap | auto-triage rules dismiss before notifying | PR |
| **SonarQube / Semgrep** | rule + line-content hash / path + rule + matched pattern + index | open, accepted, false positive, fixed / + removed | analysis finds it | next analysis lacks it | Sonar reopens automatically | — | — | quality gate on new code only | — |
| **AI harnesses (§8)** | none | none | — | — | — | rate limits | — | — | a PR |

## 10. What the evidence says, against the proposal

**F1. Two layers, linked, is universal; the proposal has it right.** Alert/incident (PagerDuty,
JSM), alert/issue (GitHub), internal issue/bug (Mozilla), cluster/bug (LUCI), test status/ticket
(Trunk). The lower layer is keyed, counted and machine-closed; the upper layer is owned, discussed
and human-closed; the link survives on both.

**F2. The key is the check's identity; the failure text is an annotation.** Every system whose
unit is a *check that can pass* keys on the check: Prometheus label sets (17), TestGrid rows (37),
Trunk, Datadog, Buildkite, Flutter, Kibana (39–44), code scanning rule + location (51). Systems whose
unit is an *event that can only occur* key on a normalised failure fingerprint: Sentry, Rollbar,
LUCI, Mozilla, Kubernetes triage. A test is the first kind; a crash line in `relay.log` is the
second. Putting "first failing assertion" into a test's key, as the proposal offers, makes the key
move with every message edit and line shift, and the documented symptom of a moving key is an item
that closes and a twin that opens (51). The fingerprint is still worth computing — normalised the
LUCI/Rollbar way, numbers, hex, paths and timestamps stripped (10, 28, 38) — because it is what
**groups** keys that failed for one reason (F6) and what tells a claimed agent "this now fails
differently".

**F3. Everyone who auto-closes also debounces the open.** Prometheus `for:` and the silent deletion
of a pending alert (14), `group_wait` (18), PagerDuty's auto-pause (22), Nagios soft states (25),
Mozilla's 3-in-7-days internal issue (34), TestGrid's consecutive count (37), JSM's
"delay until dedup count = N" (24). The two that open on the first failure, Go and Kibana (42, 44),
never auto-close, run only on merged code in CI, and are read by people paid to read them. Relay's
runs are the opposite: a shared working tree holding several sessions' half-finished edits, where a
red test is often someone else's file saved a second ago.

**F4. Close on observed passes of the key, never on absence, unless the evaluation is complete.**
Prometheus closes on absence because each evaluation covers every series (16). GitHub does not:
absence only counts within the same tool, category and ref, and a configuration that stops running
leaves its alerts open (52). Semgrep separates Fixed from Removed (52); Buildkite notes that a
skipped test cannot recover (41); Grafana keeps the last state when it could not evaluate (27). Here
the normal run is `ctest -R <one name>`, so "two consecutive green *runs*" would either never be met
or be met by runs that never touched the test. The rule has to be "N consecutive passing
**executions of that key**", with not-built, not-collected, skipped and timed-out-by-harness counted
as *not evaluated*.

**F5. The number of passes that proves a fix depends on what was wrong.** For a deterministic
failure, consecutive counts in single digits (TestGrid's `num_passes_to_disable_alert`). For a
flaky test nobody accepts that: Flutter 50 consecutive passes (43), Datadog 20 retries on the fix
commit or 30 quiet days (40), CircleCI 10 validation runs before a PR (63), Buildkite 100 executions
or 7 days (41), Trunk 7 days (39). Two greens after a flake is what a flake looks like.

**F6. One cause must be one item, and three mechanisms do it.** *Inhibition* (19): a build or
collection failure is its own item and the tests under it are recorded as not evaluated, which is
also what Nagios does with a down host (25) and Grafana with Error (27). *Ratio cut-off*: Trunk
drops an upload with more than 80% failing (39), Go ignores a failure when the builder is all red or
four builders failed the commit (42), TestGrid files one bug above `max_allowed_individual_bugs`
(37). *Reason clustering* (28, 38). And a fourth, cruder guard that all the filers share: a **cap on
items created per run** — LUCI 1, Flutter 2, Kubernetes 3, Dependabot 5 open PRs, Renovate 2 an hour
(29, 43, 38, 49).

**F7. Machine-closed systems still need a human-only third state, with a reason, and the good ones
make it expire.** GitHub `dismissed_reason` + comment (50), Dependabot's reasons and "until a patch
is available" (55), Sonar Accepted / False Positive (52), Alertmanager silences whose `endsAt` is
required (20), Sentry's default archive "until escalating" (4), Rollbar's time-limited snooze added
because mute hid growth (12). Without it the only way to stop looking at a known-bad test is to
delete the test or let the fold fill with it.

**F8. Promotion is by impact or persistence of occurrences, never by age.** LUCI: people's changes
rejected (30). Mozilla: 3 in 7 days to exist, 30 a week to demand an owner (34, 35). Flutter: still
over the rate after 7 days (43). JSM: dedup count (24). Sentry's Seer: 10 events, recent, fixable
(63). "Older than a day" promotes exactly the signals nobody re-ran.

**F9. When the item and its ticket disagree, the machine state wins, and the careful systems say
what happens next.** LUCI re-checks a bug a person closed at 1, 3 and 7 days and reopens it or files
a new one (31). Trunk closes the ticket when the test is healthy and reopens it on regression (39).
GitHub declines to sync and documents that the issue stays open (61). The proposal's "the signal
stays open until that card closes" runs the authority the wrong way: a card can be closed while the
test is red, and a test can go green while the card is still in `executing`.

**F10. Reopen within a window, otherwise a new item that links the old one.** LUCI 30 days (31),
Trunk 30 (39), Flutter 15 (43), Kubernetes' filer by window with "Previously closed issues" (38).
PagerDuty and JSM never reopen (21, 24). Regression by version (6, 12, 13) matters when failures
arrive from old code; on a single `main` it reduces to recording `fixed_in`.

**F11. Only what is new blocks work.** GitHub annotates only alerts the PR introduced (54), Sonar's
gate is "No new issues are introduced" (54), Claude Code Review has a Pre-existing class (56), LUCI
exonerates known-bad tests and counts the exonerations (32). For Relay this is the join with #7BM4's
verification gate: a signal first seen in the run of the pane that holds a card is that card's
problem; one that was open before the claim is not.

**F12. Machine state lives in a store; git gets the promoted item.** No product surveyed writes
occurrences into the place people plan work. Renovate's dashboard is the limiting case — one issue
rewritten in place (49) — and Trunk edits the ticket description rather than commenting (39), both
to keep the write rate on the human surface near zero.

**F13. Auto-resolution is silent.** Rollbar sends nothing on auto-resolve (12); Prometheus sends
nothing for an alert that never left pending (14); PagerDuty's transient alerts never page (22).

## 11. Recommendations for the signal

**R1. Keep the concept; do not make it a card file.** A signal is a record in a local store, drawn
by the board as the folded row the proposal describes, and it becomes a card only by promotion
(F1, F12). Concretely: `issues/.private/signals/events.jsonl`, append-only, one line per transition
(`open`, `occurrence-summary`, `resolve`, `reopen`, `dismiss`, `claim`, `release`, `promote`,
`remove`), folded into the current state on read, beside #7BM4's `tests/history.jsonl`, from which
every test signal is derived. Reasons specific to this repo: `issues/` already takes 39% of all
commits (§2); every commit here goes through `land.py`'s claim-merge-swap, which a machine writing
after each test run would contend on from every pane; an append-only file tolerates the several
worker processes that will write to it where a rewritten card does not; and the test history it
derives from is already decided to be local and gitignored, so a committed signal would cite
evidence no other clone has. What is lost is sharing signals between machines — and a signal
describes *this* working tree's runs; the item another machine should see is the promoted card, or a
CI-sourced signal that every machine can recompute. `scripts/relay-board.py signals` and a
`board_signals` tool give agents without the GUI the same list.

**R2. Key = source + the check's own identity; fingerprint = annotation.** `ctest:panelayout`,
`unittest:tests.test_board.BoardTests.test_claim` (the per-case id from #7BM4's JUnit writer — not
the `backend-and-bash` ctest entry, which would make 3,350 cases one signal), `check:<code>:<path>`,
`build:<target>`, `crash:<signal>:<top in-repo frame>`. No commit and no branch in the key: this
checkout works on `main` only, and GitHub's per-ref status (53) is the model to add later for
projects that branch — a `refs` map on the signal, humans shown the default branch. Store the
normalised fingerprint, the key-algorithm version (2) and, from asv via #7BM4, the test's
`source_hash`. Record with each occurrence whether the tree was dirty and a digest of `git diff
HEAD`, because "same commit" does not identify the code under test in a shared tree.

**R3. States: `pending → open → resolved`, plus `dismissed` and `removed`.** Flags rather than
states for the rest: `kind: broken | flaky`, `inhibited_by`, `stale`, `session` (the claim),
`card` (the promotion), `regressed`.
- *pending* — seen failing once. Shown only to the pane whose run produced it, which is in-loop
  feedback that pane gets anyway. Deleted without trace on the next pass (14, 22). This is where an
  agent's first repair attempt happens (25).
- *open* — counted on the board, claimable.
- *resolved* — machine only, as GitHub's `fixed` is (50). No person and no agent sets it by hand; the
  way to resolve a signal is to run the check.
- *dismissed* — a person, or an agent within limits (Q7), with `reason` ∈ `flaky-known |
  environmental | wont-fix | expected` , a comment, and a **required** `until`: a date, or "until it
  fails N more times", or "until its kind changes" (20, 4, 55). Occurrences keep counting underneath.
- *removed* — the key left discovery (`ctest -N`, the unittest collection): not a fix, and not
  counted as one (52).

**R4. Open on the second consecutive failing execution of the key, not the first** (F3). When Relay
itself ran the tests (`tests_run`, #7BM4 step 3) and ten or fewer failed, re-run the failed keys
once, immediately: fail-fail is `broken` and opens at once; fail-pass is the same-tree pass-and-fail
that Datadog calls flaky and Trunk and Buildkite monitor as pass-on-retry — it stays pending with a
flaky mark, and opens as `kind: flaky` when the Nagios-style statistic over its last 21 executions
crosses a high threshold (26), leaving only below a lower one. A failure observed only from a run
Relay did not make (a session typing `ctest` in its pane, ingested from JUnit) waits for its second
failing execution.

**R5. Resolve on consecutive passing executions of the key: 2 for `broken`, 20 for `flaky`** (F4,
F5). The proposal's two is right for the deterministic case and matches TestGrid; 20 is Datadog's
number and is cheap here because the machine can simply run the test (`ctest --repeat
until-fail:20`), which no hosted product can do on demand. Not-built, not-collected, skipped and
harness-timeout are *not evaluated*: they advance nothing. No time-based resolve for anything that
can be re-run; silent when it happens (F13).

**R6. Staleness is answered by running the check, not by a timer.** A hosted system closes on
quiet days because it cannot make the test run (Mozilla 21, Datadog 30, Sentry `resolveAge`). Relay
can. An open signal whose key has not executed for 7 days is marked `stale`, sorted to the top of
what the Check button and any sweep re-run, and stays open. Time-based closing is kept only for
event sources with no observable pass (R10).

**R7. One cause, one item — three rules, in this order** (F6). (a) **Inhibition**: a build,
configure or collection failure opens `build:<target>` and the tests that needed it are recorded as
not evaluated, their counters frozen, `inhibited_by` set. (b) **Reason group**: three or more keys
failing in one run with the same normalised fingerprint become one group signal listing them; the
members stay pending. (c) **Red run**: more than half of at least ten executed keys failing makes
one `run:<runner>` signal and opens nothing else (Trunk uses 80% for infrastructure; a broken header
here reddens less than that). And the cap: a run opens at most ten individual signals; the rest roll
into the group. These three numbers are guesses to be tuned on the first month of history.

**R8. Reopen within 30 days, otherwise open a new signal that names the old one** (F10). A reopened
signal is `regressed`, keeps its count, and goes straight to `open` with no pending tier: it has
already proven itself. When an agent resolves a signal it claimed, the resolving run's commit is
recorded as `fixed_in`, which is what a later "regressed since `<sha>`" line needs and what a
branching project would compare ancestry against (6).

**R9. Promotion: by give-up, persistence or flakiness; capped; and the card never outranks the
signal** (F8, F9). Promote when (a) the agent holding the claim releases it with a reason — the
proposal's first trigger, kept; (b) the signal has failed in at least three runs spread over at
least 24 hours and nobody holds it — Mozilla's 3-in-7 with a floor, replacing "older than a day";
(c) it is confirmed `flaky`, because deflaking or deleting a test is a decision, not a repair. One
card per signal (Trunk), linked both ways (`links.signal` on the card, `card` on the signal); a
machine-owned `## Signal` section on the card rewritten in place, never comments, never touching
the person's text (39); at most five open promoted cards at a time, the overflow staying in the fold
(49). Authority: the signal resolves only by passing; when it does, the card gets one thread entry
and, if it is `executing`, moves to `needs-verification` — never to `done`, which needs a verdict. A
card linked to an open signal cannot leave `needs-verification` (the #7BM4 gate). A card closed or
dropped by a person while its signal is red leaves the signal open and eligible to promote again
after the 30-day window, which is LUCI's `file_new_bugs`.

**R10. Sources, and the test for admitting one.** A source qualifies if it has a stable key, a
machine can observe it *passing* (or, for an event source, there is a version to regress against),
and a machine can close it.

| Source | Key | Evaluation | Opens | Closes |
|---|---|---|---|---|
| tests (ctest, unittest) | runner + test id | partial runs | 2nd cons. failure (R4) | 2 / 20 cons. passes (R5) |
| `relay-board.py check` problems | `check:<code>:<path>` | complete every run | first run (no debounce needed) | absent from the next run — the Prometheus case (16) |
| build / configure failure | `build:<target>` | complete per target | first failure | next successful build; inhibits tests (R7) |
| `land.py doctor` findings | `doctor:<code>:<path>` | complete | first run | absent from the next run |
| crashes in `relay.log` | `crash:<signal>:<top in-repo frame>` | events only | first `gui_crash` | `fixed_in` build + 14 days unseen; a crash from a build ≥ `fixed_in` is a regression (5, 6) |
| CI (GitHub Actions) | `ci:<workflow>:<job>` + ref | complete per run | 2nd cons. failure on the default branch | next success on that ref |
| compiler warnings, lint | rule + path + line-content hash | complete per file | **only new since a baseline** (54) | absent from the next run |
| QA verdict failures | — | — | **not a signal** | — |

A failed QA verdict is a judgement about a card, already has a home (the card goes back with the
verdict in its body), and no machine can close it. Lint comes last: without the new-code rule it
opens hundreds of signals on day one, which is the failure Sonar's gate exists to avoid.

**R11. Claims reuse `session` unchanged, and hand-off copies the three shapes that work** (§8).
`board_signals {list | claim | release | dismiss | promote}`: `claim` writes the pane token exactly
as `board_claim` does, a second claimant gets the same `board_claimed_elsewhere`, and the pane
closing releases it. *In-loop*: a pane whose run produced pending or new signals is told at the end
of that turn — the `Stop`-hook shape (56). *Assignment*: signals first seen in a pane's runs are
offered to that pane first, and assigned once (7). *Sweep*: unclaimed open signals are worked by an
explicit board action in `board_cleanup`'s shape, at most three per sweep (Chunk's 1–3), each fix
verified by R5 before the agent reports it. `list` sorts new-to-my-card, then regressed, then broken
before flaky, then by count.

**R12. What people see.** The proposal's fold stands: a count on the bugs tab and one expandable row,
reusing #93WR's widget. Inside it, groups before members, `N new since last opened`, dismissed
hidden behind a toggle with their expiry shown. No toast and no thread entry on open or resolve; the
only things that reach a human surface unasked are a promotion (a card in `inbox`, which is Linear's
Triage and Plane's Intake by another name, 46) and a dismissal about to expire.

**R13. Retention.** A resolved or removed signal is kept 30 days for R8, then dropped from the
folded state; its events stay in the log until the log is compacted with #7BM4's history. A signal
keeps `first_seen`, `last_seen`, `count`, the last five excerpts and the run ids; the occurrences
themselves are the history file's, not duplicated (Opsgenie stops logging at 100, 24).

The record, for the card to argue with:

```yaml
key:          "unittest:tests.test_board.BoardTests.test_claim"
source:       unittest                 # ctest | unittest | check | build | doctor | crash | ci
state:        open                     # pending | open | resolved | dismissed | removed
kind:         broken                   # broken | flaky
fingerprint:  "AssertionError: % != %  @ board_tools._claim"   # normalised; groups, never keys
key_version:  1
source_hash:  "sha256:…"               # asv rule: history resets when the test is edited
first_seen:   2026-09-20T11:03:12Z
last_seen:    2026-09-20T14:40:02Z
count:        4                        # failing executions
green_streak: 0                        # passing executions since the last failure
seen_by:      [a1b2c3d4, 9f8e7d6c]     # pane tokens whose runs failed it
first_card:   R9G7                     # the card that pane held: "new to" this card (F11)
inhibited_by: null                     # e.g. "build:relay-board-tests"
group:        null                     # a group signal's key, when collapsed
session:      null                     # the claim, as on a card
card:         null                     # set by promotion
fixed_in:     null
dismissed:    null                     # {reason, comment, by, until}
previous:     null                     # key+date of the signal this one succeeds (R8)
```

## 12. Questions for the owner

Each is a product decision the research cannot make; each has a recommendation.

1. **Files in git, or a local store with only promoted cards in git?** Recommendation: the local
   store (R1). It is what every surveyed product does, it keeps machine writes out of `land.py`'s
   way, and it sits beside the history it is computed from. The cost is that signals are per machine.
2. **Is a signal a `type: signal` card at all?** You asked for a card type. Recommendation: no — a
   keyed record with no card id, drawn on the board as the folded row, which gets an id only when it
   is promoted. A card type would inherit ids, threads, ranks, `check`, the GitHub sync and the
   survey, and the proposal already has to switch each of those off.
3. **Open on the first failure or the second?** Recommendation: pending on the first, open on the
   second consecutive failing execution, with Relay's own runs re-running up to ten failed tests once
   so that usually takes seconds (R4). The shared working tree makes first-failure signals mostly
   other sessions' half-saved edits.
4. **Accept two resolve numbers — 2 passes for broken, 20 for flaky?** Recommendation: yes (R5). One
   number is either too strict for the common case or meaningless for the flaky one.
5. **What promotes a signal to a bug card?** Recommendation: the agent gave up; or three failing
   runs over at least 24 hours with nobody holding it; or confirmed flaky — and never age alone; at
   most five promoted cards open at once (R9).
6. **When the card and the signal disagree, which wins?** Recommendation: the signal. It resolves
   only by passing; its card cannot pass verification while it is open; a resolved signal moves its
   card to `needs-verification`, not `done`. This reverses the proposal's "the signal stays open
   until that card closes".
7. **May an agent dismiss a signal?** Recommendation: yes, for `environmental` and `flaky-known`
   only, with a comment and an expiry of at most 7 days; `wont-fix`, `expected` and longer expiries
   are yours. Every dismissal expires (R3).
8. **Should an open signal block a card's verification?** Recommendation: only a signal first seen
   in a run by the pane that held that card (F11); pre-existing signals never block, and are listed
   on the Check block as "open before this card".
9. **Do agents pick up unclaimed signals unasked?** Recommendation: a pane is always told about the
   signals its own runs produced and may fix them as part of its card; unclaimed signals are worked
   only by an explicit sweep button, three at a time, governed by the board's existing `autonomy`
   setting (R11).
10. **Which sources after tests, and in what order?** Recommendation: `check` problems and build
    failures with the first version (the second is needed for R7 anyway), then crashes, then CI;
    lint only with a baseline; QA verdicts never (R10).
11. **Does #AQ6X fold into #7BM4 or stay its own card?** Recommendation: its own card, scheduled
    after #7BM4 step 1 (history and the JUnit writer), because every test signal is a fold over that
    file; #7BM4's "flaky tests become cards" (its question 4) becomes R9(c) and should be struck
    there so it is not built twice.
12. **The name.** Recommendation: keep "signal". GitHub says alert, Sonar issue, Semgrep finding,
    PagerDuty alert; Datadog uses "signals" for exactly this — what its agent runs "in response to".
    "Alert" would promise a notification, which is the one thing a signal must not do.

## 13. Unverified / flagged

**Could not be confirmed on a primary page, and relied on nowhere above:** Sentry's `resolveAge`
default, and whether For Review items expire on their own; Bugsnag's snooze-until conditions (seen
only in a third-party listing of the vendor's MCP tool); Linear's default auto-close and auto-archive
periods, and whether API-created issues with no status land in Triage; JSM's automatic
alert-to-incident rules (the page would not extract); whether Dependabot closes its own superseded
PRs; SonarQube's property name for the 30-day purge (the 30 days is confirmed); Trunk's
failure-reason grouping algorithm; Datadog's behaviour when a Fixed test flakes again and whether it
can create tickets automatically; whether Buildkite's Linear action dedupes or closes on recover;
Mozilla's reopen rule and tree-closure mechanics (the sheriffing wiki pages returned 404), and its
"below 15 a week can be ignored" guidance; whether the Kubernetes `issue-creator` is still deployed;
how Cursor Bugbot's `resolution_status` becomes resolved; Develocity's and Launchable's failure
grouping (no primary page fetched); Sweep (docs would not load); Graphite and Mergify beyond index
pages.

**Read from source or configuration rather than documentation** (marked *source* where used): LUCI
Analysis' proto, clustering code and Chromium's `luci-analysis.cfg`; Treeherder's bug filer and
commenter; TestGrid's `config.proto`; the Kubernetes issue-creator; Flutter's cocoon handlers;
Kibana's failed-test reporter; Prometheus' `rules/alerting.go` and Alertmanager's OpenAPI file;
Nagios' `flapping.c` and `defaults.h`. PagerDuty's developer site would not render and was read from
its public docs repository. PagerDuty's web help and REST schema disagree on the acknowledgement and
auto-resolve defaults (off in the web app; 1800 s and 14400 s in the schema); neither is relied on.

**Spot-checked a second time against the live page on 2026-09-20:** Trunk's automatic-ticketing
lifecycle and its 80% Infrastructure Failure Protection default; LUCI's `BugManagementPolicy`
activation/deactivation comments; that `developers.openai.com/codex` now redirects to
`learn.chatgpt.com/docs`.

**Numbers in §11 that are this document's and nobody else's:** ten failed tests as the re-run
limit, three keys for a reason group, half of ten for a red run, ten signals per run, five open
promoted cards, three signals per sweep, seven days to `stale`. Each is modelled on a cited number
but chosen for a 68-entry ctest suite and a 3,350-case Python suite on one machine, and none has
been run against real history, because #7BM4's history file does not exist yet.
