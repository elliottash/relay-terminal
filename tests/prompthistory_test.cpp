// SPDX-License-Identifier: GPL-3.0-or-later
// The prompt box's history file (src/PromptHistory.h): what a line looks like on disk, what is
// worth keeping, and that two Relays appending at once do not lose each other's lines.
#include <QtTest>
#include <QFile>
#include <QFileInfo>
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

    // Two writers on one pane's file (the composer, and a paired phone writing at the door): every
    // line is there, in the order it was submitted.
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
        QVERIFY(ph::clearDirectory(QFileInfo(path()).absolutePath()));
        QVERIFY(!QFile::exists(path()));
        QVERIFY(ph::read(path()).isEmpty());
        QVERIFY(ph::clearDirectory(QFileInfo(path()).absolutePath()));   // already gone: still fine
    }

    // One file per pane (owner report, 2026-09-19: "i want pane histories for up/down"). The path
    // is keyed by the pane's layout id, and an id that is not a pane token is refused.
    void pathsAreKeyedByPaneId() {
        const QString id = QStringLiteral("0f0dd9b2-64b3-4a2e-9a11-6c5f18a63b21");
        const QString path = ph::pathFor(id);
        QVERIFY(path.endsWith(QStringLiteral("/prompt-history/") + id + QStringLiteral(".txt")));
        QVERIFY(ph::pathFor(QString()).isEmpty());
        QVERIFY(ph::pathFor(QStringLiteral("short")).isEmpty());
        QVERIFY(ph::pathFor(QStringLiteral("../windows-json")).isEmpty());
        QVERIFY(ph::pathFor(QStringLiteral("spaces in it")).isEmpty());
    }

    // The saved layout is the list of panes that can still come back; a history file whose pane
    // is not in it goes, exactly as the scrollback store is pruned.
    void pruneKeepsThePanesThatCanComeBack() {
        const QString dir = m_dir.filePath(QStringLiteral("prompt-history"));
        const QString kept = dir + QStringLiteral("/pane-aaaa0000.txt");
        const QString gone = dir + QStringLiteral("/pane-bbbb1111.txt");
        QVERIFY(ph::append(kept, QStringLiteral("git status")));
        QVERIFY(ph::append(gone, QStringLiteral("rm -rf /")));
        QCOMPARE(ph::prune(dir, QStringList{QStringLiteral("pane-aaaa0000")}), 1);
        QVERIFY(QFile::exists(kept));
        QVERIFY(!QFile::exists(gone));
        QCOMPARE(ph::prune(dir, QStringList{QStringLiteral("pane-aaaa0000")}), 0);   // idempotent
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
