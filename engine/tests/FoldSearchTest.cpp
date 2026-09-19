// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::FoldSearch: finding inside expanded folds and merging those matches
// with a core's own into one sequence in visual order — without a GUI, without
// a core. The fake core below steps exactly the way LibVtermCore::searchStep()
// does (a cyclic index over the matches, the returned index counted from the
// newest), so the merge is tested against the contract, not an emulator.
#include "view/FoldSearch.h"

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

// A core with matches on the given absolute real rows, oldest first.
struct FakeCore {
    std::vector<int> rows;
    int current = -1; // index into rows, oldest = 0; -1 = nothing selected
    int steps = 0;

    FoldSearch::Core api()
    {
        FoldSearch::Core c;
        c.count = int(rows.size());
        c.step = [this](bool backwards) {
            FoldSearch::CorePos p;
            const int n = int(rows.size());
            if (n == 0)
                return p;
            ++steps;
            if (current < 0)
                current = backwards ? n - 1 : 0;
            else
                current = (current + (backwards ? n - 1 : 1)) % n;
            p.valid = true;
            p.row = rows[size_t(current)];
            p.index = n - 1 - current; // newest match is index 0
            return p;
        };
        c.currentRow = [this] { return current < 0 ? -1 : rows[size_t(current)]; };
        return c;
    }
};

// One fold of `body`, anchored under real row `anchor`, expanded.
void addFold(FoldLayer *layer, const QString &uri, int anchor, const QStringList &body)
{
    layer->setContent(uri, lines(body));
    layer->setAnchor(uri, anchor, anchor);
}

QString describe(const FoldSearch::Step &s)
{
    return QStringLiteral("kind=%1 match=%2 index=%3 count=%4 row=%5")
        .arg(s.kind == FoldSearch::Step::Fold ? QStringLiteral("fold")
                                              : s.kind == FoldSearch::Step::Core ? QStringLiteral("core")
                                                                                 : QStringLiteral("none"))
        .arg(s.match)
        .arg(s.index)
        .arg(s.count)
        .arg(s.visualRow);
}

} // namespace

class FoldSearchTest : public QObject {
    Q_OBJECT

private slots:
    void findsMatchesInsideAnExpandedFold()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20,
                {QStringLiteral("alpha needle one"), QStringLiteral("nothing here"), QStringLiteral("NEEDLE two")});
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        QCOMPARE(s.matchCount(layer), 2); // case-insensitive, like both cores
        const std::vector<FoldSearch::Match> &m = s.matches(layer);
        QCOMPARE(m[0].line, 0);
        QCOMPARE(m[0].from, 6);
        QCOMPARE(m[0].to, 12);
        QCOMPARE(m[1].line, 2);
        QCOMPARE(m[1].from, 0);

        // The fold hangs under real row 20, so its rows are visual 21, 22, 23.
        QCOMPARE(s.keyOf(layer, m[0]).row, 21);
        QCOMPARE(s.keyOf(layer, m[1]).row, 23);
    }

    void aMatchThatStraddlesTheWrapIsStillOneMatch()
    {
        FoldLayer layer;
        layer.setGeometry(20, 3); // 17 usable cells, so the line wraps
        addFold(&layer, QStringLiteral("a"), 5, {QStringLiteral("abcdefghijklmnopqrstuvwxyz")});
        FoldSearch s;
        s.setNeedle(QStringLiteral("pqrs"));
        QCOMPARE(s.matchCount(layer), 1);
        const FoldSearch::Match &m = s.matches(layer).front();
        QCOMPARE(m.from, 15);
        QCOMPARE(m.to, 19);
        // It is painted on both wrapped rows, clipped to each.
        const std::vector<FoldSearch::RowMatch> first = s.rowMatches(layer, 0, 0);
        QCOMPARE(int(first.size()), 1);
        QCOMPARE(first[0].from, 15);
        QCOMPARE(first[0].to, 17);
        const std::vector<FoldSearch::RowMatch> second = s.rowMatches(layer, 0, 1);
        QCOMPARE(int(second.size()), 1);
        QCOMPARE(second[0].from, 17);
        QCOMPARE(second[0].to, 19);
        // Its place in the sequence is the row its first cell is on.
        QCOMPARE(s.keyOf(layer, m).row, 6);
    }

    void shutAndUnanchoredFoldsAreNotSearched()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20, {QStringLiteral("needle in the open one")});
        layer.setContent(QStringLiteral("b"), lines({QStringLiteral("needle with no anchor yet")}));
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        QCOMPARE(s.matchCount(layer), 1);

        layer.setExpanded(QStringLiteral("a"), false);
        s.invalidate();
        QCOMPARE(s.matchCount(layer), 0);

        layer.setExpanded(QStringLiteral("a"), true);
        s.invalidate();
        QCOMPARE(s.matchCount(layer), 1);
    }

    // Core matches on real rows 10 and 30, a three-row fold under row 20 with
    // matches on its first and third row. In visual order the sequence is
    //   C(10)  F(21)  F(23)  C(33)
    // and the index counts from the newest: 3, 2, 1, 0.
    void stepsBothKindsInVisualOrderBackwards()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20,
                {QStringLiteral("needle one"), QStringLiteral("quiet"), QStringLiteral("needle two")});
        FakeCore core{{10, 30}};
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));

        FoldSearch::Step step = s.step(layer, true, core.api());
        QCOMPARE(step.count, 4);
        QVERIFY2(step.kind == FoldSearch::Step::Core && step.index == 0 && step.visualRow == 33,
                 qPrintable(describe(step)));

        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Fold && step.index == 1 && step.visualRow == 23,
                 qPrintable(describe(step)));
        QVERIFY(s.currentIsFold());

        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Fold && step.index == 2 && step.visualRow == 21,
                 qPrintable(describe(step)));

        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Core && step.index == 3 && step.visualRow == 10,
                 qPrintable(describe(step)));
        QVERIFY(!s.currentIsFold());

        // Wrap round to the newest again.
        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Core && step.index == 0 && step.visualRow == 33,
                 qPrintable(describe(step)));

        // Two fold matches were walked without ever stepping the core twice in
        // one call, and the core was never stepped and then undone.
        QCOMPARE(core.steps, 3);
    }

    void stepsBothKindsInVisualOrderForwards()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20,
                {QStringLiteral("needle one"), QStringLiteral("quiet"), QStringLiteral("needle two")});
        FakeCore core{{10, 30}};
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));

        const int wantIndex[5] = {3, 2, 1, 0, 3};
        const int wantRow[5] = {10, 21, 23, 33, 10};
        const FoldSearch::Step::Kind wantKind[5] = {FoldSearch::Step::Core, FoldSearch::Step::Fold,
                                                    FoldSearch::Step::Fold, FoldSearch::Step::Core,
                                                    FoldSearch::Step::Core};
        for (int i = 0; i < 5; ++i) {
            const FoldSearch::Step step = s.step(layer, false, core.api());
            QCOMPARE(step.count, 4);
            QVERIFY2(step.kind == wantKind[i] && step.index == wantIndex[i] && step.visualRow == wantRow[i],
                     qPrintable(QStringLiteral("at %1: %2").arg(i).arg(describe(step))));
        }
    }

    void reversingDirectionComesBackToTheSameMatch()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20,
                {QStringLiteral("needle one"), QStringLiteral("quiet"), QStringLiteral("needle two")});
        FakeCore core{{10, 30}};
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));

        s.step(layer, true, core.api());                          // C(33), index 0
        FoldSearch::Step step = s.step(layer, true, core.api());   // F(23), index 1
        QCOMPARE(step.index, 1);
        // Turn round: the core is parked on the older side and one step in the
        // new direction puts it back on the newer one.
        step = s.step(layer, false, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Core && step.index == 0 && step.visualRow == 33,
                 qPrintable(describe(step)));
        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Fold && step.index == 1, qPrintable(describe(step)));
    }

    void onlyFoldMatchesStillWalkAndWrap()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 4, {QStringLiteral("needle one"), QStringLiteral("needle two")});
        FakeCore core; // the core finds nothing
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));

        FoldSearch::Step step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Fold && step.count == 2 && step.index == 0 && step.visualRow == 6,
                 qPrintable(describe(step)));
        step = s.step(layer, true, core.api());
        QCOMPARE(step.index, 1);
        QCOMPARE(step.visualRow, 5);
        step = s.step(layer, true, core.api());
        QCOMPARE(step.index, 0); // wrapped
        QCOMPARE(core.steps, 0);
    }

    void onlyCoreMatchesBehaveExactlyAsTheCoreDoes()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20, {QStringLiteral("nothing to find here")});
        FakeCore core{{2, 7, 9}};
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        QCOMPARE(s.matchCount(layer), 0);

        for (int want : {0, 1, 2, 0}) {
            const FoldSearch::Step step = s.step(layer, true, core.api());
            QCOMPARE(step.count, 3);
            QCOMPARE(step.index, want);
            QCOMPARE(step.kind, FoldSearch::Step::Core);
        }
        QCOMPARE(core.steps, 4); // one core step per match, as before folds existed
    }

    void twoFoldsInterleaveWithTheRowsBetweenThem()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("b"), 30, {QStringLiteral("needle in the newer fold")});
        addFold(&layer, QStringLiteral("a"), 10, {QStringLiteral("needle in the older fold")});
        FakeCore core{{20}};
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        QCOMPARE(s.matchCount(layer), 2);
        // Visual: fold a at 11, real row 20 at 21, fold b at 32.
        QCOMPARE(s.keyOf(layer, s.matches(layer)[0]).row, 11);
        QCOMPARE(s.keyOf(layer, s.matches(layer)[1]).row, 32);

        FoldSearch::Step step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Fold && step.index == 0 && step.visualRow == 32,
                 qPrintable(describe(step)));
        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Core && step.index == 1 && step.visualRow == 21,
                 qPrintable(describe(step)));
        step = s.step(layer, true, core.api());
        QVERIFY2(step.kind == FoldSearch::Step::Fold && step.index == 2 && step.visualRow == 11,
                 qPrintable(describe(step)));
    }

    void shuttingAFoldChangesTheCountAndForgetsWhereTheWalkWas()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 20,
                {QStringLiteral("needle one"), QStringLiteral("needle two")});
        FakeCore core{{10}};
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        QCOMPARE(s.step(layer, true, core.api()).count, 3);
        QVERIFY(s.currentIsFold());

        layer.setExpanded(QStringLiteral("a"), false);
        s.invalidate();
        QCOMPARE(s.matchCount(layer), 0);
        QVERIFY(!s.currentIsFold());
        const FoldSearch::Step step = s.step(layer, true, core.api());
        QCOMPARE(step.count, 1);
        QCOMPARE(step.kind, FoldSearch::Step::Core);
        QCOMPARE(step.index, 0);
    }

    void rewrappingKeepsTheMatchesAndMovesTheirRows()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 5,
                {QStringLiteral("one"), QStringLiteral("a needle late in a long enough line to wrap")});
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        QCOMPARE(s.matchCount(layer), 1);
        const FoldSearch::Match before = s.matches(layer).front();
        QCOMPARE(s.keyOf(layer, before).row, 7);

        layer.setGeometry(16, 3); // 13 usable cells: the line wraps three ways
        s.invalidate();
        QCOMPARE(s.matchCount(layer), 1);
        const FoldSearch::Match after = s.matches(layer).front();
        QCOMPARE(after.from, before.from); // cells of a logical line, not of a row
        QCOMPARE(after.to, before.to);
        QCOMPARE(s.keyOf(layer, after).row, 7); // still the first row of that line
    }

    void theSelectedFoldMatchIsTheOnlyCurrentOne()
    {
        FoldLayer layer;
        layer.setGeometry(40, 3);
        addFold(&layer, QStringLiteral("a"), 4,
                {QStringLiteral("needle one"), QStringLiteral("needle two")});
        FakeCore core;
        FoldSearch s;
        s.setNeedle(QStringLiteral("needle"));
        s.step(layer, true, core.api()); // the newer of the two
        QCOMPARE(s.rowMatches(layer, 0, 0).front().current, false);
        QCOMPARE(s.rowMatches(layer, 0, 1).front().current, true);
        s.step(layer, true, core.api());
        QCOMPARE(s.rowMatches(layer, 0, 0).front().current, true);
        QCOMPARE(s.rowMatches(layer, 0, 1).front().current, false);
        // A needle nobody matches leaves nothing highlighted.
        s.setNeedle(QStringLiteral("zzz"));
        QVERIFY(s.rowMatches(layer, 0, 0).empty());
        QVERIFY(!s.currentIsFold());
    }
};

QObject *makeFoldSearchTest()
{
    return new FoldSearchTest;
}

#include "FoldSearchTest.moc"
