// SPDX-License-Identifier: GPL-3.0-or-later
#include "Completion.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using relay::Completion;
using relay::completeAt;

class CompletionTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        QDir dir(m_dir.path());
        QVERIFY(dir.mkdir(QStringLiteral("src")));
        QVERIFY(dir.mkdir(QStringLiteral("scripts")));
        for (const QString &name : {QStringLiteral("README.md"), QStringLiteral("notes txt"), QStringLiteral(".hidden")}) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
    }

    void completesDirectories() {
        const Completion c = completeAt(QStringLiteral("ls sr"), 5, m_dir.path(), {});
        QCOMPARE(c.labels, QStringList{QStringLiteral("src/")});
        QCOMPARE(c.inserts, QStringList{QStringLiteral("src/")});
        QCOMPARE(c.start, 3);
        QCOMPARE(c.length, 2);
    }

    void sharedPrefixOfSeveral() {
        const Completion c = completeAt(QStringLiteral("ls s"), 4, m_dir.path(), {});
        QCOMPARE(c.labels.size(), 2);   // src/ and scripts/
        QCOMPARE(c.common, QStringLiteral("s"));
    }

    void hiddenFilesNeedADot() {
        QVERIFY(!completeAt(QStringLiteral("cat "), 4, m_dir.path(), {}).labels.contains(QStringLiteral(".hidden")));
        QCOMPARE(completeAt(QStringLiteral("cat ."), 5, m_dir.path(), {}).labels, QStringList{QStringLiteral(".hidden")});
    }

    void spacesAreEscapedAndMatched() {
        const Completion c = completeAt(QStringLiteral("cat not"), 7, m_dir.path(), {});
        QCOMPARE(c.inserts, QStringList{QStringLiteral("notes\\ txt")});
        // A half-typed escaped name still matches the real file.
        const Completion again = completeAt(QStringLiteral("cat notes\\ t"), 12, m_dir.path(), {});
        QCOMPARE(again.inserts, QStringList{QStringLiteral("notes\\ txt")});
        QCOMPARE(again.length, 8);
    }

    void insideASubdirectory() {
        const Completion c = completeAt(QStringLiteral("ls src/"), 7, m_dir.path(), {});
        QVERIFY(c.labels.isEmpty());
        const Completion parent = completeAt(QStringLiteral("ls ./RE"), 7, m_dir.path(), {});
        QCOMPARE(parent.inserts, QStringList{QStringLiteral("./README.md")});
    }

    void firstWordCompletesCommands() {
        const Completion c = completeAt(QStringLiteral("gi"), 2, m_dir.path(), {QStringLiteral("git"), QStringLiteral("gio"), QStringLiteral("ls")});
        QVERIFY(c.commands);
        QCOMPARE(c.labels, (QStringList{QStringLiteral("gio"), QStringLiteral("git")}));
        QCOMPARE(c.common, QStringLiteral("gi"));
        // After a pipe a command name is expected again.
        QVERIFY(completeAt(QStringLiteral("ls | gi"), 7, m_dir.path(), {QStringLiteral("git")}).commands);
        // A path-shaped first word is a path.
        QVERIFY(!completeAt(QStringLiteral("./sc"), 4, m_dir.path(), {QStringLiteral("git")}).commands);
    }

    void unknownCommandFallsBackToPaths() {
        const Completion c = completeAt(QStringLiteral("REA"), 3, m_dir.path(), {QStringLiteral("git")});
        QVERIFY(!c.commands);
        QCOMPARE(c.inserts, QStringList{QStringLiteral("README.md")});
    }

    void homeAndMissingDirectories() {
        QVERIFY(completeAt(QStringLiteral("ls /nonexistent-dir-xyz/f"), 24, m_dir.path(), {}).inserts.isEmpty());
        const Completion home = completeAt(QStringLiteral("ls ~/"), 5, m_dir.path(), {});
        for (const QString &insert : home.inserts) QVERIFY(insert.startsWith(QStringLiteral("~/")));
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(CompletionTest)
#include "completion_test.moc"
