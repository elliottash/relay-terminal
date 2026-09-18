// SPDX-License-Identifier: GPL-3.0-or-later
// Pane movement: which pane Alt+arrow focuses and Ctrl+Alt+arrow moves past, what the splitter
// order is afterwards, and which edge a dragged pane is dropped on.
//
// The swap tests run against a real QSplitter, because the regression they cover was a wrong
// reading of QSplitter::insertWidget: it MOVES a child the splitter already owns and numbers
// the target index as if that child had been taken out first, so the single insert is already
// the whole swap. The old code "finished" a move toward the end with a second insert of the
// neighbour, which put both panes back where they started and made Ctrl+Alt+Right and
// Ctrl+Alt+Down do nothing at all.
#include "PaneLayout.h"

#include <QLabel>
#include <QSplitter>
#include <QStringList>
#include <QTest>

#include <memory>

using namespace relay::panes;

namespace {

// A splitter of named panes, so a test can state the order as a string.
QSplitter *splitterOf(Qt::Orientation orientation, const QStringList &names) {
    auto *splitter = new QSplitter(orientation);
    for (const QString &name : names) {
        auto *pane = new QLabel(name);
        pane->setObjectName(name);
        splitter->addWidget(pane);
    }
    return splitter;
}

QString order(QSplitter *splitter) {
    QStringList names;
    for (int i = 0; i < splitter->count(); ++i) names << splitter->widget(i)->objectName();
    return names.join(QLatin1Char(','));
}

QWidget *paneNamed(QSplitter *splitter, const QString &name) {
    for (int i = 0; i < splitter->count(); ++i)
        if (splitter->widget(i)->objectName() == name) return splitter->widget(i);
    return nullptr;
}

void swap(QSplitter *splitter, const QString &current, const QString &neighbor) {
    swapInSplitter(splitter, paneNamed(splitter, current), paneNamed(splitter, neighbor));
}

}  // namespace

class PaneLayoutTests : public QObject {
    Q_OBJECT
private Q_SLOTS:

    // ----- the neighbour a direction points at ------------------------------------------------

    void neighborPicksThePaneOnThatSide() {
        const QRect left(0, 0, 500, 900);
        const QList<QRect> others{QRect(500, 0, 500, 900)};
        QCOMPARE(neighborIndex(left, others, Direction::Right), 0);
        QCOMPARE(neighborIndex(left, others, Direction::Left), -1);
        QCOMPARE(neighborIndex(left, others, Direction::Up), -1);
        QCOMPARE(neighborIndex(left, others, Direction::Down), -1);

        const QRect right(500, 0, 500, 900);
        const QList<QRect> back{left};
        QCOMPARE(neighborIndex(right, back, Direction::Left), 0);
        QCOMPARE(neighborIndex(right, back, Direction::Right), -1);
    }

    void neighborPrefersTheNearestThenTheMostAligned() {
        const QRect from(0, 0, 300, 900);
        // Two panes to the right: the near one wins whichever is better aligned.
        const QList<QRect> candidates{QRect(700, 0, 300, 900), QRect(300, 0, 400, 900)};
        QCOMPARE(neighborIndex(from, candidates, Direction::Right), 1);

        // Equally near, so the one whose centre lines up with this pane wins.
        const QRect tall(0, 0, 300, 900);
        const QList<QRect> stacked{QRect(300, 600, 300, 300), QRect(300, 300, 300, 300)};
        QCOMPARE(neighborIndex(tall, stacked, Direction::Right), 1);
    }

    void neighborWorksInAVerticalSplit() {
        const QRect top(0, 0, 1000, 400);
        const QList<QRect> below{QRect(0, 400, 1000, 500)};
        QCOMPARE(neighborIndex(top, below, Direction::Down), 0);
        QCOMPARE(neighborIndex(top, below, Direction::Up), -1);
        QCOMPARE(neighborIndex(below.first(), {top}, Direction::Up), 0);
    }

    void noNeighborWithoutCandidates() {
        QCOMPARE(neighborIndex(QRect(0, 0, 100, 100), {}, Direction::Right), -1);
    }

    void directionsKnowTheirSplitter() {
        QVERIFY(towardStart(Direction::Left));
        QVERIFY(towardStart(Direction::Up));
        QVERIFY(!towardStart(Direction::Right));
        QVERIFY(!towardStart(Direction::Down));
        QCOMPARE(orientationFor(Direction::Left), Qt::Horizontal);
        QCOMPARE(orientationFor(Direction::Right), Qt::Horizontal);
        QCOMPARE(orientationFor(Direction::Up), Qt::Vertical);
        QCOMPARE(orientationFor(Direction::Down), Qt::Vertical);
    }

    // ----- the swap Ctrl+Alt+arrow performs ----------------------------------------------------

    void swapMovesAPaneTowardTheEnd() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B")}));
        swap(splitter.get(), QStringLiteral("A"), QStringLiteral("B"));
        QCOMPARE(order(splitter.get()), QStringLiteral("B,A"));
    }

    void swapMovesAPaneTowardTheStart() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B")}));
        swap(splitter.get(), QStringLiteral("B"), QStringLiteral("A"));
        QCOMPARE(order(splitter.get()), QStringLiteral("B,A"));
    }

    // The report: Ctrl+Alt+Right did nothing, so a pane could never come back.
    void swappingBackAndForthReturnsTheOriginalOrder() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B")}));
        swap(splitter.get(), QStringLiteral("A"), QStringLiteral("B"));
        QCOMPARE(order(splitter.get()), QStringLiteral("B,A"));
        swap(splitter.get(), QStringLiteral("A"), QStringLiteral("B"));
        QCOMPARE(order(splitter.get()), QStringLiteral("A,B"));
    }

    void swapOnlyTouchesTheTwoPanes() {
        std::unique_ptr<QSplitter> splitter(
            splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")}));
        swap(splitter.get(), QStringLiteral("B"), QStringLiteral("C"));   // B moves right
        QCOMPARE(order(splitter.get()), QStringLiteral("A,C,B,D"));
        swap(splitter.get(), QStringLiteral("B"), QStringLiteral("C"));   // and back again
        QCOMPARE(order(splitter.get()), QStringLiteral("A,B,C,D"));
    }

    void swapAtTheEndsOfTheSplitter() {
        std::unique_ptr<QSplitter> splitter(
            splitterOf(Qt::Vertical, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        swap(splitter.get(), QStringLiteral("C"), QStringLiteral("B"));   // last pane upward
        QCOMPARE(order(splitter.get()), QStringLiteral("A,C,B"));
        swap(splitter.get(), QStringLiteral("A"), QStringLiteral("C"));   // first pane downward
        QCOMPARE(order(splitter.get()), QStringLiteral("C,A,B"));
    }

    void swapKeepsThePaneSizes() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B")}));
        splitter->resize(1000, 400);
        splitter->setSizes({300, 690});
        splitter->show();
        const QList<int> before = splitter->sizes();
        swap(splitter.get(), QStringLiteral("A"), QStringLiteral("B"));
        QCOMPARE(splitter->sizes(), before);
    }

    void swapIgnoresPanesFromAnotherSplitter() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B")}));
        std::unique_ptr<QSplitter> other(splitterOf(Qt::Horizontal, {QStringLiteral("X")}));
        swapInSplitter(splitter.get(), paneNamed(splitter.get(), QStringLiteral("A")), paneNamed(other.get(), QStringLiteral("X")));
        QCOMPARE(order(splitter.get()), QStringLiteral("A,B"));
        QCOMPARE(order(other.get()), QStringLiteral("X"));
        swapInSplitter(nullptr, paneNamed(splitter.get(), QStringLiteral("A")), paneNamed(splitter.get(), QStringLiteral("B")));
        QCOMPARE(order(splitter.get()), QStringLiteral("A,B"));
    }

    // ----- the edge a dragged pane is dropped on ----------------------------------------------

    void dropEdgePicksTheNearestEdge() {
        const QSize size(800, 600);
        QCOMPARE(dropEdge(QPoint(20, 300), size), Direction::Left);
        QCOMPARE(dropEdge(QPoint(780, 300), size), Direction::Right);
        QCOMPARE(dropEdge(QPoint(400, 20), size), Direction::Up);
        QCOMPARE(dropEdge(QPoint(400, 580), size), Direction::Down);
    }

    void dropEdgeSurvivesAZeroSizedPane() {
        QCOMPARE(dropEdge(QPoint(0, 0), QSize(0, 0)), Direction::Left);
    }

    // ----- pane sizes across a prompt-box that comes and goes (#G152) --------------------------

    void enclosingSplittersAreListedInnermostFirst() {
        auto *outer = new QSplitter(Qt::Vertical);
        auto *inner = new QSplitter(Qt::Horizontal);
        auto *pane = new QLabel(QStringLiteral("A"));
        inner->addWidget(pane);
        outer->addWidget(inner);
        std::unique_ptr<QSplitter> owner(outer);
        const auto splitters = enclosingSplitters(pane);
        QCOMPARE(splitters.size(), 2);
        QCOMPARE(splitters.at(0).data(), inner);
        QCOMPARE(splitters.at(1).data(), outer);
        QCOMPARE(enclosingSplitters(nullptr).size(), 0);
        QCOMPARE(enclosingSplitters(outer).size(), 0);
    }

    // Taking control of the terminal hides the prompt box, which changes that pane's minimum size.
    // The splitter redistributes every pane when that happens; these are the sizes that go back.
    void restoreSizesPutsThePanesBackAfterAMinimumChanges() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        splitter->resize(900, 400);
        splitter->setSizes({300, 300, 300});
        const auto splitters = enclosingSplitters(paneNamed(splitter.get(), QStringLiteral("C")));
        QCOMPARE(splitters.size(), 1);
        const QList<QList<int>> before{splitter->sizes()};
        // Something inside one pane goes away and the splitter moves the dividers.
        splitter->setSizes({500, 300, 100});
        QVERIFY(splitter->sizes() != before.first());
        restoreSizes(splitters, before);
        QCOMPARE(splitter->sizes(), before.first());
    }

    void restoreSizesSkipsASplitterThatChanged() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B")}));
        splitter->resize(600, 400);
        const QList<QPointer<QSplitter>> splitters{splitter.get()};
        // Sizes recorded for two panes must not be forced onto a splitter that now has three.
        const QList<QList<int>> stale{{300, 300}};
        splitter->addWidget(new QLabel(QStringLiteral("C")));
        const QList<int> current = splitter->sizes();
        restoreSizes(splitters, stale);
        QCOMPARE(splitter->sizes(), current);
        // A splitter that has gone away is skipped rather than crashing.
        const QList<QPointer<QSplitter>> gone{QPointer<QSplitter>()};
        restoreSizes(gone, stale);
    }
};

QTEST_MAIN(PaneLayoutTests)
#include "panelayout_test.moc"
