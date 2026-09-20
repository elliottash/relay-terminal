// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The Test suites pane's headless model (card #7BM4, phase 2; design item (b) of
// issues/features/2026-09-20-switchboard-tooling-hub-tests-profile-and-what-else.md).
//
// A row is a **test, not a run** — Buildkite's and Datadog's shape, researched in
// docs/SWITCHBOARD-TOOLING-RESEARCH.md §3 — folded out of the four `tests_*` events the board
// worker sends (the wire contract, docs/AGENT-SESSIONS-PROTOCOL.md §31 once phase 3 lands):
//
//   tests_list     the whole inventory plus the summary line the header wears
//   tests_run      one run's progress; it is what puts a row in `Running` or `Queued`
//   tests_history  the executions of one test, newest first, when the detail asks for more
//   tests_check    one card's staleness findings (kept so a pane can show them; no rows change)
//
// Two rules the contract states and this file keeps: **unknown keys are ignored**, so the backend
// may grow fields without a GUI release, and **a missing optional key means "unknown", never
// zero** — every such field carries a `has…` flag and its text accessor answers "—". A p95 of 0.0
// and a p95 nobody measured are different facts, and a table that prints both as `0.00 s` is
// lying about the second.
//
// No widgets: tests/testsuites_test.cpp runs the whole of this without a window, exactly as
// tests/jobs_test.cpp runs relay::JobsModel. The `cards with no tests` board-level report the card
// also asks for is deliberately **not** here — it is a later phase, and `cards_without_tests` on
// the wire is ignored like any other unknown key.
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace relay::tests {

// One line of the JSONL execution store, and one cell of a row's run grid.
struct Execution {
    QString ts;        // ISO-8601 UTC
    QString runId;
    QString id;
    QString runner;
    QString result;    // "pass" | "fail" | "skip" | "error" | "timeout" | ""
    QString commit;
    QString host;      // socket.gethostname(): spark and sphinxpad are distinguishable
    double duration = 0;
    bool hasDuration = false;
    QString durationText() const;
};

// Where a row stands in the run that is in flight. Idle is also "no run in flight".
enum class RunState { Idle, Queued, Running };

// One test. Mirrors the contract's TestRecord field for field; the `has…` flags are this side's.
struct TestRow {
    QString id;        // "<runner>:<invocation>", the stable key
    QString name;
    QString runner;    // "ctest" | "unittest" | "manual"
    // What a person types to run just this one (§31.3): `ctest -R panelayout`,
    // `tests/test_board.py::CardTests::test_roundtrip`. It is **not** the id's tail — the id is
    // `ctest:panelayout` — and it is this, not the id, that goes into a card's `## Tests` section.
    QString invocation;
    QString file;      // repo-relative, "" when unknown
    int line = 0;
    bool hasLine = false;
    QStringList labels;
    QString firstSeen;
    QString lastRun;      // empty = never run
    QString lastResult;   // empty = never run
    int runs = 0, passed = 0, failed = 0, skipped = 0;
    double durationP50 = 0, durationP95 = 0, durationLast = 0;
    bool hasP50 = false, hasP95 = false, hasLast = false;
    double reliability = 0;      // 0..100
    bool hasReliability = false;
    double flakeScore = 0;       // recency-weighted pass<->fail transitions; 0 = steady
    bool slow = false, flaky = false;
    QStringList stale;           // "gone" | "never-run" | "skipped-forever" | "orphaned" | "edited"
    QString sourceHash;
    QVector<Execution> history;  // newest first, at most 20 from tests_list
    bool hasLastFailure = false;
    QString failureAt, failureCommit, failureMessage, failureExcerpt;
    QStringList cards;           // cards whose ## Tests section names this test
    RunState runState = RunState::Idle;

    bool neverRun() const { return lastRun.isEmpty() && runs == 0; }
    // fail, error and timeout are all "it did not pass"; skip is not a failure.
    bool failing() const;
    // The column texts, with "—" wherever the worker said nothing (never 0).
    QString reliabilityText() const;
    QString p50Text() const;
    QString p95Text() const;
    QString runsText() const;
    QString lastRunText(const QDateTime &now = QDateTime::currentDateTimeUtc()) const;
    QString cardsText() const;
    // "flaky", "slow", "never run", "gone" … — what the name column wears beside the name.
    QStringList badges() const;
};

// "1.4 s", "22 ms", "—" for a duration nobody measured.
QString formatDuration(double seconds, bool known = true);
// "3 m ago", "yesterday", "2026-09-14" — an ISO-8601 stamp as a reader's distance from it.
QString relativeTime(const QString &iso, const QDateTime &now = QDateTime::currentDateTimeUtc());

// One line of a `tests_check` answer: what Check said about one test of one card.
struct CheckFinding {
    QString test, verdict, message, severity;
};

// The header line, as sent or as composed here.
struct Summary {
    int total = 0, passed = 0, failed = 0, skipped = 0, neverRun = 0, slow = 0, flaky = 0;
    double duration = 0;
    bool hasDuration = false;
    QString line;    // the worker's ready-made text; empty when it sent none
};

class TestSuitesModel {
public:
    // Flake score descending is the default: TestGrid sorts its grid that way, and "what is
    // rotting" is the question a board-level pane exists to answer.
    enum class Sort { FlakeScore, P95, Name, LastFailure, LastRun };

    TestSuitesModel();

    // rows(), the summary or the run state changed.
    std::function<void()> onChanged;
    // Replaceable clock, for the tests and for a deterministic screenshot.
    std::function<QDateTime()> clock;

    // True when the event was one of this pane's (`tests_*`) and needs no other handling.
    // `reset` and `ready` empty the model and are passed on, as JobsModel does.
    bool handle(const QJsonObject &event);

    // After the filter and the sort.
    const QList<TestRow> &rows() const { return m_view; }
    // Everything the worker listed, in the order it listed it.
    const QList<TestRow> &allRows() const { return m_all; }
    const TestRow *row(const QString &id) const;
    int indexOf(const QString &id) const;        // into rows(), -1 when filtered away
    QVector<Execution> history(const QString &id) const;
    bool isEmpty() const { return m_all.isEmpty(); }

    // Free text over name, file and labels, plus the tokens
    // `is:slow is:flaky is:failed is:never is:stale`, ANDed with each other and with the text.
    void setFilter(const QString &text);
    QString filter() const { return m_filter; }

    void setSort(Sort sort, bool ascending);
    Sort sort() const { return m_sort; }
    bool ascending() const { return m_ascending; }
    // The direction a column is read in first: names up, everything else worst-first.
    static bool defaultAscending(Sort sort) { return sort == Sort::Name; }

    // The contract's `summary.line`, or one composed here when the worker sent none.
    QString summaryLine() const;
    const Summary &summary() const { return m_summary; }
    QString project() const { return m_project; }
    // A tests_list has arrived: the worker is attached and this is what it knows.
    bool attached() const { return m_attached; }

    // ----- the run in flight -------------------------------------------------------------------
    // The pane calls this as it sends a tests_run, because the event stream never repeats the ids
    // it was asked for: the rows go Queued now and the worker's `started` promotes them one by one.
    void markRequested(const QStringList &ids);
    bool running() const { return m_running; }
    QString runId() const { return m_runId; }
    int runDone() const { return m_runDone; }
    int runTotal() const { return m_runTotal; }
    // "running 12/68 · panelayout", "stopped", the worker's own message — one line for the header.
    QString runLine() const;

    // ----- the last tests_check ------------------------------------------------------------------
    // Kept whole so the card's Check strip (phase 4) and this pane read one answer. No row moves:
    // a check runs nothing.
    QString checkCard() const { return m_checkCard; }
    const QVector<CheckFinding> &checkFindings() const { return m_checkFindings; }
    const QStringList &checkActions() const { return m_checkActions; }

    void clear();

private:
    void reindex();               // filter, then sort, into m_view
    void recountSummary();
    void changed();
    TestRow *find(const QString &id);

    QList<TestRow> m_all, m_view;
    QString m_filter, m_project, m_runId, m_runMessage, m_runningTest;
    QString m_checkCard;
    QVector<CheckFinding> m_checkFindings;
    QStringList m_checkActions;
    Summary m_summary;
    QString m_sentLine;
    Sort m_sort = Sort::FlakeScore;
    bool m_ascending = false;
    bool m_attached = false, m_running = false;
    int m_runDone = 0, m_runTotal = 0;
};

}  // namespace relay::tests
