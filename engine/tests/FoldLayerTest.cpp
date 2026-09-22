// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::FoldLayer: the fold layer's maths without a GUI — wrapping, the
// visual-row <-> real-row mapping, anchoring, trimming and ordering, and the
// prose blocks that replace their own rows on a resize (#R2WQ).
#include "view/FoldLayer.h"
#include "view/ProseSpans.h"

#include "core/CellTypes.h"

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

    // #PPR4: a resolve is one batch — every anchor the view found, then the
    // folds it keeps — and costs one rebuild of the layout, whatever the
    // conversation has accumulated. It used to cost one per anchor, over a
    // membership list scanned per fold.
    void oneResolveRebuildsTheLayoutOnce()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        QSet<QString> seen;
        std::vector<FoldLayer::AnchorRows> anchors;
        for (int i = 0; i < 200; ++i) {
            const QString uri = QStringLiteral("relay://call/p/1/%1").arg(i);
            f.setContent(uri, lines({QStringLiteral("detail")}));
            anchors.push_back(FoldLayer::AnchorRows{uri, i * 2, i * 2});
            seen.insert(uri);
        }
        const quint64 before = f.rebuildCount();
        f.applyAnchors(anchors, seen);
        QCOMPARE(f.rebuildCount() - before, quint64(1));
        QCOMPARE(int(f.folds().size()), 200);
        QCOMPARE(f.expandedCount(), 200);
        // Every anchor landed, so the blocks sit where the runs said.
        QCOMPARE(f.visualOfReal(0), 0);
        QCOMPARE(f.visualOfReal(1), 2);   // the first block's row sits between them
        QCOMPARE(f.visualTotal(400), 600);

        // The oldest hundred have left the scrollback: one rebuild again, and
        // the folds that are gone go with them.
        QSet<QString> kept;
        std::vector<FoldLayer::AnchorRows> moved;
        for (int i = 100; i < 200; ++i) {
            const QString uri = QStringLiteral("relay://call/p/1/%1").arg(i);
            moved.push_back(FoldLayer::AnchorRows{uri, (i - 100) * 2, (i - 100) * 2});
            kept.insert(uri);
        }
        const quint64 second = f.rebuildCount();
        f.applyAnchors(moved, kept);
        QCOMPARE(f.rebuildCount() - second, quint64(1));
        QCOMPARE(int(f.folds().size()), 100);
        QVERIFY(!f.known(QStringLiteral("relay://call/p/1/0")));
        QVERIFY(f.known(QStringLiteral("relay://call/p/1/199")));
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

    // ---- word-aware wrapping (#R2WQ): one break rule everywhere

    void foldRowsBreakBetweenWords()
    {
        FoldLayer f;
        f.setGeometry(20, 3);   // 17 usable
        f.setContent(QStringLiteral("u"),
                     lines({QStringLiteral("commit RELAY_ENGINE_WITH_GHOSTTY describes the core")}));
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("u"));
        QStringList rows;
        for (int r = 0; r < fold->height(); ++r) {
            const QString text = f.rowText(0, r);
            QVERIFY2(!text.isEmpty(), "no empty rows while words remain");
            rows << text;
        }
        // Before #R2WQ this filled rows cluster by cluster: "commit RELAY_EN".
        QCOMPARE(rows.first().endsWith(QLatin1String("commit")), false);
        QVERIFY(rows.join(QLatin1Char(' ')).contains(QLatin1String("commit")));
        QVERIFY(rows.join(QLatin1Char(' ')).contains(QLatin1String("describes")));
        // No row runs past the block's usable width.
        for (const QString &row : rows)
            QVERIFY2(row.size() <= 17, qPrintable(row));
        // ... and no row ends inside a word: each row's last word is whole.
        const QStringList words = QString(QStringLiteral("commit RELAY_ENGINE_WITH_GHOSTTY describes the core"))
                                      .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &row : rows) {
            const QString last = row.trimmed().section(' ', -1);
            QVERIFY2(words.contains(last) || last == row.trimmed(), qPrintable(row));
        }
    }

    // ---- prose blocks (#R2WQ): the pane's own lines, replaced on a resize

    void proseStandsAsideAtThePrintWidth()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setProse(QStringLiteral("relay://prose/p/1"),
                   lines({QStringLiteral("one two three four five six seven eight")}), 80);
        f.setAnchor(QStringLiteral("relay://prose/p/1"), 4, 5);   // the two rows it printed at 80
        QVERIFY(!f.active());
        QCOMPARE(f.visualTotal(100), 100);
        QCOMPARE(f.at(4).fold, false);
        QCOMPARE(f.at(4).realRow, 4);
        QCOMPARE(f.at(5).realRow, 5);
        QCOMPARE(f.at(6).realRow, 6);
        QVERIFY(!f.rowHidden(4));
        QCOMPARE(f.fold(QStringLiteral("relay://prose/p/1"))->height(), 1);   // it fits at 80
    }

    void proseTakesOverAwayFromThePrintWidth()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        const QString uri = QStringLiteral("relay://prose/p/1");
        f.setProse(uri, lines({QStringLiteral("one two three four five six seven eight nine ten")}), 80);
        f.setAnchor(uri, 4, 5);   // two real rows at 80
        QVERIFY(!f.active());
        f.setGeometry(40, 3);     // narrower: the line wraps to two rows
        QVERIFY(f.active());
        QCOMPARE(f.fold(uri)->height(), 2);
        QCOMPARE(f.visualTotal(100), 100);   // two painted for two hidden
        QVERIFY(f.rowHidden(4));
        QVERIFY(f.rowHidden(5));
        QVERIFY(!f.rowHidden(6));
        // The block starts where its first hidden row was; rows after it move.
        QCOMPARE(f.visualOfReal(3), 3);
        QCOMPARE(f.visualOfReal(4), 4);      // a hidden row answers the block's first row
        QCOMPARE(f.visualOfReal(6), 6);
        QVERIFY(f.at(4).fold);
        QCOMPARE(f.at(4).foldRow, 0);
        QVERIFY(f.at(5).fold);
        QCOMPARE(f.at(5).foldRow, 1);
        QCOMPARE(f.at(6).fold, false);
        QCOMPARE(f.at(6).realRow, 6);
        // Back at the print width it stands aside again, and the printed rows show.
        f.setGeometry(80, 3);
        QVERIFY(!f.active());
        QCOMPARE(f.visualTotal(100), 100);
        QCOMPARE(f.at(4).realRow, 4);
        // Wider than printed: taken over too, with fewer rows than it hides.
        f.setGeometry(120, 3);
        QVERIFY(f.active());
        QCOMPARE(f.fold(uri)->height(), 1);
        QCOMPARE(f.visualTotal(100), 99);
        QVERIFY(f.at(4).fold);               // the block's one row, where the two were
        QCOMPARE(f.at(4).foldRow, 0);
        QCOMPARE(f.at(5).realRow, 6);        // everything below moves up a row
    }

    void proseWrapsWithTheSharedRuleAndHangs()
    {
        FoldLayer f;
        f.setGeometry(48, 3);
        const QString uri = QStringLiteral("relay://prose/p/2");
        f.setProse(uri, lines({QStringLiteral("• one two three four five six seven eight nine ten eleven twelve")}), 80);
        f.setAnchor(uri, 0, 2);
        QVERIFY(f.active());
        const FoldLayer::Fold *fold = f.fold(uri);
        QVERIFY(fold->height() > 1);
        // The first row starts at column 0; every continuation hangs past the bullet.
        QCOMPARE(fold->rows[0].startCol, 0);
        for (int r = 1; r < fold->height(); ++r) {
            QCOMPARE(fold->rows[size_t(r)].startCol, 2);
            QVERIFY2(!f.rowText(0, int(r)).startsWith(QLatin1Char(' ')), "no leading spaces in cells");
        }
        // No row ends mid-word and none is stranded short by the old width.
        for (int r = 0; r < fold->height(); ++r) {
            const QString row = f.rowText(0, r);
            QVERIFY2(row.size() + fold->rows[size_t(r)].startCol <= 48, qPrintable(row));
            QVERIFY2(!row.isEmpty(), "no empty rows");
        }
    }

    void proseKeepsItsInkAndRole()
    {
        FoldSpan span;
        span.text = QStringLiteral("heading");
        span.sgr = QStringLiteral("1;35");   // bold magenta, as MarkdownAnsi renders it
        span.bold = true;
        FoldLine line;
        line.spans << span;
        line.role = kFoldRoleAgent;
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setProse(QStringLiteral("relay://prose/p/3"), QVector<FoldLine>{line}, 40);
        f.setAnchor(QStringLiteral("relay://prose/p/3"), 0, 0);
        QVERIFY(f.active());
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("relay://prose/p/3"));
        QCOMPARE(fold->cells[0][0].fgPacked, CellColor::indexed(5));   // ANSI 35
        QVERIFY(fold->cells[0][0].bold);
        QCOMPARE(fold->lines[0].role, kFoldRoleAgent);
    }

    void proseAndFoldsStack()
    {
        FoldLayer f;
        f.setGeometry(80, 3);
        const QString prose = QStringLiteral("relay://prose/p/4");
        f.setProse(prose, lines({QStringLiteral("a b c d e f g h i j k l m n o p")}), 80);
        f.setAnchor(prose, 2, 3);
        f.setContent(QStringLiteral("call"), lines({QStringLiteral("1"), QStringLiteral("2")}));
        f.setAnchor(QStringLiteral("call"), 6, 6);
        // At 80 the call fold still inserts; the prose block stands aside.
        QCOMPARE(f.visualTotal(20), 22);
        // At 48 the prose block takes its two rows over: one painted row, and
        // the call fold still inserts its two.
        f.setGeometry(48, 3);
        QCOMPARE(f.fold(prose)->height(), 1);
        QCOMPARE(f.visualTotal(20), 21);     // 20 - 2 hidden + 1 prose + 2 call
        QVERIFY(f.at(2).fold);               // the prose block
        QCOMPARE(f.at(2).foldIndex, f.indexOf(prose));
        QCOMPARE(f.at(3).realRow, 4);        // the row after the hidden span
        QVERIFY(f.at(6).fold);               // the call fold's rows, under row 6
        QVERIFY(f.at(7).fold);
        QCOMPARE(f.at(8).realRow, 7);
        // Round trip for every row that is not hidden.
        for (int r = 0; r < 20; ++r) {
            if (f.rowHidden(r))
                continue;
            QCOMPARE(f.at(f.visualOfReal(r)).realRow, r);
        }
    }

    void proseIsNeverAFoldAnchor()
    {
        QVERIFY(FoldLayer::isProseUri(QStringLiteral("relay://prose/p/1")));
        QVERIFY(!FoldLayer::isProseUri(QStringLiteral("relay://call/p/1/2")));
        QVERIFY(!FoldLayer::isProseUri(QString()));
        FoldLayer f;
        f.setPrefix(QStringLiteral("relay://call/"));
        QVERIFY(!f.isAnchorUri(QStringLiteral("relay://prose/p/1")));
        f.setGeometry(80, 3);
        f.setProse(QStringLiteral("relay://prose/p/5"), lines({QStringLiteral("x")}), 40);
        f.setAnchor(QStringLiteral("relay://prose/p/5"), 0, 0);
        // The chevron is an insertion fold's; prose rows are text from column 0.
        QCOMPARE(f.foldAtAnchorStart(0), -1);
        QCOMPARE(f.rowStartCol(0, 0), 0);
        // ... and prose is not offered to the host as an open fold.
        QVERIFY(!f.expandedUris().contains(QStringLiteral("relay://prose/p/5")));
    }

    // ---- the pane's ANSI -> FoldLine conversion (#R2WQ): what the pane hands
    // the view is the text before the wrapper, with its styles as spans.

    void proseSpansKeepTheRenderedStyles()
    {
        ProseCollector c;
        c.feed(QStringLiteral("plain \x1b[1mbold\x1b[0m tail\n"));
        c.feed(QStringLiteral("\x1b[1;35m## heading\x1b[0m\n"));
        const QVector<FoldLine> out = c.take();
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].text(), QStringLiteral("plain bold tail"));
        QCOMPARE(out[0].spans.size(), 3);
        QCOMPARE(out[0].spans[1].text, QStringLiteral("bold"));
        QVERIFY(out[0].spans[1].bold);
        QCOMPARE(out[0].spans[1].sgr, QStringLiteral("1"));
        QVERIFY(!out[0].spans[0].bold);
        QVERIFY(out[0].spans[0].sgr.isEmpty());
        QCOMPARE(out[1].spans.first().sgr, QStringLiteral("1;35"));
        QVERIFY(out[1].spans.first().bold);
        // The SGR is what the fold layer resolves against the theme.
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setProse(QStringLiteral("relay://prose/x"), out, 40);
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("relay://prose/x"));
        QCOMPARE(fold->cells[1].back().fgPacked, CellColor::indexed(5));   // magenta
    }

    // #RW9T: `~~struck~~` renders as SGR 9, which the grid paints (AttrStrike). It has to reach
    // the fold's cells too, or a block loses its rule the moment a resize takes it over.
    void proseSpansKeepStrikethrough()
    {
        ProseCollector c;
        c.feed(QStringLiteral("keep \x1b[9mgone\x1b[29m back\x1b[0m\n"));
        const QVector<FoldLine> out = c.take();
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].spans.size(), 3);
        QVERIFY(!out[0].spans[0].strike);
        QCOMPARE(out[0].spans[1].text, QStringLiteral("gone"));
        QVERIFY(out[0].spans[1].strike);
        QCOMPARE(out[0].spans[1].sgr, QStringLiteral("9"));
        QVERIFY(!out[0].spans[2].strike);
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setProse(QStringLiteral("relay://prose/s"), out, 40);
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("relay://prose/s"));
        QVERIFY(fold->cells[0][5].strike);    // 'g' of "gone"
        QVERIFY(!fold->cells[0][0].strike);   // 'k' of "keep"
    }

    // #RW9T: only an SGR carries style. An erase or a cursor move has parameters that read as
    // SGR parameters, and handing them to the same parser set faint, bold or an ink.
    void proseSpansIgnoreNonSgrControlSequences()
    {
        ProseCollector c;
        c.feed(QStringLiteral("\x1b[2K\x1b[1G\x1b[3Aplain\n"));
        const QVector<FoldLine> out = c.take();
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].spans.size(), 1);
        QCOMPARE(out[0].spans[0].text, QStringLiteral("plain"));
        QVERIFY(!out[0].spans[0].dim);
        QVERIFY(!out[0].spans[0].bold);
        QVERIFY(!out[0].spans[0].italic);
        QVERIFY(out[0].spans[0].sgr.isEmpty());
    }

    void proseSpansSurviveArbitraryChunkBoundaries()
    {
        const QString rendered = QStringLiteral(
            "one two \x1b[2;97mthree\x1b[0m four\n\x1b[3;36m- a quoted bullet\x1b[0m\n");
        ProseCollector whole;
        whole.feed(rendered);
        for (int cut : {1, 5, 13, 27, 40, int(rendered.size()) - 1}) {
            ProseCollector streamed;
            streamed.feed(rendered.left(cut));
            streamed.feed(rendered.mid(cut));
            const QVector<FoldLine> a = whole.take(), b = streamed.take();
            QCOMPARE(b.size(), a.size());
            for (int i = 0; i < a.size(); ++i) {
                QCOMPARE(b[i].text(), a[i].text());
                QCOMPARE(b[i].spans.size(), a[i].spans.size());
                for (int s = 0; s < a[i].spans.size(); ++s)
                    QCOMPARE(b[i].spans[s].sgr, a[i].spans[s].sgr);
            }
        }
    }

    void proseSpansDropTrailingEmptyLinesAndKeepInteriorOnes()
    {
        ProseCollector a;
        a.feed(QStringLiteral("reply\n"));
        QCOMPARE(a.take().size(), 1);
        ProseCollector b;
        b.feed(QStringLiteral("para one\n\npara two\n\n"));
        const QVector<FoldLine> lines = b.take();
        QCOMPARE(lines.size(), 3);   // the trailing blank is not the block's
        QCOMPARE(lines[1].text(), QString());
        ProseCollector c;
        c.feed(QString());
        QVERIFY(c.take().isEmpty());
        // A line of blanks is a span of spaces: it stays.
        ProseCollector d;
        d.feed(QStringLiteral("x\n  \ny\n"));
        QCOMPARE(d.take().size(), 3);
        // take() does not reset: the pane replaces the collector when a new
        // block opens (openProse assigns a fresh one).
    }

    void proseSpansSkipOscAndOtherSequences()
    {
        // The role mark the pane writes per row, and an OSC 8 run, are zero
        // width to the collector, exactly as they are to the terminal.
        ProseCollector c;
        c.feed(QStringLiteral("\x1b]7772;shell\x1b\\\x1b[1ma line the user typed\x1b[0m\n"));
        const QVector<FoldLine> lines = c.take();
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].text(), QStringLiteral("a line the user typed"));
        QVERIFY(lines[0].spans.first().bold);
    }

    // ---- a markdown link's label, re-wrapped (card #MDKN)
    //
    // At the width the block was printed at the label is clickable because its cells carry the
    // OSC 8 run; at any other width the view paints its own wrap of these lines instead, so the
    // target has to be here too. FoldSpan::link is the field a tool-call fold's rows already use,
    // and FoldLayer copies it onto the cell the hit test reads.
    void proseSpansCarryALabelsLinkAndNotTheAnchor()
    {
        ProseCollector c;
        c.feed(QStringLiteral("see \x1b]8;;relay://prose/p/1#l=option%3Aa%2Fb\x1b\\\x1b[4;32mdocs\x1b[0m"
                              "\x1b]8;;relay://prose/p/1\x1b\\\x1b[2;97m (option:a/b)\x1b[0m now\n"));
        const QVector<FoldLine> lines = c.take();
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].text(), QStringLiteral("see docs (option:a/b) now"));
        QString linked;
        for (const FoldSpan &s : lines[0].spans)
            if (!s.link.isEmpty()) linked += s.text;
        QCOMPARE(linked, QStringLiteral("docs"));
        for (const FoldSpan &s : lines[0].spans)
            if (!s.link.isEmpty())
                QCOMPARE(s.link, QStringLiteral("relay://prose/p/1#l=option%3Aa%2Fb"));
        // The block's own anchor is a handle, not a destination: no span carries it, or the
        // whole block would be one enormous link.
        for (const FoldSpan &s : lines[0].spans)
            QVERIFY(s.link != QStringLiteral("relay://prose/p/1"));
        // And the cell the hit test reads has it.
        FoldLayer f;
        f.setGeometry(80, 3);
        f.setProse(QStringLiteral("relay://prose/p/1"), lines, 80);
        const FoldLayer::Fold *fold = f.fold(QStringLiteral("relay://prose/p/1"));
        QCOMPARE(fold->cells[0][4].link, QStringLiteral("relay://prose/p/1#l=option%3Aa%2Fb"));
        QVERIFY(fold->cells[0][0].link.isEmpty());
    }

    void aLabelsLinkSurvivesArbitraryChunkBoundaries()
    {
        const QString rendered =
            QStringLiteral("a \x1b]8;;relay://prose/p/2#l=%23K7Q2\x1b\\\x1b[4;32mthe card\x1b[0m"
                           "\x1b]8;;relay://prose/p/2\x1b\\ b\n");
        ProseCollector whole;
        whole.feed(rendered);
        const QVector<FoldLine> want = whole.take();
        for (int cut = 1; cut < rendered.size(); ++cut) {
            ProseCollector streamed;
            streamed.feed(rendered.left(cut));
            streamed.feed(rendered.mid(cut));
            const QVector<FoldLine> got = streamed.take();
            QCOMPARE(got.size(), want.size());
            QCOMPARE(got[0].text(), want[0].text());
            QString a, b;
            for (const FoldSpan &s : want[0].spans) if (!s.link.isEmpty()) a += s.text;
            for (const FoldSpan &s : got[0].spans) if (!s.link.isEmpty()) b += s.text;
            QCOMPARE(b, a);
        }
    }
};

QObject *makeFoldLayerTest()
{
    return new FoldLayerTest;
}

#include "FoldLayerTest.moc"
