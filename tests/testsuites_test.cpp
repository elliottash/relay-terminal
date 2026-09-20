// SPDX-License-Identifier: AGPL-3.0-or-later
// The Test suites pane (card #7BM4, phase 2): the headless model's fold of the four `tests_*`
// events, its filter tokens and its five sorts, and a smoke test of the widget over it — the
// shape tests/jobs_test.cpp uses for relay::JobsModel and relay::JobsPanel.
//
// Setting RELAY_TESTSUITES_SHOT=<path> makes the fixture test save a PNG of the pane instead of
// only drawing it, which is how docs/qa_evidence/2026-09-20-test-suites-pane/ was made.
#include "TestSuitesModel.h"
#include "TestSuitesPane.h"
#include "Theme.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTableView>
#include <QTest>

using relay::tests::ColCards;
using relay::tests::ColGrid;
using relay::tests::ColP95;
using relay::tests::Execution;
using relay::tests::RunState;
using relay::tests::TestRow;
using relay::tests::TestSuitesModel;
using relay::tests::TestSuitesPane;

namespace {

const QString kDash = QStringLiteral("—");

QJsonObject execution(const QString &result, const QString &ts, double duration = 0.02,
                      const QString &commit = QStringLiteral("5263b353ac91"),
                      const QString &host = QStringLiteral("spark")) {
    return QJsonObject{{"ts", ts},        {"run_id", "run-7"}, {"id", "ctest:panelayout"},
                       {"runner", "ctest"}, {"result", result}, {"duration", duration},
                       {"commit", commit},  {"host", host}};
}

// A TestRecord with every field the contract names. `extra` merges in the per-test differences.
QJsonObject record(const QString &id, const QJsonObject &extra = {}) {
    QJsonObject test{{"id", id},
                     {"name", id.section(QLatin1Char(':'), 1)},
                     {"runner", id.section(QLatin1Char(':'), 0, 0)},
                     {"invocation", QStringLiteral("ctest -R ") + id.section(QLatin1Char(':'), 1)},
                     {"file", "tests/panelayout_test.cpp"},
                     {"line", 12},
                     {"labels", QJsonArray{"panes"}},
                     {"first_seen", "2026-09-14T09:00:00Z"},
                     {"last_run", "2026-09-20T11:03:12Z"},
                     {"last_result", "pass"},
                     {"runs", 37},
                     {"pass", 35},
                     {"fail", 1},
                     {"skip", 1},
                     {"duration_p50", 0.022},
                     {"duration_p95", 0.031},
                     {"duration_last", 0.024},
                     {"reliability", 97.2},
                     {"flake_score", 0.0},
                     {"slow", false},
                     {"flaky", false},
                     {"stale", QJsonArray{}},
                     {"source_hash", "sha256:abc"},
                     {"history", QJsonArray{execution("pass", "2026-09-20T11:03:12Z")}},
                     {"cards", QJsonArray{"SDXE"}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) test.insert(it.key(), it.value());
    return test;
}

QJsonObject listEvent(const QJsonArray &tests, const QJsonObject &summary = {}) {
    QJsonObject event{{"event", "tests_list"}, {"project", "/home/elliott/repos/relay-terminal"},
                      {"tests", tests}};
    if (!summary.isEmpty()) event.insert(QStringLiteral("summary"), summary);
    return event;
}

// Thirty rows that look like this repo's suites: mostly green, two flaky, two slow, three never
// run, one consistently failing, a handful without a card.
QJsonArray fixture(int count = 30) {
    static const char *names[] = {"panelayout", "boardmodel", "boardpane", "conversations", "editor",
                                  "filepanes", "jobs", "logging", "modelcatalog", "modelpicker",
                                  "modelsettings", "panestate", "panestatus", "paneusage", "requests",
                                  "runtimedirs", "settings", "sharing", "subagents", "theme",
                                  "windowstate", "completion", "fileindex", "closedstack", "hints",
                                  "aliases", "calllines", "diffview", "remotepane", "striplayout"};
    QJsonArray tests;
    for (int i = 0; i < count; ++i) {
        const QString name = QString::fromLatin1(names[i % 30]);
        const bool pythonish = i % 7 == 3;
        const QString id = pythonish ? QStringLiteral("unittest:tests.test_%1.Cases.test_roundtrip").arg(name)
                                     : QStringLiteral("ctest:%1").arg(name);
        const bool flaky = i == 2 || i == 11;
        const bool slow = i == 5 || i == 17;
        const bool never = i >= 27;
        const bool broken = i == 8;
        QJsonArray history;
        for (int h = 0; h < (never ? 0 : 20); ++h) {
            QString result = QStringLiteral("pass");
            if (broken) result = QStringLiteral("fail");
            else if (flaky && (h % 3 == 1)) result = QStringLiteral("fail");
            else if (i == 14 && h % 9 == 0) result = QStringLiteral("skip");
            history.append(execution(result, QStringLiteral("2026-09-%1T0%2:11:04Z")
                                                 .arg(20 - h / 6, 2, 10, QLatin1Char('0'))
                                                 .arg(h % 6 + 1),
                                     slow ? 3.4 + h * 0.2 : 0.02 + h * 0.001,
                                     QStringLiteral("5263b353ac9%1").arg(h % 10),
                                     h % 4 == 0 ? QStringLiteral("sphinxpad") : QStringLiteral("spark")));
        }
        QJsonObject extra{{"name", pythonish ? QStringLiteral("tests.test_%1.Cases.test_roundtrip").arg(name)
                                             : name},
                          {"runner", pythonish ? "unittest" : "ctest"},
                          {"file", pythonish ? QStringLiteral("tests/test_%1.py").arg(name)
                                             : QStringLiteral("tests/%1_test.cpp").arg(name)},
                          {"labels", QJsonArray{pythonish ? "backend" : "gui"}},
                          {"flaky", flaky},
                          {"slow", slow},
                          {"flake_score", flaky ? 6.5 - i * 0.1 : 0.0},
                          {"runs", never ? 0 : 20},
                          {"history", history},
                          {"cards", i % 5 == 0 ? QJsonArray{"SDXE"} : QJsonArray{}}};
        if (never) {
            extra.insert(QStringLiteral("last_run"), QString());
            extra.insert(QStringLiteral("last_result"), QString());
            extra.insert(QStringLiteral("stale"), QJsonArray{"never-run"});
            extra.insert(QStringLiteral("reliability"), QJsonValue());
            extra.insert(QStringLiteral("duration_p50"), QJsonValue());
            extra.insert(QStringLiteral("duration_p95"), QJsonValue());
        } else {
            extra.insert(QStringLiteral("last_run"),
                         QStringLiteral("2026-09-20T1%1:03:12Z").arg(i % 10));
            extra.insert(QStringLiteral("last_result"), broken ? "fail" : "pass");
            extra.insert(QStringLiteral("reliability"), broken ? 0.0 : (flaky ? 66.7 : 100.0));
            extra.insert(QStringLiteral("duration_p50"), slow ? 3.4 : 0.02 + (i % 9) * 0.004);
            extra.insert(QStringLiteral("duration_p95"), slow ? 7.1 : 0.03 + (i % 9) * 0.006);
        }
        if (broken || flaky)
            extra.insert(QStringLiteral("last_failure"),
                         QJsonObject{{"at", "2026-09-20T10:41:02Z"},
                                     {"commit", "5263b353ac91"},
                                     {"message", "FAIL: the queue row kept its old height"},
                                     {"excerpt", "  QCOMPARE(row.height(), 24)\n  Actual   (row.height()): 18\n"
                                                 "  Expected (24)           : 24"}});
        tests.append(record(id, extra));
    }
    return tests;
}

}  // namespace

class TestSuitesTest : public QObject {
    Q_OBJECT
private slots:

    // ----- the model ----------------------------------------------------------------------------

    void aListFoldsEveryFieldAndIgnoresWhatItDoesNotKnow() {
        TestSuitesModel model;
        int changes = 0;
        model.onChanged = [&] { ++changes; };
        QVERIFY(model.handle(listEvent({record(QStringLiteral("ctest:panelayout"),
                                               {{"future_key", "whatever the backend grows next"}})})));
        QCOMPARE(changes, 1);
        QVERIFY(model.attached());
        QCOMPARE(model.project(), QStringLiteral("/home/elliott/repos/relay-terminal"));
        QCOMPARE(model.rows().size(), 1);
        const TestRow &row = model.rows().first();
        QCOMPARE(row.id, QStringLiteral("ctest:panelayout"));
        QCOMPARE(row.name, QStringLiteral("panelayout"));
        QCOMPARE(row.runner, QStringLiteral("ctest"));
        // The invocation is what a person types, not the id's tail (#7BM4): it is what a card's
        // `## Tests` line is made of, so the fold keeps it rather than reconstructing it.
        QCOMPARE(row.invocation, QStringLiteral("ctest -R panelayout"));
        QCOMPARE(row.file, QStringLiteral("tests/panelayout_test.cpp"));
        QVERIFY(row.hasLine);
        QCOMPARE(row.line, 12);
        QCOMPARE(row.labels, QStringList{QStringLiteral("panes")});
        QCOMPARE(row.runs, 37);
        QCOMPARE(row.passed, 35);
        QCOMPARE(row.failed, 1);
        QCOMPARE(row.skipped, 1);
        QVERIFY(row.hasP95);
        QCOMPARE(row.p50Text(), QStringLiteral("22 ms"));
        QCOMPARE(row.p95Text(), QStringLiteral("31 ms"));
        QCOMPARE(row.reliabilityText(), QStringLiteral("97%"));
        QCOMPARE(row.cardsText(), QStringLiteral("#SDXE"));
        QCOMPARE(row.history.size(), 1);
        QCOMPARE(row.history.first().host, QStringLiteral("spark"));
        QVERIFY(!row.neverRun());
        QVERIFY(!model.handle(QJsonObject{{"event", "delta"}}));   // not ours: passed on
    }

    void aMissingOptionalKeyIsADashAndNeverAZero() {
        TestSuitesModel model;
        model.handle(listEvent({QJsonObject{{"id", "ctest:brand-new"}, {"name", "brand-new"}}}));
        const TestRow &row = model.rows().first();
        // An older worker that sends no invocation: the name is a better answer than an empty
        // `## Tests` line, and it is often runnable as it stands.
        QCOMPARE(row.invocation, QStringLiteral("brand-new"));
        QCOMPARE(row.p50Text(), kDash);
        QCOMPARE(row.p95Text(), kDash);
        QCOMPARE(row.reliabilityText(), kDash);
        QCOMPARE(row.runsText(), kDash);
        QCOMPARE(row.lastRunText(), kDash);
        QCOMPARE(row.cardsText(), kDash);
        QVERIFY(row.neverRun());
        QVERIFY(row.badges().contains(QStringLiteral("never run")));
        // A measured zero is not a dash: 0.0 s is a fact.
        model.handle(listEvent({QJsonObject{{"id", "ctest:instant"}, {"duration_p50", 0.0}}}));
        QCOMPARE(model.rows().first().p50Text(), QStringLiteral("0 ms"));
    }

    void theFilterTakesTextAndTheFiveTokens() {
        TestSuitesModel model;
        model.handle(listEvent(fixture()));
        const int all = model.rows().size();
        QCOMPARE(all, 30);

        model.setFilter(QStringLiteral("is:flaky"));
        QCOMPARE(model.rows().size(), 2);
        for (const TestRow &row : model.rows()) QVERIFY(row.flaky);

        model.setFilter(QStringLiteral("is:slow"));
        QCOMPARE(model.rows().size(), 2);

        model.setFilter(QStringLiteral("is:failed"));
        QCOMPARE(model.rows().size(), 1);
        QVERIFY(model.rows().first().failing());

        model.setFilter(QStringLiteral("is:never"));
        QCOMPARE(model.rows().size(), 3);
        for (const TestRow &row : model.rows()) QVERIFY(row.neverRun());

        model.setFilter(QStringLiteral("is:stale"));
        QCOMPARE(model.rows().size(), 3);

        model.setFilter(QStringLiteral("backend"));            // a label
        QVERIFY(model.rows().size() > 0);
        for (const TestRow &row : model.rows()) QVERIFY(row.labels.contains(QStringLiteral("backend")));

        model.setFilter(QStringLiteral("_test.cpp"));          // the file
        for (const TestRow &row : model.rows()) QVERIFY(row.file.endsWith(QStringLiteral("_test.cpp")));

        model.setFilter(QStringLiteral("board"));              // the name
        QCOMPARE(model.rows().size(), 2);

        model.setFilter(QStringLiteral("is:flaky is:slow"));   // ANDed: nothing is both here
        QCOMPARE(model.rows().size(), 0);

        model.setFilter(QStringLiteral("is:nonsense"));        // a token this GUI has not learned
        QCOMPARE(model.rows().size(), all);

        model.setFilter(QString());
        QCOMPARE(model.rows().size(), all);
    }

    void everySortOrdersAndUnknownKeysGoLast() {
        TestSuitesModel model;
        model.handle(listEvent(fixture()));

        // The default: flake score, worst first.
        QCOMPARE(model.sort(), TestSuitesModel::Sort::FlakeScore);
        QVERIFY(!model.ascending());
        QVERIFY(model.rows().first().flaky);
        QVERIFY(model.rows().first().flakeScore >= model.rows().at(1).flakeScore);

        model.setSort(TestSuitesModel::Sort::Name, true);
        QStringList names;
        for (const TestRow &row : model.rows()) names << row.name.toLower();
        QStringList sorted = names;
        std::sort(sorted.begin(), sorted.end());
        QCOMPARE(names, sorted);
        model.setSort(TestSuitesModel::Sort::Name, false);
        std::reverse(sorted.begin(), sorted.end());
        names.clear();
        for (const TestRow &row : model.rows()) names << row.name.toLower();
        QCOMPARE(names, sorted);

        model.setSort(TestSuitesModel::Sort::P95, false);
        QVERIFY(model.rows().first().slow);
        QVERIFY(model.rows().first().durationP95 >= model.rows().at(1).durationP95);
        for (int i = 0; i < 3; ++i) QVERIFY(model.rows().at(model.rows().size() - 1 - i).hasP95 == false);
        model.setSort(TestSuitesModel::Sort::P95, true);
        QVERIFY(model.rows().first().hasP95);              // the smallest measured one
        QVERIFY(!model.rows().last().hasP95);              // unknown is still last, both ways

        model.setSort(TestSuitesModel::Sort::LastFailure, false);
        QVERIFY(!model.rows().first().failureAt.isEmpty());
        QVERIFY(model.rows().last().failureAt.isEmpty());

        model.setSort(TestSuitesModel::Sort::LastRun, false);
        QVERIFY(!model.rows().first().lastRun.isEmpty());
        QVERIFY(model.rows().last().lastRun.isEmpty());    // never run sorts last
        for (int i = 1; i < model.rows().size(); ++i) {
            const QString a = model.rows().at(i - 1).lastRun, b = model.rows().at(i).lastRun;
            if (!a.isEmpty() && !b.isEmpty()) QVERIFY(a >= b);
        }
    }

    void aRunMovesRowsThroughQueuedAndRunning() {
        TestSuitesModel model;
        model.handle(listEvent(fixture(4)));
        const QString first = model.rows().first().id;
        const QString second = model.rows().at(1).id;

        model.markRequested({first, second});
        QVERIFY(model.running());
        QCOMPARE(model.runTotal(), 2);
        QCOMPARE(model.row(first)->runState, RunState::Queued);
        QCOMPARE(model.row(second)->runState, RunState::Queued);

        model.handle(QJsonObject{{"event", "tests_run"}, {"run_id", "run-9"}, {"state", "started"},
                                 {"id", first}, {"done", 0}, {"total", 2}});
        QCOMPARE(model.row(first)->runState, RunState::Running);
        QCOMPARE(model.runId(), QStringLiteral("run-9"));
        QVERIFY(model.runLine().startsWith(QStringLiteral("running 0/2")));

        model.handle(QJsonObject{{"event", "tests_run"}, {"run_id", "run-9"}, {"state", "progress"},
                                 {"id", first}, {"result", "fail"}, {"duration", 1.5}, {"done", 1},
                                 {"total", 2}});
        QCOMPARE(model.row(first)->runState, RunState::Idle);
        QCOMPARE(model.row(first)->lastResult, QStringLiteral("fail"));
        QVERIFY(model.row(first)->failing());
        QCOMPARE(model.row(first)->history.first().result, QStringLiteral("fail"));
        QCOMPARE(model.runDone(), 1);

        model.handle(QJsonObject{{"event", "tests_run"}, {"run_id", "run-9"}, {"state", "finished"},
                                 {"done", 2}, {"total", 2}});
        QVERIFY(!model.running());
        for (const TestRow &row : model.allRows()) QCOMPARE(row.runState, RunState::Idle);
        QVERIFY(model.runLine().isEmpty());

        // A run that is stopped clears the queue just the same.
        model.markRequested({first});
        QCOMPARE(model.row(first)->runState, RunState::Queued);
        model.handle(QJsonObject{{"event", "tests_run"}, {"state", "stopped"}, {"message", "stopped"}});
        QCOMPARE(model.row(first)->runState, RunState::Idle);
        QCOMPARE(model.runLine(), QStringLiteral("stopped"));
    }

    void historyAndCheckArriveOnTheirOwn() {
        TestSuitesModel model;
        model.handle(listEvent(fixture(3)));
        const QString id = model.rows().first().id;
        QVERIFY(model.history(id).size() <= 20);
        QJsonArray executions;
        for (int i = 0; i < 40; ++i) executions.append(execution("pass", "2026-09-19T08:00:00Z"));
        QVERIFY(model.handle(QJsonObject{{"event", "tests_history"}, {"id", id},
                                         {"executions", executions}}));
        QCOMPARE(model.history(id).size(), 40);
        QVERIFY(model.handle(QJsonObject{{"event", "tests_history"}, {"id", "ctest:nobody"},
                                         {"executions", executions}}));   // ours, and harmless

        QVERIFY(model.handle(QJsonObject{
            {"event", "tests_check"}, {"card", "7BM4"},
            {"findings", QJsonArray{QJsonObject{{"test", id}, {"verdict", "never-run"},
                                                {"message", "no execution recorded"},
                                                {"severity", "warning"}}}},
            {"actions", QJsonArray{"Run these"}}}));
        QCOMPARE(model.checkCard(), QStringLiteral("7BM4"));
        QCOMPARE(model.checkFindings().size(), 1);
        QCOMPARE(model.checkFindings().first().verdict, QStringLiteral("never-run"));
        QCOMPARE(model.checkActions(), QStringList{QStringLiteral("Run these")});
    }

    void theSummaryIsTheWorkersLineOrOneComposedHere() {
        TestSuitesModel model;
        QCOMPARE(model.summaryLine(), QStringLiteral("No worker attached."));
        const QString sent = QStringLiteral("68 tests · 64 passed (2 slow, 1 flaky) · 3 never run · 12.4 s");
        model.handle(listEvent(fixture(), QJsonObject{{"total", 68}, {"passed", 64}, {"line", sent}}));
        QCOMPARE(model.summaryLine(), sent);
        QCOMPARE(model.summary().total, 68);

        TestSuitesModel local;
        local.handle(listEvent(fixture()));
        const QString line = local.summaryLine();
        QVERIFY2(line.startsWith(QStringLiteral("30 tests · 26 passed (2 slow, 2 flaky)")),
                 qPrintable(line));
        QVERIFY2(line.contains(QStringLiteral("1 failed")), qPrintable(line));
        QVERIFY2(line.contains(QStringLiteral("3 never run")), qPrintable(line));

        TestSuitesModel empty;
        empty.handle(listEvent({}));
        QCOMPARE(empty.summaryLine(), QStringLiteral("No tests discovered."));
        empty.clear();
        QVERIFY(!empty.attached());
    }

    // ----- the widget ---------------------------------------------------------------------------

    void thePaneListsRunsAndPaintsWhatTheModelHolds() {
        TestSuitesPane pane;
        pane.resize(1180, 760);
        QCOMPARE(pane.paneTitle(), QStringLiteral("Test suites"));
        QVERIFY(!pane.emptyStateText().isEmpty());          // nothing attached yet

        QList<QJsonObject> sent;
        pane.onSend = [&](const QJsonObject &request) { sent << request; };
        pane.requestList();
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first(), QJsonObject({{"type", "tests_list"}}));

        pane.handleEvent(listEvent(fixture()));
        QCOMPARE(pane.visibleRowCount(), 30);
        QVERIFY(pane.emptyStateText().isEmpty());
        QVERIFY2(pane.summaryText().contains(QStringLiteral("30 tests")), qPrintable(pane.summaryText()));

        // The grid delegate paints, at twenty cells and at none.
        pane.show();
        QVERIFY(!pane.grab().isNull());
        pane.selectRow(0);
        QVERIFY(!pane.selectedId().isEmpty());
        QVERIFY(pane.detailText().contains(QStringLiteral("History")));

        sent.clear();
        pane.runSelected(0);
        QVERIFY(!sent.isEmpty());
        const QJsonObject run = sent.first();
        QCOMPARE(run, QJsonObject({{"type", "tests_run"},
                                   {"ids", QJsonArray{pane.selectedId()}},
                                   {"repeat_until_fail", 0}}));
        QCOMPARE(pane.model()->row(pane.selectedId())->runState, RunState::Queued);
        sent.clear();
        pane.runSelected(10);
        QCOMPARE(sent.first().value(QStringLiteral("repeat_until_fail")).toInt(), 10);
        sent.clear();
        pane.stopRun();
        QCOMPARE(sent.first().value(QStringLiteral("type")).toString(), QStringLiteral("tests_stop"));

        // A file and a card leave through their own seams.
        QString openedPath, openedCard;
        int openedLine = -1;
        pane.onOpenFile = [&](const QString &path, int line) { openedPath = path; openedLine = line; };
        pane.onOpenCard = [&](const QString &card) { openedCard = card; };
        pane.openSelectedSource();
        QVERIFY(!openedPath.isEmpty());
        QCOMPARE(openedLine, 12);
        TestRow made;
        pane.onMakeCard = [&](const TestRow &row) { made = row; };
        pane.makeCardForSelected();
        QCOMPARE(made.id, pane.selectedId());

        // Every row hidden by the filter, and every row back again.
        pane.filterBox()->setText(QStringLiteral("is:flaky"));
        QCOMPARE(pane.visibleRowCount(), 2);
        QVERIFY(!pane.grab().isNull());
        pane.filterBox()->setText(QStringLiteral("nothing matches this"));
        QCOMPARE(pane.visibleRowCount(), 0);
        QCOMPARE(pane.emptyStateText(), QStringLiteral("Nothing matches this filter."));
        pane.filterBox()->setText(QString());
        QCOMPARE(pane.visibleRowCount(), 30);

        // Never-run rows draw an empty grid: N = 0 cells, and the pane says so in one sentence.
        TestSuitesPane fresh;
        fresh.resize(900, 500);
        QJsonArray never;
        for (const auto &value : fixture()) {
            QJsonObject test = value.toObject();
            test.insert(QStringLiteral("history"), QJsonArray{});
            test.insert(QStringLiteral("last_run"), QString());
            test.insert(QStringLiteral("runs"), 0);
            never.append(test);
        }
        fresh.handleEvent(listEvent(never));
        QCOMPARE(fresh.emptyStateText(), QStringLiteral("These tests have been discovered but never run."));
        fresh.show();
        QVERIFY(!fresh.grab().isNull());
    }

    void theKeysFilterAndRun() {
        TestSuitesPane pane;
        pane.resize(1000, 600);
        pane.handleEvent(listEvent(fixture()));
        pane.show();
        pane.selectRow(1);
        QList<QJsonObject> sent;
        pane.onSend = [&](const QJsonObject &request) { sent << request; };
        pane.table()->setFocus();
        QTest::keyClick(pane.table(), Qt::Key_Return);
        QVERIFY(!sent.isEmpty());
        QCOMPARE(sent.first().value(QStringLiteral("type")).toString(), QStringLiteral("tests_run"));
        QTest::keyClick(pane.table(), Qt::Key_Slash);
        // hasFocus() also asks whether the window is active, which an offscreen pane never is.
        QCOMPARE(pane.focusWidget(), static_cast<QWidget *>(pane.filterBox()));
        pane.filterBox()->setText(QStringLiteral("is:flaky"));
        QCOMPARE(pane.visibleRowCount(), 2);
        QTest::keyClick(pane.filterBox(), Qt::Key_Escape);
        QVERIFY(pane.filterBox()->text().isEmpty());
        QCOMPARE(pane.visibleRowCount(), 30);
    }

    void columnsCanBeHidden() {
        TestSuitesPane pane;
        pane.handleEvent(listEvent(fixture(5)));
        QVERIFY(pane.columnVisible(ColCards));
        pane.setColumnVisible(ColCards, false);
        QVERIFY(!pane.columnVisible(ColCards));
        pane.setColumnVisible(ColCards, true);
        QVERIFY(pane.columnVisible(ColCards));
    }

    // The QA screenshot: thirty realistic rows, the theme applied, saved where the card's evidence
    // directory expects it. Only when asked for, so an ordinary test run writes nothing.
    void theFixtureScreenshot() {
        const QByteArray target = qgetenv("RELAY_TESTSUITES_SHOT");
        if (target.isEmpty()) QSKIP("set RELAY_TESTSUITES_SHOT=<path.png> to save the pane");
        if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
            relay::theme::applyTheme(*app);
        TestSuitesPane pane;
        const int width = qEnvironmentVariableIntValue("RELAY_TESTSUITES_SHOT_WIDTH");
        pane.resize(width > 0 ? width : 1180, 780);
        pane.handleEvent(listEvent(fixture(), QJsonObject{
            {"line", "30 tests · 26 passed (2 slow, 2 flaky) · 1 failed · 3 never run · 41.2 s"}}));
        pane.show();
        pane.selectRow(0);
        QCoreApplication::processEvents();
        const QString path = QString::fromLocal8Bit(target);
        QVERIFY2(pane.grab().save(path), qPrintable(path));
        QVERIFY(QFileInfo(path).size() > 0);
    }
};

QTEST_MAIN(TestSuitesTest)
#include "testsuites_test.moc"
