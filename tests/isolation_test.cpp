// SPDX-License-Identifier: AGPL-3.0-or-later
// The isolation probe (card #GMCF, decision 5). `systemd-run --user --scope -- true` used to be run
// and waited on for three seconds from the first pane's constructor, before any window existed; it
// is now started by main() and nobody waits more than isolation::kProbeWaitMs for it. The promise
// is a number: with a systemd-run that takes five seconds, the first consumer is answered in well
// under a second (unisolated), and the real answer is picked up when it arrives.
//
// Each scenario runs in a child process, because the probe is deliberately answered once per
// process: the test binary re-executes itself with `scenario <name> <fake dir>` and the parent
// reads the `key=value` line the child prints. The child's PATH holds only the fake systemd-run,
// so no scenario touches the real one.
#include "Isolation.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

// ----- the child: one scenario, one line of results ---------------------------------------------

static int runScenario(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qputenv("PATH", QByteArray(argv[3]));
    isolation::beginProbe();
    QElapsedTimer timer;
    timer.start();
    const bool first = isolation::available();
    const qint64 waited = timer.elapsed();
    // Ask twice more, as a pane does (worker environment, worker scope, shell scope): the extra
    // asks must not each pay their own wait.
    isolation::available();
    isolation::available();
    const qint64 askedThrice = timer.elapsed();
    // Then let the event loop run until the answer that was still outstanding lands, or 12 s — an
    // answer already in needs no wait, which is what the state rather than available() says here.
    while (isolation::detail::probeState() < 0 && timer.elapsed() < 12000) QTest::qWait(25);
    const bool late = isolation::available();
    QFile counter(QString::fromUtf8(argv[3]) + QStringLiteral("/runs"));
    const int runs = counter.open(QIODevice::ReadOnly) ? counter.readAll().trimmed().split('\n').size() : 0;
    printf("first=%d waited_ms=%lld thrice_ms=%lld late=%d runs=%d\n",
           first ? 1 : 0, static_cast<long long>(waited), static_cast<long long>(askedThrice),
           late ? 1 : 0, runs);
    fflush(stdout);
    return 0;
}

// ----- the parent -------------------------------------------------------------------------------

class IsolationTest : public QObject {
    Q_OBJECT

    // A fake systemd-run in its own directory, which records each invocation in `runs`. The child's
    // PATH is that directory and nothing else — so the real systemd-run can never be reached — which
    // is why the bodies below name what they run by absolute path.
    QString fake(const QString &name, const QString &body) {
        const QString dir = m_root.path() + '/' + name;
        QDir().mkpath(dir);
        QFile script(dir + QStringLiteral("/systemd-run"));
        script.open(QIODevice::WriteOnly);
        script.write(QStringLiteral("#!/bin/sh\necho run >> \"%1/runs\"\n%2\n").arg(dir, body).toUtf8());
        script.close();
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return dir;
    }

    QHash<QString, QString> scenario(const QString &name, const QString &path) {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("scenario"), name, path});
        if (!child.waitForFinished(30000)) { child.kill(); return {}; }
        QHash<QString, QString> fields;
        for (const QString &pair : QString::fromUtf8(child.readAllStandardOutput()).trimmed().split(' ', Qt::SkipEmptyParts)) {
            const int equals = pair.indexOf('=');
            if (equals > 0) fields.insert(pair.left(equals), pair.mid(equals + 1));
        }
        return fields;
    }

private slots:
    void initTestCase() { QVERIFY(m_root.isValid()); }

    // The finding: a systemd-run that takes five seconds. Before this change the pane's constructor
    // sat in waitForFinished(3000) with no window on screen; now the first ask is answered in
    // under a second, unisolated, and asking again does not wait again.
    void aSlowProbeDoesNotHoldTheCaller() {
        const auto result = scenario(QStringLiteral("slow"), fake(QStringLiteral("slow"), QStringLiteral("/bin/sleep 5")));
        QVERIFY2(!result.isEmpty(), "the scenario child produced no result line");
        QCOMPARE(result.value(QStringLiteral("first")), QStringLiteral("0"));   // unknown -> unisolated
        QVERIFY2(result.value(QStringLiteral("waited_ms")).toLongLong() < 1000,
                 qPrintable(QStringLiteral("first ask waited %1 ms").arg(result.value(QStringLiteral("waited_ms")))));
        // Three asks, one wait: the pane's own three questions must not cost three timeouts.
        QVERIFY2(result.value(QStringLiteral("thrice_ms")).toLongLong() < 1000,
                 qPrintable(QStringLiteral("three asks waited %1 ms").arg(result.value(QStringLiteral("thrice_ms")))));
        // ...and the late answer is picked up, so the next pane is isolated.
        QCOMPARE(result.value(QStringLiteral("late")), QStringLiteral("1"));
        QCOMPARE(result.value(QStringLiteral("runs")), QStringLiteral("1"));
    }

    // The healthy machine: the answer is there at once, and it is still probed once per process.
    void aFastProbeIsAnsweredOnce() {
        const auto result = scenario(QStringLiteral("fast"), fake(QStringLiteral("fast"), QStringLiteral("exit 0")));
        QCOMPARE(result.value(QStringLiteral("first")), QStringLiteral("1"));
        QVERIFY(result.value(QStringLiteral("waited_ms")).toLongLong() < 1000);
        QCOMPARE(result.value(QStringLiteral("runs")), QStringLiteral("1"));   // not once per ask
    }

    // A systemd-run that fails (no user manager, D-Bus not answering) is a fast, cached "no".
    void aFailingProbeIsUnavailable() {
        const auto result = scenario(QStringLiteral("fail"), fake(QStringLiteral("fail"), QStringLiteral("exit 1")));
        QCOMPARE(result.value(QStringLiteral("first")), QStringLiteral("0"));
        QCOMPARE(result.value(QStringLiteral("late")), QStringLiteral("0"));
        QVERIFY(result.value(QStringLiteral("waited_ms")).toLongLong() < 1000);
        QCOMPARE(result.value(QStringLiteral("runs")), QStringLiteral("1"));
    }

    // No systemd-run at all: the answer needs no subprocess and no wait.
    void aMissingToolNeedsNoProbe() {
        const QString empty = m_root.path() + QStringLiteral("/empty");
        QDir().mkpath(empty);
        const auto result = scenario(QStringLiteral("missing"), empty);
        QCOMPARE(result.value(QStringLiteral("first")), QStringLiteral("0"));
        QVERIFY(result.value(QStringLiteral("waited_ms")).toLongLong() < 100);
        QCOMPARE(result.value(QStringLiteral("runs")), QStringLiteral("0"));
    }

    // #ZPWT: the per-pane and slice formulas as pure functions of the RAM, checked against
    // synthetic machine sizes (the string defaults read the real /proc/meminfo). Integer division
    // is part of the contract: sizes land on whole GiB strings either way.
    void scaledDefaultsFollowTheMachine() {
        const qulonglong GiB = 1ULL << 30;
        // Agent: RAM/2 with a 2G floor.
        QCOMPARE(isolation::agentDefaultBytes(122 * GiB), 61 * GiB);
        QCOMPARE(isolation::agentDefaultBytes(4 * GiB), 2 * GiB);
        QCOMPARE(isolation::agentDefaultBytes(2 * GiB), 2 * GiB);
        // Shell: 3·RAM/4 with a 4G floor.
        QCOMPARE(isolation::shellDefaultBytes(120 * GiB), 90 * GiB);
        QCOMPARE(isolation::shellDefaultBytes(8 * GiB), 6 * GiB);
        QCOMPARE(isolation::shellDefaultBytes(4 * GiB), 4 * GiB);
        // All panes together: RAM − max(8G, RAM/10), never below half the machine (an 8G laptop
        // keeps half of it rather than zero). RAM/10 truncates at bytes: 122 GiB keeps 109.8 GiB,
        // which sized() rounds up to the "110G" the Options row shows.
        QCOMPARE(isolation::totalDefaultBytes(122 * GiB), 122 * GiB - 122 * GiB / 10);
        QCOMPARE(isolation::sized(isolation::totalDefaultBytes(122 * GiB)), QStringLiteral("110G"));
        QCOMPARE(isolation::totalDefaultBytes(120 * GiB), 108 * GiB);
        QCOMPARE(isolation::totalDefaultBytes(8 * GiB), 4 * GiB);
        // Escapees keep the pre-#ZPWT agent formula: clamp(RAM/16, 2G, 8G), tight on purpose
        // (122 GiB → 7.625 GiB, sized to "8G").
        QCOMPARE(isolation::escapeeDefaultBytes(122 * GiB), 122 * GiB / 16);
        QCOMPARE(isolation::sized(isolation::escapeeDefaultBytes(122 * GiB)), QStringLiteral("8G"));
        QCOMPARE(isolation::escapeeDefaultBytes(200 * GiB), 8 * GiB);
        QCOMPARE(isolation::escapeeDefaultBytes(16 * GiB), 2 * GiB);
    }

private:
    QTemporaryDir m_root;
};

int main(int argc, char **argv) {
    if (argc > 3 && QByteArray(argv[1]) == "scenario") return runScenario(argc, argv);
    QCoreApplication app(argc, argv);
    IsolationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "isolation_test.moc"
