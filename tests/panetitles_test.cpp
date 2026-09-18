// SPDX-License-Identifier: GPL-3.0-or-later
// Pane titles and tab labels (issue JRWQ): tidying a model-written title, the offline "same work"
// judgement and how a tab label is joined and shortened. The worker side is tested in
// tests/test_titles.py; the rules here are what the GUI applies before, or without, any model.
#include "PaneTitles.h"

#include <QTest>

using namespace relay::titles;

class PaneTitlesTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cleansAModelReply() {
        QCOMPARE(clean(QStringLiteral("  Fixing   pane drag\n")), QStringLiteral("Fixing pane drag"));
        QCOMPARE(clean(QStringLiteral("\"Fixing pane drag.\"")), QStringLiteral("Fixing pane drag"));
        QCOMPARE(clean(QStringLiteral("Title: Release notes")), QStringLiteral("Release notes"));
        QCOMPARE(clean(QStringLiteral("“Release notes”")), QStringLiteral("Release notes"));
        // At most six words in the header.
        QCOMPARE(clean(QStringLiteral("one two three four five six seven eight")),
                 QStringLiteral("one two three four five six"));
        // A hand-set name keeps its words and only the length cap applies.
        QCOMPARE(clean(QStringLiteral("one two three four five six seven"), kMaxUserTitle, 0),
                 QStringLiteral("one two three four five six seven"));
        QVERIFY(clean(QString()).isEmpty());
    }

    void clipsOnAWordBoundary() {
        const QString text = QStringLiteral("Fixing pane drag and Ctrl+H sizing");
        const QString cut = clip(text, 20);
        QVERIFY(cut.size() <= 20);
        QVERIFY(cut.endsWith(QChar(0x2026)));
        QVERIFY(text.startsWith(cut.left(cut.size() - 1)));
        QCOMPARE(clip(text, 200), text);
    }

    void dropsEmptyAndRepeatedTitles() {
        const QStringList items = distinct({QStringLiteral("Release notes"), QString(),
                                            QStringLiteral("release NOTES"), QStringLiteral("  Fixing pane drag ")});
        QCOMPARE(items, QStringList({QStringLiteral("Release notes"), QStringLiteral("Fixing pane drag")}));
    }

    void sameWorkWithoutAModel() {
        // One pane, or two panes naming the same work, are related.
        QVERIFY(relatedText({QStringLiteral("Fixing pane drag")}));
        QVERIFY(relatedText({QStringLiteral("Fixing pane drag"), QStringLiteral("Pane drag drop zones")}));
        // Unrelated work is not, and shared filler words ("fixing", "the") do not make it related.
        QVERIFY(!relatedText({QStringLiteral("Fixing pane drag"), QStringLiteral("Release notes")}));
        QVERIFY(!relatedText({QStringLiteral("Fixing the tests"), QStringLiteral("Fixing the docs")}));
        // Three panes count as related only when all of them share the work.
        QVERIFY(relatedText({QStringLiteral("Pane drag"), QStringLiteral("Drag drop zones"), QStringLiteral("Drag cursor")}));
        QVERIFY(!relatedText({QStringLiteral("Pane drag"), QStringLiteral("Drag zones"), QStringLiteral("Release notes")}));
    }

    void joinsRelatedAndUnrelatedPanes() {
        const QStringList two{QStringLiteral("Fixing pane drag"), QStringLiteral("Release notes")};
        QCOMPARE(join(two, false), QStringLiteral("Fixing pane drag; Release notes"));
        // Related panes give one phrase: the model's when it sent one, else the first pane's title.
        QCOMPARE(join(two, true), QStringLiteral("Fixing pane drag"));
        QCOMPARE(join(two, true, QStringLiteral("\"Pane drag work.\"")), QStringLiteral("Pane drag work"));
        QVERIFY(join({}, false).isEmpty());
        QCOMPARE(join({QString(), QStringLiteral("Release notes")}, false), QStringLiteral("Release notes"));
    }

    void shortensALongLabel() {
        const QString label = join({QStringLiteral("Fixing pane drag and Ctrl+H sizing"),
                                    QStringLiteral("Release notes for the 0.1 preview")}, false, QString(), 30);
        QVERIFY(label.size() <= 30);
        QVERIFY(label.endsWith(QChar(0x2026)));
    }
};

QTEST_MAIN(PaneTitlesTests)
#include "panetitles_test.moc"
