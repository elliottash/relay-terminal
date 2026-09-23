// SPDX-License-Identifier: AGPL-3.0-or-later
// The three-way merge, revisions and in-place edits an open buffer reconciles an external change
// with (card #F8R7). No window: src/TextMerge is QtCore only.
#include "TextMerge.h"

#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::merge;

namespace {
QString apply(QString text, const QVector<TextEdit> &edits) {
    for (int k = int(edits.size()) - 1; k >= 0; --k) text.replace(edits[k].position, edits[k].removed, edits[k].inserted);
    return text;
}
}  // namespace

class TextMergeTests : public QObject {
    Q_OBJECT
private slots:
    void cleanNonOverlappingEditsMerge() {
        const QString base = QStringLiteral("one\ntwo\nthree\nfour\nfive\n");
        const QString ours = QStringLiteral("ONE\ntwo\nthree\nfour\nfive\n");
        const QString theirs = QStringLiteral("one\ntwo\nthree\nfour\nFIVE\nsix\n");
        const MergeResult r = merge3(base, ours, theirs);
        QVERIFY(r.clean());
        QCOMPARE(r.text, QStringLiteral("ONE\ntwo\nthree\nfour\nFIVE\nsix\n"));
    }

    void adjacentDistinctLinesMerge() {
        const QString base = QStringLiteral("a\nb\nc\n");
        const MergeResult r = merge3(base, QStringLiteral("A\nb\nc\n"), QStringLiteral("a\nB\nc\n"));
        QVERIFY(r.clean());
        QCOMPARE(r.text, QStringLiteral("A\nB\nc\n"));
    }

    void sameChangeOnBothSidesIsTakenOnce() {
        const QString base = QStringLiteral("a\nb\nc\n");
        const QString both = QStringLiteral("a\nBEE\nc\n");
        const MergeResult r = merge3(base, both, both);
        QVERIFY(r.clean());
        QCOMPARE(r.text, both);
    }

    void overlappingEditsConflictWithAllThreeVersions() {
        const QString base = QStringLiteral("head\nmiddle\ntail\n");
        const QString ours = QStringLiteral("head\nmine\ntail\n");
        const QString theirs = QStringLiteral("head\ntheirs\ntail\n");
        const MergeResult r = merge3(base, ours, theirs);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].base, QStringLiteral("middle\n"));
        QCOMPARE(r.conflicts[0].ours, QStringLiteral("mine\n"));
        QCOMPARE(r.conflicts[0].theirs, QStringLiteral("theirs\n"));
        QCOMPARE(r.conflicts[0].line, 1);
        QCOMPARE(r.text, QStringLiteral("head\n<<<<<<< mine\nmine\n||||||| loaded\nmiddle\n=======\ntheirs\n>>>>>>> disk\ntail\n"));
    }

    void conflictAndCleanHunksTogether() {
        const QString base = QStringLiteral("1\n2\n3\n4\n5\n6\n7\n");
        const QString ours = QStringLiteral("1\n2x\n3\n4\n5\n6\n7o\n");
        const QString theirs = QStringLiteral("1t\n2\n3\n4\n5\n6\n7t\n");
        const MergeResult r = merge3(base, ours, theirs);
        QCOMPARE(r.conflicts.size(), 1);
        QVERIFY(r.text.startsWith(QStringLiteral("1t\n2x\n3\n4\n5\n6\n<<<<<<< mine\n7o\n")));
        QCOMPARE(r.conflicts[0].line, 6);
    }

    void insertionsAtTheSamePointConflict() {
        const QString base = QStringLiteral("a\nb\n");
        const MergeResult r = merge3(base, QStringLiteral("a\nmine\nb\n"), QStringLiteral("a\ntheirs\nb\n"));
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.conflicts[0].base, QString());
        QCOMPARE(r.conflicts[0].ours, QStringLiteral("mine\n"));
        QCOMPARE(r.conflicts[0].theirs, QStringLiteral("theirs\n"));
        // The same insertion on both sides is not a conflict.
        const MergeResult same = merge3(base, QStringLiteral("a\nnew\nb\n"), QStringLiteral("a\nnew\nb\n"));
        QVERIFY(same.clean());
        QCOMPARE(same.text, QStringLiteral("a\nnew\nb\n"));
    }

    void insertionTouchingAnotherChangeConflicts() {
        const QString base = QStringLiteral("a\nb\nc\n");
        // Ours inserts before b; theirs rewrites b. Which comes first is not knowable.
        const MergeResult r = merge3(base, QStringLiteral("a\nnew\nb\nc\n"), QStringLiteral("a\nB\nc\n"));
        QVERIFY(!r.clean());
        // An insertion one line away from the other change merges.
        const MergeResult apart = merge3(base, QStringLiteral("new\na\nb\nc\n"), QStringLiteral("a\nb\nC\n"));
        QVERIFY(apart.clean());
        QCOMPARE(apart.text, QStringLiteral("new\na\nb\nC\n"));
    }

    void endOfFileNewline() {
        // One side adds the final newline, the other edits the top: both land.
        const QString base = QStringLiteral("top\nlast");
        const MergeResult r = merge3(base, QStringLiteral("TOP\nlast"), QStringLiteral("top\nlast\n"));
        QVERIFY(r.clean());
        QCOMPARE(r.text, QStringLiteral("TOP\nlast\n"));
        // Both append after a last line with no newline, differently: a conflict whose markers
        // still sit on lines of their own.
        const MergeResult both = merge3(base, QStringLiteral("top\nlast\nmine"), QStringLiteral("top\nlast\ntheirs"));
        QVERIFY(!both.clean());
        QCOMPARE(both.text, QStringLiteral("top\n<<<<<<< mine\nlast\nmine\n||||||| loaded\nlast\n=======\nlast\ntheirs\n>>>>>>> disk\n"));
        // Removing the final newline on one side merges with an edit elsewhere.
        const MergeResult strip = merge3(QStringLiteral("a\nb\nc\n"), QStringLiteral("A\nb\nc\n"), QStringLiteral("a\nb\nc"));
        QVERIFY(strip.clean());
        QCOMPARE(strip.text, QStringLiteral("A\nb\nc"));
    }

    void emptySides() {
        QCOMPARE(merge3(QString(), QString(), QStringLiteral("x\n")).text, QStringLiteral("x\n"));
        QVERIFY(!merge3(QString(), QStringLiteral("y\n"), QStringLiteral("x\n")).clean());
        const MergeResult cleared = merge3(QStringLiteral("a\n"), QString(), QStringLiteral("a\n"));
        QVERIFY(cleared.clean());
        QCOMPARE(cleared.text, QString());
    }

    void reconcileOutcomes() {
        const QString base = QStringLiteral("a\nb\nc\n");
        QCOMPARE(reconcile(base, QStringLiteral("x\n"), base).outcome, Outcome::Unchanged);
        const Reconciliation take = reconcile(base, base, QStringLiteral("a\nB\nc\n"));
        QCOMPARE(take.outcome, Outcome::TakeDisk);
        QCOMPARE(take.text, QStringLiteral("a\nB\nc\n"));
        QCOMPARE(reconcile(base, QStringLiteral("a\nB\nc\n"), QStringLiteral("a\nB\nc\n")).outcome, Outcome::Converged);
        const Reconciliation merged = reconcile(base, QStringLiteral("A\nb\nc\n"), QStringLiteral("a\nb\nC\n"));
        QCOMPARE(merged.outcome, Outcome::Merged);
        QCOMPARE(merged.text, QStringLiteral("A\nb\nC\n"));
        QCOMPARE(reconcile(base, QStringLiteral("a\nX\nc\n"), QStringLiteral("a\nY\nc\n")).outcome, Outcome::Conflict);
    }

    void editsBetweenTouchOnlyWhatDiffers() {
        const QString from = QStringLiteral("alpha\nbeta gamma\ndelta\n");
        const QString to = QStringLiteral("alpha\nbeta GAMMA\ndelta\nepsilon\n");
        const QVector<TextEdit> edits = editsBetween(from, to);
        QCOMPARE(apply(from, edits), to);
        QCOMPARE(edits.size(), 2);
        QCOMPARE(edits[0].position, 11);   // just the word, not the line
        QCOMPARE(edits[0].removed, 5);
        QCOMPARE(edits[0].inserted, QStringLiteral("GAMMA"));
        QVERIFY(editsBetween(from, from).isEmpty());
    }

    // Many random line edits on both sides: every clean merge contains both sides' changes, and
    // editsBetween always reproduces its target.
    void randomisedRoundTrips() {
        QRandomGenerator rng(0xF8A7);
        for (int round = 0; round < 300; ++round) {
            QStringList base;
            const int lines = 1 + int(rng.bounded(30));
            for (int k = 0; k < lines; ++k) base << QStringLiteral("line %1\n").arg(k);
            auto mutate = [&](QStringList lines, const QString &tag, int lo, int hi) {
                for (int k = lo; k < hi && k < lines.size(); ++k)
                    if (rng.bounded(4) == 0) lines[k] = QStringLiteral("%1 %2\n").arg(tag).arg(k);
                return lines;
            };
            const int split = lines / 2;
            // Ours edits only the top half and theirs only the bottom, a line apart, so they never overlap.
            const QStringList ours = mutate(base, QStringLiteral("ours"), 0, std::max(0, split - 1));
            const QStringList theirs = mutate(base, QStringLiteral("theirs"), split + 1, lines);
            const MergeResult r = merge3(base.join(QString()), ours.join(QString()), theirs.join(QString()));
            QVERIFY(r.clean());
            QStringList expected = base;
            for (int k = 0; k < lines; ++k) {
                if (ours[k] != base[k]) expected[k] = ours[k];
                if (theirs[k] != base[k]) expected[k] = theirs[k];
            }
            QCOMPARE(r.text, expected.join(QString()));
            QCOMPARE(apply(ours.join(QString()), editsBetween(ours.join(QString()), r.text)), r.text);
        }
    }

    void editorTextMatchesThePlainTextEdit() {
        QCOMPARE(editorText(QStringLiteral("a\r\nb\rc\n")), QStringLiteral("a\nb\nc\n"));
        QCOMPARE(editorText(QString::fromUtf8("x y z ")), QStringLiteral("x y\nz\n"));
    }

    void revisionsDecideByContent() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("f.txt"));
        QCOMPARE(readLocalFile(path, 1024).revision.exists, false);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("hello\r\nworld\n");
        }
        const Snapshot one = readLocalFile(path, 1024);
        QVERIFY(one.revision.exists);
        QCOMPARE(one.text, QStringLiteral("hello\nworld\n"));
        QCOMPARE(one.revision.size, qint64(13));
        QVERIFY(!one.truncated);
        QVERIFY(one.revision.sameContent(readLocalFile(path, 1024).revision));
        QVERIFY(one.revision.sameContent(revisionOf(one.bytes)));
        const Snapshot capped = readLocalFile(path, 4);
        QVERIFY(capped.truncated);
        QCOMPARE(capped.bytes, QByteArray("hell"));
        QVERIFY(!Revision().sameContent(one.revision));
        QVERIFY(Revision().sameContent(Revision()));
    }
};

QTEST_GUILESS_MAIN(TextMergeTests)
#include "textmerge_test.moc"
