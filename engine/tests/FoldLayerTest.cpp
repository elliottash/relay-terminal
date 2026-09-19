// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::FoldLayer: the fold layer's maths without a GUI — wrapping, the
// visual-row <-> real-row mapping, anchoring, trimming and ordering.
#include "view/FoldLayer.h"

#include <QtTest>

using namespace relay;

namespace {

FoldLine plain(const QString &text)
{
    FoldSpan s;
    s.text = text;
    FoldLine l;
    l.spans << s;
    return l;
}

QVector<FoldLine> lines(const QStringList &texts)
{
    QVector<FoldLine> out;
    for (const QString &t : texts)
        out << plain(t);
    return out;
}

} // namespace

class FoldLayerTest : public QObject {
    Q_OBJECT

private slots:
    void prefixDecidesWhatIsAnAnchor()
    {
        FoldLayer f;
        QVERIFY(!f.isAnchorUri(QStringLiteral("relay://call/1/2/3")));
        f.setPrefix(QStringLiteral("relay://call/"));
        QVERIFY(f.isAnchorUri(QStringLiteral("relay://call/1/2/3")));
        QVERIFY(!f.isAnchorUri(QStringLiteral("https://example.org")));
        QVERIFY(!f.isAnchorUri(QString()));
    }

    void contentExpandsAndWrapsToTheWidth()
    {
        FoldLayer f;
        f.setGeometry(20, 3); // 17 usable columns
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("0123456789abcdefghij"), QStringLiteral("short")}));
        QVERIFY(f.expanded(QStringLiteral("u")));
        QVERIFY(f.hasContent(QStringLiteral("u")));
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("u"));
        QVERIFY(fold);
        // 20 cells over 17 columns is two rows, plus the short line.
        QCOMPARE(fold->height(), 3);
        QCOMPARE(f.rowText(0, 0), QStringLiteral("0123456789abcdefg"));
        QCOMPARE(f.rowText(0, 1), QStringLiteral("hij"));
        QCOMPARE(f.rowText(0, 2), QStringLiteral("short"));
        // The logical line comes back whole, without the indent.
        QCOMPARE(f.lineText(0, 0), QStringLiteral("0123456789abcdefghij"));
    }

    void reflowRewrapsTheBlock()
    {
        FoldLayer f;
        f.setGeometry(20, 3);
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("0123456789abcdefghij")}));
        f.setAnchor(QStringLiteral("u"), 5, 5);
        QCOMPARE(f.fold(QStringLiteral("u"))->height(), 2);
        QCOMPARE(f.visualTotal(100), 102);
        QVERIFY(f.setGeometry(40, 3));
        QCOMPARE(f.fold(QStringLiteral("u"))->height(), 1);
        QCOMPARE(f.visualTotal(100), 101);
        QVERIFY(!f.setGeometry(40, 3)); // no change, no rewrap
    }

    void wideCharactersTakeTwoColumns()
    {
        QCOMPARE(FoldLayer::clusterWidth(QStringLiteral("a")), 1);
        QCOMPARE(FoldLayer::clusterWidth(QStringLiteral("你")), 2);
        QCOMPARE(FoldLayer::clusterWidth(QString::fromUtf8("\xF0\x9F\x8E\x89")), 2);
        FoldLayer f;
        f.setGeometry(8, 3); // 5 usable columns
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("你好你好")}));
        // A third wide cluster would need a sixth column: two rows of two.
        QCOMPARE(f.fold(QStringLiteral("u"))->height(), 2);
        QCOMPARE(f.rowText(0, 0), QStringLiteral("你好"));
        QCOMPARE(f.rowText(0, 1), QStringLiteral("你好"));
    }

    // relay::wrapFoldLines() is what a host uses to cap its content in the rows the view will
    // actually paint (#K48R: the reasoning fold's six and eighteen). It has to agree with the
    // layer exactly, so: wrap first, lay the result out, and every line must be one row — and the
    // rows must read the same as laying the original out did.
    void preWrappedLinesLayOutOneRowEach()
    {
        const QVector<FoldLine> source = lines({QStringLiteral("0123456789abcdefghij"),
                                                QStringLiteral("short"),
                                                QString(),
                                                QStringLiteral("你好你好你好你好你好"),
                                                QStringLiteral("a\tb")});
        for (int columns : {8, 20, 41, 80}) {
            const int usable = columns - kFoldIndent;
            FoldLayer direct;
            direct.setGeometry(columns, kFoldIndent);
            direct.setContent(QStringLiteral("u"), source);
            const QVector<FoldLine> wrapped = wrapFoldLines(source, usable);
            FoldLayer layer;
            layer.setGeometry(columns, kFoldIndent);
            layer.setContent(QStringLiteral("u"), wrapped);
            const FoldLayer::Fold *fold = layer.fold(QStringLiteral("u"));
            QVERIFY(fold);
            // One row per pre-wrapped line: nothing wrapped a second time.
            QCOMPARE(fold->height(), int(wrapped.size()));
            QCOMPARE(fold->height(), direct.fold(QStringLiteral("u"))->height());
            for (int row = 0; row < fold->height(); ++row)
                QCOMPARE(layer.rowText(0, row), direct.rowText(0, row));
        }
        // Zero or less: the lines come back untouched, for a host that has no width yet.
        QCOMPARE(wrapFoldLines(source, 0).size(), source.size());
    }

    void combiningMarksStayInOneCell()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("éx")}));
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("u"));
        QCOMPARE(int(fold->cells[0].size()), 2);
        QCOMPARE(fold->cells[0][0].text, QStringLiteral("é"));
    }

    void anEmptyLineIsStillOneRow()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("u"), lines({QString(), QStringLiteral("x"), QString()}));
        QCOMPARE(f.fold(QStringLiteral("u"))->height(), 3);
        QCOMPARE(f.rowText(0, 0), QString());
    }

    void unresolvedFoldsAddNoRows()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(!f.active());
        QCOMPARE(f.visualTotal(50), 50);
        f.setAnchor(QStringLiteral("u"), 10, 10);
        QVERIFY(f.active());
        QCOMPARE(f.visualTotal(50), 52);
        f.clearAnchors();
        QVERIFY(!f.active());
        QCOMPARE(f.visualTotal(50), 50);
    }

    void visualRowsInterleaveWithRealRows()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("a"), QStringLiteral("b")}));
        f.setAnchor(QStringLiteral("u"), 4, 4);

        QCOMPARE(f.visualOfReal(0), 0);
        QCOMPARE(f.visualOfReal(4), 4);  // the anchor row itself does not move
        QCOMPARE(f.visualOfReal(5), 7);  // the two fold rows sit between 4 and 5
        QCOMPARE(f.foldVisualStart(0), 5);

        QCOMPARE(f.at(3).fold, false);
        QCOMPARE(f.at(3).realRow, 3);
        QCOMPARE(f.at(4).fold, false);
        QCOMPARE(f.at(4).realRow, 4);
        QVERIFY(f.at(5).fold);
        QCOMPARE(f.at(5).foldRow, 0);
        QVERIFY(f.at(6).fold);
        QCOMPARE(f.at(6).foldRow, 1);
        QCOMPARE(f.at(7).fold, false);
        QCOMPARE(f.at(7).realRow, 5);
        // Round trip for every real row.
        for (int r = 0; r < 20; ++r)
            QCOMPARE(f.at(f.visualOfReal(r)).realRow, r);
    }

    void twoFoldsStack()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("a"), lines({QStringLiteral("1")}));
        f.setContent(QStringLiteral("b"), lines({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
        f.setAnchor(QStringLiteral("a"), 2, 2);
        f.setAnchor(QStringLiteral("b"), 6, 6);
        QCOMPARE(f.visualTotal(10), 14);
        QCOMPARE(f.visualOfReal(2), 2);
        QCOMPARE(f.visualOfReal(3), 4);
        QCOMPARE(f.visualOfReal(6), 7);
        QCOMPARE(f.visualOfReal(7), 11);
        QCOMPARE(f.expandedUris(), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        for (int r = 0; r < 10; ++r)
            QCOMPARE(f.at(f.visualOfReal(r)).realRow, r);
        // Collapsing the first one closes its rows and moves the second up.
        f.setExpanded(QStringLiteral("a"), false);
        QCOMPARE(f.visualTotal(10), 13);
        QCOMPARE(f.visualOfReal(7), 10);
        QCOMPARE(f.expandedUris(), QStringList({QStringLiteral("b")}));
    }

    void anAnchorRunEndsWhereTheFoldHangs()
    {
        // An anchor line long enough to soft-wrap covers rows 4 and 5; the fold
        // hangs under the last of them.
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("x")}));
        f.setAnchor(QStringLiteral("u"), 4, 5);
        QCOMPARE(f.visualOfReal(5), 5);
        QCOMPARE(f.visualOfReal(6), 7);
        QVERIFY(f.at(6).fold);
    }

    void trimmingDropsAFoldWhoseAnchorIsGone()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("a"), lines({QStringLiteral("1")}));
        f.setContent(QStringLiteral("b"), lines({QStringLiteral("1")}));
        f.setAnchor(QStringLiteral("a"), 1, 1);
        f.setAnchor(QStringLiteral("b"), 9, 9);
        QCOMPARE(f.visualTotal(20), 22);
        // The scrollback trimmed away the first anchor and shifted the second.
        f.retainAnchored({QStringLiteral("b")});
        QVERIFY(!f.known(QStringLiteral("a")));
        QCOMPARE(f.visualTotal(20), 21);
        f.setAnchor(QStringLiteral("b"), 4, 4);
        QCOMPARE(f.visualOfReal(5), 6);
    }

    void removeAndClear()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("a"), lines({QStringLiteral("1")}));
        f.setContent(QStringLiteral("b"), lines({QStringLiteral("1")}));
        f.setAnchor(QStringLiteral("a"), 1, 1);
        f.setAnchor(QStringLiteral("b"), 3, 3);
        f.remove(QStringLiteral("a"));
        QVERIFY(!f.known(QStringLiteral("a")));
        QVERIFY(f.known(QStringLiteral("b")));
        QCOMPARE(f.visualTotal(10), 11);
        QCOMPARE(f.indexOf(QStringLiteral("b")), 0);
        f.clear();
        QVERIFY(!f.active());
        QCOMPARE(f.visualTotal(10), 10);
        QVERIFY(f.expandedUris().isEmpty());
    }

    void expandingWithoutContentAddsNothingYet()
    {
        // The host has not answered onFoldRequested yet: the fold is expanded
        // but empty, so it takes no rows and the view is unchanged.
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setExpanded(QStringLiteral("u"), true);
        f.setAnchor(QStringLiteral("u"), 3, 3);
        QVERIFY(f.expanded(QStringLiteral("u")));
        QVERIFY(!f.active());
        QCOMPARE(f.visualTotal(10), 10);
        QCOMPARE(f.expandedUris(), QStringList({QStringLiteral("u")}));
        f.setContent(QStringLiteral("u"), lines({QStringLiteral("detail")}));
        QVERIFY(f.active());
        QCOMPARE(f.visualTotal(10), 11);
    }

    void aLongFoldScrollsRowByRow()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        QStringList body;
        for (int i = 0; i < 300; ++i)
            body << QStringLiteral("line %1").arg(i);
        f.setContent(QStringLiteral("u"), lines(body));
        f.setAnchor(QStringLiteral("u"), 10, 10);
        QCOMPARE(f.fold(QStringLiteral("u"))->height(), 300);
        QCOMPARE(f.visualTotal(50), 350);
        QCOMPARE(f.rowText(0, 299), QStringLiteral("line 299"));
        for (int v = 11; v <= 310; ++v) {
            const FoldLayer::VisualRow r = f.at(v);
            QVERIFY(r.fold);
            QCOMPARE(r.foldRow, v - 11);
        }
        QCOMPARE(f.at(311).realRow, 11);
    }

    void spansKeepTheirColours()
    {
        FoldSpan add;
        add.text = QStringLiteral("+ added");
        add.fg = QColor(0, 200, 0);
        add.bold = true;
        FoldSpan tail;
        tail.text = QStringLiteral(" ok");
        tail.dim = true;
        FoldLine line;
        line.spans << add << tail;
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setContent(QStringLiteral("u"), QVector<FoldLine>{line});
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("u"));
        QCOMPARE(int(fold->cells[0].size()), 10);
        QCOMPARE(fold->cells[0][0].fg, QColor(0, 200, 0));
        QVERIFY(fold->cells[0][0].bold);
        QVERIFY(!fold->cells[0][0].dim);
        QVERIFY(fold->cells[0][9].dim);
        QVERIFY(!fold->cells[0][9].fg.isValid());
        QCOMPARE(line.text(), QStringLiteral("+ added ok"));
    }
};

QObject *makeFoldLayerTest()
{
    return new FoldLayerTest;
}

#include "FoldLayerTest.moc"
