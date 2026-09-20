// SPDX-License-Identifier: AGPL-3.0-or-later
// The Profile result pane (card #7BM4 phase 5), driven with no worker and no window: the
// `profile` events of protocol 31.9 go in through handleEvent() and everything the pane shows —
// its title, its header line, its table and its two buttons — comes out of the seams.
#include "ProfilePane.h"

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableView>
#include <QtTest/QtTest>

using relay::profile::ProfilePane;

namespace {

QJsonObject profileEvent(const QString &state, const QString &target = QStringLiteral("build")) {
    return {{QStringLiteral("event"), QStringLiteral("profile")},
            {QStringLiteral("target"), target},
            {QStringLiteral("state"), state}};
}

QJsonObject buildSummary() {
    QJsonArray rows;
    rows.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("src/main.cpp.o")},
                            {QStringLiteral("self"), 181.0}, {QStringLiteral("total"), 181.0},
                            {QStringLiteral("self_pct"), 75.4}, {QStringLiteral("total_pct"), 75.4}});
    rows.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("tests/boardmodel_test.cpp.o")},
                            {QStringLiteral("self"), 68.0}, {QStringLiteral("total"), 68.0},
                            {QStringLiteral("self_pct"), 28.3}, {QStringLiteral("total_pct"), 28.3}});
    rows.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("relay")},
                            {QStringLiteral("self"), 0.4}, {QStringLiteral("total"), 0.4},
                            {QStringLiteral("self_pct"), 0.2}, {QStringLiteral("total_pct"), 0.2}});
    return {{QStringLiteral("kind"), QStringLiteral("build")},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("steps"), 904},
            {QStringLiteral("wall"), 240.0},
            {QStringLiteral("total_rows"), 904},
            {QStringLiteral("line"), QStringLiteral("904 steps · 4m 0s wall · 12m 0s of compile time · spark")},
            {QStringLiteral("markdown"), QStringLiteral("### build\n\n| Output | Seconds | Share |\n")},
            {QStringLiteral("flame"), QStringLiteral("/tmp/evidence/build.trace.json")},
            {QStringLiteral("out"), QStringLiteral("/tmp/evidence")}};
}

}  // namespace

class ProfilePaneTest : public QObject {
    Q_OBJECT
private slots:
    void targetsMirrorTheWorker();
    void titleNamesTheTarget();
    void progressLinesAccumulate();
    void finishedFillsTheTable();
    void buildAndProfileHeadersDiffer();
    void stopSendsTheRequest();
    void errorSaysWhy();
    void anotherTargetsEventsAreIgnored();
    void buttonsWaitForSomethingToOpen();
};

// The menu and `profile_protocol.TARGETS` are one list in two files; this is the half that can be
// checked here — four entries, each with an id, a label and a line that says how long.
void ProfilePaneTest::targetsMirrorTheWorker() {
    const auto &targets = relay::profile::profileTargets();
    QCOMPARE(targets.size(), 4);
    QStringList ids;
    for (const auto &target : targets) {
        ids << target.id;
        QVERIFY(!target.label.isEmpty());
        QVERIFY(target.detail.size() > 40);
    }
    QCOMPARE(ids, QStringList({QStringLiteral("build"), QStringLiteral("build-remote"),
                               QStringLiteral("tests"), QStringLiteral("app")}));
    QVERIFY(relay::profile::profileTarget(QStringLiteral("app")) != nullptr);
    QCOMPARE(relay::profile::profileTarget(QStringLiteral("nothing")), nullptr);
}

void ProfilePaneTest::titleNamesTheTarget() {
    ProfilePane pane;
    QCOMPARE(pane.paneTitle(), QStringLiteral("Profile"));
    pane.startWaitingFor(QStringLiteral("build"));
    QCOMPARE(pane.paneTitle(), QStringLiteral("Profile: Build (this machine)"));
    QVERIFY(pane.running());
    pane.startWaitingFor(QStringLiteral("app"));
    QCOMPARE(pane.paneTitle(), QStringLiteral("Profile: The app"));
    QCOMPARE(pane.rowCount(), 0);
}

void ProfilePaneTest::progressLinesAccumulate() {
    ProfilePane pane;
    QStringList notices;
    pane.onNotice = [&notices](const QString &text) { notices << text; };
    pane.startWaitingFor(QStringLiteral("build"));
    QJsonObject started = profileEvent(QStringLiteral("started"));
    started.insert(QStringLiteral("out"), QStringLiteral("/tmp/evidence"));
    started.insert(QStringLiteral("command"), QStringLiteral("relay-profile build --out /tmp/evidence"));
    pane.handleEvent(started);
    QCOMPARE(pane.evidenceDir(), QStringLiteral("/tmp/evidence"));
    for (const QString &line : {QStringLiteral("relay-profile: configuring /tmp/x"),
                                QStringLiteral("relay-profile: building target relay")}) {
        QJsonObject progress = profileEvent(QStringLiteral("progress"));
        progress.insert(QStringLiteral("line"), line);
        pane.handleEvent(progress);
    }
    QCOMPARE(pane.lines().size(), 3);
    QCOMPARE(pane.lines().last(), QStringLiteral("relay-profile: building target relay"));
    QVERIFY(pane.running());
    // The board's notice line hears the same thing Clean up's progress puts there.
    QVERIFY(notices.last().startsWith(QStringLiteral("Profiling Build (this machine) — ")));
    QVERIFY(notices.last().endsWith(QStringLiteral("building target relay")));
}

void ProfilePaneTest::finishedFillsTheTable() {
    ProfilePane pane;
    QString openedFlame, attachedMarkdown, attachedEvidence;
    pane.onOpenFlameGraph = [&openedFlame](const QString &file) { openedFlame = file; };
    pane.onAttachToCard = [&](const QString &markdown, const QString &evidence) {
        attachedMarkdown = markdown;
        attachedEvidence = evidence;
    };
    pane.startWaitingFor(QStringLiteral("build"));
    QJsonObject finished = profileEvent(QStringLiteral("finished"));
    finished.insert(QStringLiteral("summary"), buildSummary());
    finished.insert(QStringLiteral("message"), QStringLiteral("904 steps"));
    pane.handleEvent(finished);

    QVERIFY(!pane.running());
    QCOMPARE(pane.state(), QStringLiteral("finished"));
    QCOMPARE(pane.rowCount(), 3);
    QAbstractItemModel *model = pane.tableModel();
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->columnCount(), int(relay::profile::ColCount));
    QCOMPARE(model->index(0, relay::profile::ColName).data().toString(),
             QStringLiteral("src/main.cpp.o"));
    // Sorted by self, as the worker sent it: the heaviest translation unit is the first row.
    QCOMPARE(model->index(0, relay::profile::ColSelf).data().toString(), QStringLiteral("3m 1s"));
    QCOMPARE(model->index(0, relay::profile::ColShare).data().toString(), QStringLiteral("75.4%"));
    QCOMPARE(model->index(2, relay::profile::ColSelf).data().toString(), QStringLiteral("400 ms"));
    QVERIFY(pane.statusText().contains(QStringLiteral("904 steps")));

    // The two things you do with a finished profile.
    QCOMPARE(pane.flameFile(), QStringLiteral("/tmp/evidence/build.trace.json"));
    QVERIFY(pane.markdown().startsWith(QStringLiteral("### build")));
    pane.focusView();
    for (QPushButton *button : pane.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Open flame graph")) {
            QVERIFY(button->isEnabled());
            button->click();
        }
        if (button->text().startsWith(QStringLiteral("Attach"))) {
            QVERIFY(button->isEnabled());
            button->click();
        }
    }
    QCOMPARE(openedFlame, QStringLiteral("/tmp/evidence/build.trace.json"));
    QCOMPARE(attachedEvidence, QStringLiteral("/tmp/evidence"));
    QVERIFY(attachedMarkdown.contains(QStringLiteral("| Output | Seconds | Share |")));
}

// The same four columns, named for what was profiled: a build has outputs and compile times, a
// sampled profile has functions and self/total.
void ProfilePaneTest::buildAndProfileHeadersDiffer() {
    ProfilePane pane;
    pane.startWaitingFor(QStringLiteral("build"));
    QJsonObject finished = profileEvent(QStringLiteral("finished"));
    finished.insert(QStringLiteral("summary"), buildSummary());
    pane.handleEvent(finished);
    QAbstractItemModel *model = pane.tableModel();
    QCOMPARE(model->headerData(relay::profile::ColName, Qt::Horizontal).toString(),
             QStringLiteral("Output"));
    QCOMPARE(model->headerData(relay::profile::ColSelf, Qt::Horizontal).toString(),
             QStringLiteral("Compile"));

    ProfilePane other;
    other.startWaitingFor(QStringLiteral("tests"));
    QJsonObject summary = buildSummary();
    summary.insert(QStringLiteral("kind"), QStringLiteral("profile"));
    QJsonObject done = profileEvent(QStringLiteral("finished"), QStringLiteral("tests"));
    done.insert(QStringLiteral("summary"), summary);
    other.handleEvent(done);
    QCOMPARE(other.tableModel()->headerData(relay::profile::ColName, Qt::Horizontal).toString(),
             QStringLiteral("Function"));
    QCOMPARE(other.tableModel()->headerData(relay::profile::ColSelf, Qt::Horizontal).toString(),
             QStringLiteral("Self"));
}

void ProfilePaneTest::stopSendsTheRequest() {
    ProfilePane pane;
    QList<QJsonObject> sent;
    pane.onSend = [&sent](const QJsonObject &request) { sent << request; };
    pane.stopRun();                       // nothing is running: nothing is sent
    QCOMPARE(sent.size(), 0);
    pane.startWaitingFor(QStringLiteral("app"));
    pane.stopRun();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.first().value(QStringLiteral("type")).toString(), QStringLiteral("profile_stop"));

    QJsonObject stopped = profileEvent(QStringLiteral("stopped"), QStringLiteral("app"));
    stopped.insert(QStringLiteral("message"), QStringLiteral("the profile was stopped"));
    pane.handleEvent(stopped);
    QVERIFY(!pane.running());
    QCOMPARE(pane.statusText(), QStringLiteral("Profile of The app stopped"));
    pane.stopRun();
    QCOMPARE(sent.size(), 1);             // and not again once it has ended
}

void ProfilePaneTest::errorSaysWhy() {
    ProfilePane pane;
    pane.startWaitingFor(QStringLiteral("build"));
    QJsonObject failed = profileEvent(QStringLiteral("error"));
    failed.insert(QStringLiteral("message"),
                  QStringLiteral("relay-profile: ninja is not installed"));
    pane.handleEvent(failed);
    QVERIFY(!pane.running());
    QCOMPARE(pane.rowCount(), 0);
    QVERIFY(pane.statusText().contains(QStringLiteral("ninja is not installed")));
    QVERIFY(pane.lines().contains(QStringLiteral("relay-profile: ninja is not installed")));
}

void ProfilePaneTest::anotherTargetsEventsAreIgnored() {
    ProfilePane pane;
    pane.startWaitingFor(QStringLiteral("build"));
    QJsonObject other = profileEvent(QStringLiteral("finished"), QStringLiteral("tests"));
    other.insert(QStringLiteral("summary"), buildSummary());
    pane.handleEvent(other);
    QCOMPARE(pane.rowCount(), 0);
    QCOMPARE(pane.state(), QStringLiteral("started"));
    // And anything that is not a `profile` event at all.
    pane.handleEvent({{QStringLiteral("event"), QStringLiteral("tests_run")},
                      {QStringLiteral("state"), QStringLiteral("finished")}});
    QCOMPARE(pane.state(), QStringLiteral("started"));
}

void ProfilePaneTest::buttonsWaitForSomethingToOpen() {
    ProfilePane pane;
    for (QPushButton *button : pane.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Open flame graph")
            || button->text().startsWith(QStringLiteral("Attach")))
            QVERIFY2(!button->isEnabled(), qPrintable(button->text()));
    }
}

QTEST_MAIN(ProfilePaneTest)
#include "profilepane_test.moc"
