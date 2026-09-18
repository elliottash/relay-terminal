// SPDX-License-Identifier: GPL-3.0-or-later
// The prompt box's history file (src/PromptHistory.h): what a line looks like on disk, what is
// worth keeping, and that two Relays appending at once do not lose each other's lines.
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "PromptHistory.h"

namespace ph = relay::prompthistory;

class PromptHistoryTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString path() const { return m_dir.filePath(QStringLiteral("prompt-history.txt")); }

private slots:
    void init() { QFile::remove(path()); }

    void multilineEntriesSurviveTheRoundTrip() {
        const QString entry = QStringLiteral("explain this:\n  path\\to\\thing\n\nand fix it");
        QCOMPARE(ph::encode(entry).contains(QLatin1Char('\n')), false);
        QCOMPARE(ph::decode(ph::encode(entry)), entry);
        QVERIFY(ph::append(path(), entry));
        QCOMPARE(ph::read(path()), QStringList{entry});
    }

    // A hand-written file of plain lines is a valid history, and a backslash that is not one of
    // our escapes is left as it reads.
    void aPlainFileReadsAsItLooks() {
        QFile file(path());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("git status\nrg '\\d+' src\n");
        file.close();
        QCOMPARE(ph::read(path()), (QStringList{QStringLiteral("git status"), QStringLiteral("rg '\\d+' src")}));
    }

    // An entry keeps the spaces the person typed at either end; only the line ending is stripped.
    void spacesInsideAnEntrySurvive() {
        const QString entry = QStringLiteral("  indented prompt  ");
        QVERIFY(ph::append(path(), entry));
        QCOMPARE(ph::read(path()), QStringList{entry});
    }

    void emptyAndOverlongEntriesAreNotStored() {
        QVERIFY(!ph::storable(QString()));
        QVERIFY(!ph::storable(QStringLiteral("   \n  ")));
        QVERIFY(!ph::storable(QString(ph::kMaxEntryChars + 1, QLatin1Char('x'))));
        QVERIFY(ph::storable(QStringLiteral("ls")));
        QVERIFY(ph::append(path(), QStringLiteral("   ")));   // a no-op, not a failure
        QVERIFY(!QFile::exists(path()));
    }

    void entriesComeBackOldestFirstAndCapped() {
        for (int i = 0; i < 12; ++i) QVERIFY(ph::append(path(), QStringLiteral("line %1").arg(i)));
        const QStringList all = ph::read(path());
        QCOMPARE(all.size(), 12);
        QCOMPARE(all.constFirst(), QStringLiteral("line 0"));
        QCOMPARE(all.constLast(), QStringLiteral("line 11"));
        const QStringList tail = ph::read(path(), 3);
        QCOMPARE(tail, (QStringList{QStringLiteral("line 9"), QStringLiteral("line 10"), QStringLiteral("line 11")}));
    }

    // Two panes (or two Relays) appending: every line is there, in the order it was submitted.
    void appendsFromEverywhereInterleave() {
        QVERIFY(ph::append(path(), QStringLiteral("pane one")));
        QVERIFY(ph::append(path(), QStringLiteral("pane two")));
        QVERIFY(ph::append(path(), QStringLiteral("pane one again")));
        QCOMPARE(ph::read(path()), (QStringList{QStringLiteral("pane one"), QStringLiteral("pane two"),
                                                QStringLiteral("pane one again")}));
    }

    // The same line twice running is stored once, whoever sent it: a prompt box dedupes against
    // its own last line and cannot see what another pane, or a paired phone, appended in between.
    void theSameLineTwiceRunningIsStoredOnce() {
        QVERIFY(ph::lastEntry(path()).isEmpty());
        QVERIFY(ph::append(path(), QStringLiteral("run the tests")));
        QCOMPARE(ph::lastEntry(path()), QStringLiteral("run the tests"));
        QVERIFY(ph::append(path(), QStringLiteral("run the tests")));
        QCOMPARE(ph::read(path()), QStringList{QStringLiteral("run the tests")});
        QVERIFY(ph::append(path(), QStringLiteral("and again")));
        QVERIFY(ph::append(path(), QStringLiteral("run the tests")));   // not consecutive: kept
        QCOMPARE(ph::read(path()).size(), 3);
    }

    // The tail is read without the rest of the file, and a multi-line entry at the end is one
    // entry, not its last line.
    void lastEntryReadsAWholeEntry() {
        const QString multiline = QStringLiteral("first line\nsecond line");
        QVERIFY(ph::append(path(), QStringLiteral("something before")));
        QVERIFY(ph::append(path(), multiline));
        QCOMPARE(ph::lastEntry(path()), multiline);
        QVERIFY(ph::append(path(), multiline));
        QCOMPARE(ph::read(path()).size(), 2);
    }

    void trimKeepsTheNewest() {
        for (int i = 0; i < 20; ++i) QVERIFY(ph::append(path(), QStringLiteral("line %1").arg(i)));
        QVERIFY(ph::trim(path(), 5));
        const QStringList kept = ph::read(path());
        QCOMPARE(kept.size(), 5);
        QCOMPARE(kept.constFirst(), QStringLiteral("line 15"));
        QCOMPARE(kept.constLast(), QStringLiteral("line 19"));
    }

    void theFileIsTheOwnersAlone() {
        QVERIFY(ph::append(path(), QStringLiteral("secret-ish prompt")));
        const QFile::Permissions permissions = QFile(path()).permissions();
        QVERIFY(permissions.testFlag(QFile::ReadOwner));
        QVERIFY(!permissions.testFlag(QFile::ReadGroup));
        QVERIFY(!permissions.testFlag(QFile::ReadOther));
    }

    void clearForgetsEverything() {
        QVERIFY(ph::append(path(), QStringLiteral("git status")));
        QVERIFY(ph::clear(path()));
        QVERIFY(!QFile::exists(path()));
        QVERIFY(ph::read(path()).isEmpty());
        QVERIFY(ph::clear(path()));   // already gone: still fine
    }

    void aMissingOrUnusablePathIsNotACrash() {
        QCOMPARE(ph::read(QString()), QStringList());
        QCOMPARE(ph::read(m_dir.filePath(QStringLiteral("nothing-here.txt"))), QStringList());
        QString error;
        QVERIFY(!ph::append(QString(), QStringLiteral("ls"), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(PromptHistoryTests)
#include "prompthistory_test.moc"
