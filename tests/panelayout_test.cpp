// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include <QMap>
#include <QSplitter>
#include <QStringList>
#include <QTest>

#include <memory>
#include <numeric>
#include <utility>

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

// ----- the pane header's give-way ladder ---------------------------------------------------------

// A header with everything on, in a pane that is wide enough for all of it: the state glyph, the
// title, the directory, an ssh chip whose "user@host" is wider than the chip's
// 150 px floor, the phone chip, the usage meter and the subagent badge. The numbers are roughly
// what those elements measure with the app font, and every threshold below is derived from them
// rather than written down, so the tests say what the order is and not what the font is.
HeaderWants everythingOn() {
    HeaderWants wants;
    wants.title = 180;            // "Fixing the pane drag"
    wants.directory = 240;        // "TERMINAL  ~/src/relay-terminal"
    wants.ssh = 170;              // "⇄ elliott@sphinxpad"
    wants.sshHost = 120;          // "⇄ sphinxpad"
    wants.sshEllipsis = 40;       // "⇄ …": the chip cannot go under this
    wants.usage = 78;             // "12% 41%"
    wants.usageCpu = 44;          // "12%"
    wants.glyph = 16;
    wants.badge = 96;             // "2 subagents"
    wants.chips = 52;             // the phone chip
    wants.fixed = 32 + 6 * 8;     // the hover button row's room and the row's spacing
    return wants;
}

// What the elements that never give way, plus the row itself, have taken before the ladder starts.
int hardWidth(const HeaderWants &w) { return w.fixed + w.glyph + w.badge + w.chips; }

// The header width at which everything is whole and there is not a pixel to spare.
int fullWidth(const HeaderWants &w) {
    return hardWidth(w) + w.title + w.directory + w.ssh + w.usage;
}

// Nothing is left to the layout to squeeze, so the whole row must fit in the header.
bool fits(const HeaderWants &w, const HeaderFit &fit, int width) {
    return hardWidth(w) + fit.title + fit.directory + fit.sshPx + fit.usagePx <= width;
}

// How far down the ladder a fit is: 0 nothing has given way, then one per step of the owner's
// order, 7 every rung taken. Written for everythingOn(), where each element really is there.
// The state word was two of these steps until card #0STR took it off the header altogether.
int rung(const HeaderWants &w, const HeaderFit &fit) {
    if (fit.usage == UsageForm::CpuOnly) return 7;
    if (fit.ssh == SshForm::HostOnly) return fit.sshPx < w.sshHost ? 6 : 5;
    if (fit.sshPx < w.ssh) return 4;
    if (fit.title < w.title) return 3;
    if (fit.directory == 0) return 2;
    if (fit.directory < w.directory) return 1;
    return 0;
}

// A fit as one comparable line, so two sweeps of the same widths can be compared whole.
QString describe(const HeaderFit &fit) {
    return QStringLiteral("t%1 d%2 s%3/%4 u%5/%6 short%7")
        .arg(fit.title).arg(fit.directory)
        .arg(int(fit.ssh)).arg(fit.sshPx).arg(int(fit.usage)).arg(fit.usagePx).arg(fit.shortfall);
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

    // ----- docking a pane beside a neighbour (owner, 2026-09-19) ------------------------------

    // Docking used to hand every child of the splitter an equal share, so a keystroke undid the
    // widths the person had dragged. Only the anchor's share is divided.
    void dockingSplitsOnlyTheAnchorsShare() {
        // Two panes, the newcomer beside the wide one: the narrow one is not touched.
        QCOMPARE(sizesAfterDock({300, 690}, 1), QList<int>({300, 345, 345}));
        QCOMPARE(sizesAfterDock({300, 690}, 0), QList<int>({150, 150, 690}));
        // Three panes, the newcomer beside the middle: both ends keep what they had.
        QCOMPARE(sizesAfterDock({200, 500, 300}, 1), QList<int>({200, 250, 250, 300}));
        // An odd share divides floor then ceil, as a fresh two-pane splitter divides itself.
        QCOMPARE(sizesAfterDock({201, 300}, 0), QList<int>({100, 101, 300}));
        // Nothing is created or lost: the splitter still adds up to what it did.
        for (const QList<int> &sizes : {QList<int>{300, 690}, QList<int>{200, 500, 300}, QList<int>{201, 300}})
            for (int i = 0; i < sizes.size(); ++i) {
                const QList<int> after = sizesAfterDock(sizes, i);
                QCOMPARE(after.size(), sizes.size() + 1);
                QCOMPARE(std::accumulate(after.cbegin(), after.cend(), 0),
                         std::accumulate(sizes.cbegin(), sizes.cend(), 0));
            }
    }

    // What cannot be divided says so, rather than sizing the pane that was just docked to nothing.
    void dockingRefusesWhatItCannotDivide() {
        QVERIFY(sizesAfterDock({}, 0).isEmpty());
        QVERIFY(sizesAfterDock({300, 690}, -1).isEmpty());
        QVERIFY(sizesAfterDock({300, 690}, 2).isEmpty());
        QVERIFY(sizesAfterDock({0, 0}, 0).isEmpty());          // a splitter not laid out yet
        QVERIFY(sizesAfterDock({300, 0, 690}, 1).isEmpty());   // an anchor with no room to give
    }

    // The arithmetic against a real QSplitter, the way RelayWindow::insertBeside uses it: read the
    // sizes, insert, set them. The panes that were not the anchor come out with the widths they
    // had, which is the whole of the fix; equal shares would have made all four 250.
    void dockingKeepsTheOtherPanesWidths() {
        std::unique_ptr<QSplitter> splitter(splitterOf(Qt::Horizontal, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        splitter->setHandleWidth(0);
        splitter->setChildrenCollapsible(false);
        splitter->resize(1000, 400);
        splitter->setSizes({200, 500, 300});
        splitter->show();
        const int anchorIndex = 1;
        const QList<int> kept = sizesAfterDock(splitter->sizes(), anchorIndex);
        auto *docked = new QLabel(QStringLiteral("D"));
        docked->setObjectName(QStringLiteral("D"));
        splitter->insertWidget(anchorIndex + 1, docked);
        QCOMPARE(kept.size(), splitter->count());
        splitter->setSizes(kept);
        QCOMPARE(order(splitter.get()), QStringLiteral("A,B,D,C"));
        const QList<int> after = splitter->sizes();
        QCOMPARE(after.size(), 4);
        QVERIFY2(std::abs(after.at(0) - 200) <= 2, qPrintable(QStringLiteral("A is %1").arg(after.at(0))));
        QVERIFY2(std::abs(after.at(3) - 300) <= 2, qPrintable(QStringLiteral("C is %1").arg(after.at(3))));
        QVERIFY2(std::abs(after.at(1) - after.at(2)) <= 2, "the anchor and the newcomer halve its share");
        QVERIFY2(after.at(1) > 200, "and neither half is the equal share the splitter used to force");
    }

    // ----- moving a pane past the page's edge --------------------------------------------------

    // A pane that already spans the page across the direction of travel has that edge to
    // itself; one that only reaches it — a pane of a stack — still has a column to be given.
    void fillingTheEdgeMeansNothingToMove() {
        const QRect page(0, 0, 1000, 600);
        const QRect rightmostAlone(700, 0, 300, 600);      // a lone pane in the rightmost column
        const QRect rightmostStacked(700, 300, 300, 300);  // the bottom pane of a stack there
        const QRect topRowAlone(0, 0, 1000, 200);          // a lone pane in the topmost row
        const QRect topRowStacked(500, 0, 500, 200);       // the right half of that row
        const QRect only(0, 0, 1000, 600);                 // the tab's only pane
        QVERIFY(fillsTheEdge(rightmostAlone, page, Direction::Right));
        QVERIFY(!fillsTheEdge(rightmostStacked, page, Direction::Right));
        QVERIFY(fillsTheEdge(topRowAlone, page, Direction::Up));
        QVERIFY(!fillsTheEdge(topRowStacked, page, Direction::Up));
        QVERIFY(fillsTheEdge(only, page, Direction::Left));
        QVERIFY(fillsTheEdge(only, page, Direction::Down));
        // A rounding pixel short of the boundary still counts as filling it.
        QVERIFY(fillsTheEdge(QRect(700, 2, 300, 598), page, Direction::Right));
    }

    // The newcomer's share is one of the regions there will be once it has landed; the panes
    // that were already on the page keep their relative sizes, and nothing is created or lost.
    void edgeDockGivesTheNewcomerOneEqualShare() {
        QCOMPARE(sizesAfterEdgeDock({300, 690}, false), QList<int>({200, 460, 330}));
        QCOMPARE(sizesAfterEdgeDock({300, 690}, true), QList<int>({330, 200, 460}));
        // A page with one region is wrapped, and half is that one equal share of two.
        QCOMPARE(sizesAfterEdgeDock({900}, true), QList<int>({450, 450}));
        // Rounding is absorbed by the widest pane, never by the total: 301 splits into a 100
        // share and a 201 remainder that keeps 100:201's proportions.
        QCOMPARE(sizesAfterEdgeDock({100, 201}, true), QList<int>({100, 67, 134}));
        // What cannot be divided says so, rather than sizing the pane to nothing.
        QVERIFY(sizesAfterEdgeDock({}, false).isEmpty());
        QVERIFY(sizesAfterEdgeDock({0, 0}, true).isEmpty());
        for (const QList<int> &sizes : {QList<int>{300, 690}, QList<int>{200, 500, 300}, QList<int>{100, 201}})
            for (bool atStart : {false, true}) {
                const QList<int> after = sizesAfterEdgeDock(sizes, atStart);
                QCOMPARE(after.size(), sizes.size() + 1);
                QCOMPARE(std::accumulate(after.cbegin(), after.cend(), 0),
                         std::accumulate(sizes.cbegin(), sizes.cend(), 0));
            }
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

    // ----- equalizing a whole page (owner report, 2026-09-19: pane sizes jiggle) ----------------

    void splittersInWalksNestedSplittersDepthFirst() {
        auto *outer = new QSplitter(Qt::Vertical);
        auto *inner = new QSplitter(Qt::Horizontal);
        inner->addWidget(new QLabel(QStringLiteral("A")));
        inner->addWidget(new QLabel(QStringLiteral("B")));
        outer->addWidget(inner);
        outer->addWidget(new QLabel(QStringLiteral("C")));
        std::unique_ptr<QSplitter> owner(outer);
        const auto splitters = splittersIn(outer);
        QCOMPARE(splitters.size(), 2);
        QCOMPARE(splitters.at(0).data(), outer);
        QCOMPARE(splitters.at(1).data(), inner);
        QCOMPARE(splittersIn(nullptr).size(), 0);
        // A plain widget with no splitter in it at all.
        QLabel lone(QStringLiteral("D"));
        QCOMPARE(splittersIn(&lone).size(), 0);
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

    // ----- "new pane, then an arrow places it" (issue #78BN) ----------------------------------

    void placementArrowsPlaceAndConsume() {
        PlacementWindow window;
        window.arm(0);
        QVERIFY(window.armed(0));
        const auto left = window.keyPress(Qt::Key_Left, Qt::NoModifier, 100, QString());
        QCOMPARE(int(left.action), int(PlacementWindow::Action::Place));
        QCOMPARE(int(left.direction), int(Direction::Left));
        // One arrow only: the window is closed afterwards.
        QVERIFY(!window.armed(150));
        QCOMPARE(int(window.keyPress(Qt::Key_Up, Qt::NoModifier, 150, QString()).action),
                 int(PlacementWindow::Action::None));

        for (const auto pair : {std::make_pair(int(Qt::Key_Up), Direction::Up),
                                std::make_pair(int(Qt::Key_Down), Direction::Down),
                                std::make_pair(int(Qt::Key_Right), Direction::Right)}) {
            window.arm(0);
            const auto response = window.keyPress(pair.first, Qt::NoModifier, 10, QString());
            QCOMPARE(int(response.action), int(PlacementWindow::Action::Place));
            QCOMPARE(int(response.direction), int(pair.second));
        }
    }

    void placementIgnoresModifiedArrowsAndOtherKeys() {
        PlacementWindow window;
        // Alt+Left still focuses the pane to the left.
        window.arm(0);
        const auto alt = window.keyPress(Qt::Key_Left, Qt::AltModifier, 10, QString());
        QCOMPARE(int(alt.action), int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(10));
        // A letter closes the window and is passed on, so it still reaches the prompt box.
        window.arm(0);
        QCOMPARE(int(window.keyPress(Qt::Key_A, Qt::NoModifier, 10, QString()).action),
                 int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(10));
    }

    // The hand that typed Ctrl+E has not let go yet (card #JXWT): Ctrl still held places the pane,
    // and Shift may ride along because Ctrl+Shift+E, the twin a program cannot swallow, leaves
    // both down — but only while the keymap leaves the chord free.
    void placementAcceptsArrowsWithCtrlHeld() {
        PlacementWindow window;
        window.arm(0);
        const auto down = window.keyPress(Qt::Key_Down, Qt::ControlModifier, 100, QString());
        QCOMPARE(int(down.action), int(PlacementWindow::Action::Place));
        QCOMPARE(int(down.direction), int(Direction::Down));
        QVERIFY(!window.armed(100));
        window.arm(0);
        const auto up = window.keyPress(Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier, 100, QString());
        QCOMPARE(int(up.action), int(PlacementWindow::Action::Place));
        QCOMPARE(int(up.direction), int(Direction::Up));
        // A chord the keymap binds keeps doing what it always does: the window closes and the key
        // is passed on. Konsole binds Ctrl+Shift+Down to focus-below, and a hand-bound Ctrl+Down
        // is that too.
        window.arm(0);
        QCOMPARE(int(window.keyPress(Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier, 10,
                                     QStringLiteral("pane.focusDown")).action),
                 int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(10));
        window.arm(0);
        QCOMPARE(int(window.keyPress(Qt::Key_Down, Qt::ControlModifier, 10,
                                     QStringLiteral("pane.focusDown")).action),
                 int(PlacementWindow::Action::Dismiss));
        // Alt or Meta with the arrow is never placement, bound or not: Ctrl+Alt+arrow moves a
        // pane, and Alt+arrow focuses one.
        window.arm(0);
        QCOMPARE(int(window.keyPress(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier, 10, QString()).action),
                 int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(10));
        window.arm(0);
        QCOMPARE(int(window.keyPress(Qt::Key_Left, Qt::ShiftModifier, 10, QString()).action),
                 int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(10));
    }

    void placementKeepsTheWindowOpenWhileAModifierIsHeld() {
        PlacementWindow window;
        window.arm(0);
        for (int key : {int(Qt::Key_Control), int(Qt::Key_Shift), int(Qt::Key_Alt), int(Qt::Key_Meta), int(Qt::Key_AltGr)}) {
            QCOMPARE(int(window.keyPress(key, Qt::NoModifier, 10, QString()).action),
                     int(PlacementWindow::Action::None));
            QVERIFY(window.armed(10));
        }
        // The arrow that follows still places the pane.
        QCOMPARE(int(window.keyPress(Qt::Key_Down, Qt::NoModifier, 20, QString()).action),
                 int(PlacementWindow::Action::Place));
    }

    void placementExpiresAfterTwoSeconds() {
        PlacementWindow window;
        window.arm(0);
        QVERIFY(window.armed(PlacementWindow::kTimeoutMs - 1));
        QVERIFY(!window.armed(PlacementWindow::kTimeoutMs));
        // An arrow typed after the window closed is passed on, not swallowed.
        const auto late = window.keyPress(Qt::Key_Left, Qt::NoModifier, PlacementWindow::kTimeoutMs + 500, QString());
        QCOMPARE(int(late.action), int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(PlacementWindow::kTimeoutMs + 500));
    }

    void placementClosesOnAClick() {
        PlacementWindow window;
        window.arm(0);
        QCOMPARE(int(window.mousePress(10).action), int(PlacementWindow::Action::Dismiss));
        QVERIFY(!window.armed(10));
        QCOMPARE(int(window.keyPress(Qt::Key_Left, Qt::NoModifier, 20, QString()).action),
                 int(PlacementWindow::Action::None));
        // A click with no window open is nothing at all.
        QCOMPARE(int(window.mousePress(30).action), int(PlacementWindow::Action::None));
    }

    void placementCancelClosesTheWindow() {
        PlacementWindow window;
        window.arm(0);
        window.cancel();
        QVERIFY(!window.armed(0));
        QCOMPARE(int(window.keyPress(Qt::Key_Left, Qt::NoModifier, 10, QString()).action),
                 int(PlacementWindow::Action::None));
    }

    // ----- "move left/right, then the Move-down key docks it beneath" (card #Q7Y9) ------------

    // The hint after a drag, and the chord itself, name the move that takes the pane TOWARD the
    // anchor. The first version named the side the pane came from, so a pane dropped beneath the
    // one on its left was taught "Move-right then Move-down", which moves it away.
    void chordNamesTheMoveTowardTheAnchor() {
        const QRect anchor(0, 0, 100, 200);
        const QRect onItsRight(100, 0, 100, 200);
        const QRect onItsLeft(-100, 0, 100, 200);
        QVERIFY(moveToward(onItsRight, anchor).has_value());
        QCOMPARE(int(*moveToward(onItsRight, anchor)), int(Direction::Left));
        QVERIFY(moveToward(onItsLeft, anchor).has_value());
        QCOMPARE(int(*moveToward(onItsLeft, anchor)), int(Direction::Right));
        // Already stacked, or overlapping: neither move describes it.
        QVERIFY(!moveToward(QRect(0, 200, 100, 200), anchor).has_value());
        QVERIFY(!moveToward(QRect(50, 0, 100, 200), anchor).has_value());
    }

    // The chord's window survives its own keys and nothing else.
    void chordWindowSurvivesOnlyItsOwnKeys() {
        for (int key : {int(Qt::Key_Control), int(Qt::Key_Shift), int(Qt::Key_Alt), int(Qt::Key_Meta), int(Qt::Key_AltGr)})
            QVERIFY(chordKeyKeepsWindow(key, QString()));
        for (const char *id : {"pane.moveLeft", "pane.moveRight", "pane.moveDown"})
            QVERIFY(chordKeyKeepsWindow(Qt::Key_Down, QString::fromLatin1(id)));
        // Shell keys with a modifier held are not "half a shortcut": they close the window, so a
        // Move-down typed much later is a plain move again.
        QVERIFY(!chordKeyKeepsWindow(Qt::Key_C, QString()));
        QVERIFY(!chordKeyKeepsWindow(Qt::Key_L, QString()));
        QVERIFY(!chordKeyKeepsWindow(Qt::Key_A, QString()));
        // Another shortcut closes it as well.
        QVERIFY(!chordKeyKeepsWindow(Qt::Key_T, QStringLiteral("tab.new")));
        QVERIFY(!chordKeyKeepsWindow(Qt::Key_Up, QStringLiteral("pane.moveUp")));
    }

    // ----- the pane header's give-way ladder (owner, 2026-09-19) ------------------------------

    // Everything is on and the header is exactly as wide as the row wants: nothing has given way.
    void aFullHeaderGivesNothingAway() {
        const HeaderWants w = everythingOn();
        const HeaderFit fit = headerFit(fullWidth(w), w);
        QCOMPARE(rung(w, fit), 0);
        QCOMPARE(fit.directory, w.directory);
        QCOMPARE(fit.title, w.title);
        QCOMPARE(fit.ssh, SshForm::UserAndHost);
        QCOMPARE(fit.sshPx, w.ssh);
        QCOMPARE(fit.usage, UsageForm::CpuAndMemory);
        QCOMPARE(fit.usagePx, w.usage);
        QCOMPARE(fit.shortfall, 0);
        QVERIFY(fits(w, fit, fullWidth(w)));
    }

    // Rung 1: the directory is the first to give, eliding from the left to its floor and then going
    // altogether, while the title is still whole.
    void theDirectoryGivesWayFirst() {
        const HeaderWants w = everythingOn();
        const HeaderFit elided = headerFit(fullWidth(w) - 100, w);
        QCOMPARE(rung(w, elided), 1);
        QCOMPARE(elided.directory, w.directory - 100);
        QCOMPARE(elided.title, w.title);
        QVERIFY(elided.directory >= kDirectoryFloorPx);

        // One pixel under the floor is no path at all, never a stub.
        const int floorAt = fullWidth(w) - (w.directory - kDirectoryFloorPx);
        QCOMPARE(headerFit(floorAt, w).directory, kDirectoryFloorPx);
        QCOMPARE(headerFit(floorAt - 1, w).directory, 0);
        QCOMPARE(headerFit(floorAt - 1, w).title, w.title);   // the title has not started yet
    }

    // Rung 2: with the directory gone the title elides, down to its floor and no further.
    void thenTheTitleElidesToItsFloor() {
        const HeaderWants w = everythingOn();
        // The width at which the title has exactly its full text and nothing to spare.
        const int titleFull = fullWidth(w) - w.directory;
        QCOMPARE(headerFit(titleFull, w).title, w.title);
        QCOMPARE(headerFit(titleFull, w).directory, 0);
        QCOMPARE(headerFit(titleFull - 40, w).title, w.title - 40);
        QCOMPARE(rung(w, headerFit(titleFull - 40, w)), 3);
        // The floor, and then the ssh chip is what gives instead — the state word stood between
        // the two until card #0STR took it off the header.
        const int titleAtFloor = titleFull - (w.title - kTitleFloorPx);
        QCOMPARE(headerFit(titleAtFloor, w).title, kTitleFloorPx);
        QCOMPARE(headerFit(titleAtFloor, w).sshPx, w.ssh);
        QCOMPARE(headerFit(titleAtFloor - 1, w).title, kTitleFloorPx);
        QVERIFY(headerFit(titleAtFloor - 1, w).sshPx < w.ssh);
    }

    // Rung 3: the ssh chip is squeezed to its 150 px floor, then drops the `user@` and shows the
    // host alone below that floor, then elides the host itself.
    void thenTheSshChipKeepsOnlyTheHost() {
        const HeaderWants w = everythingOn();
        const int sshFull = fullWidth(w) - w.directory - (w.title - kTitleFloorPx);
        QCOMPARE(headerFit(sshFull, w).sshPx, w.ssh);
        QCOMPARE(headerFit(sshFull, w).ssh, SshForm::UserAndHost);
        // Squeezed, still user@host: the chip elides its own text into what it is given.
        const HeaderFit squeezed = headerFit(sshFull - 10, w);
        QCOMPARE(squeezed.ssh, SshForm::UserAndHost);
        QCOMPARE(squeezed.sshPx, w.ssh - 10);
        // At the floor it drops the user. Below the host's own width the host elides, and the chip
        // never goes under the width of the glyph and one ellipsis.
        const HeaderFit host = headerFit(sshFull - (w.ssh - kSshFloorPx) - 1, w);
        QCOMPARE(host.ssh, SshForm::HostOnly);
        QCOMPARE(host.sshPx, kSshFloorPx - 1);
        const HeaderFit tight = headerFit(sshFull - (w.ssh - w.sshEllipsis), w);
        QCOMPARE(tight.ssh, SshForm::HostOnly);
        QCOMPARE(tight.sshPx, w.sshEllipsis);
        QVERIFY(headerFit(200, w).sshPx >= w.sshEllipsis);
    }

    // Rung 4, the last one: the usage chip collapses to CPU alone, and it happens exactly when the
    // ssh chip has reached its floor — not before, since the meter outlives the host.
    void andLastTheUsageChipKeepsOnlyTheCpu() {
        const HeaderWants w = everythingOn();
        int collapsed = 0;
        for (int width = fullWidth(w); width >= 100; --width)
            if (headerFit(width, w).usage == UsageForm::CpuOnly) { collapsed = width; break; }
        QVERIFY2(collapsed > 0, "the usage chip never collapsed, however narrow the header got");
        QCOMPARE(headerFit(collapsed, w).usagePx, w.usageCpu);
        QCOMPARE(headerFit(collapsed + 1, w).usage, UsageForm::CpuAndMemory);
        // The rung below it has been taken to the last pixel, and the badge and the glyph have not.
        QCOMPARE(headerFit(collapsed, w).sshPx, w.sshEllipsis);
        QCOMPARE(headerFit(collapsed, w).title, kTitleFloorPx);
        QCOMPARE(headerFit(collapsed, w).shortfall, 0);
        // Past the last rung the header is simply too narrow, and it says so rather than taking
        // the badge apart.
        QVERIFY(headerFit(collapsed - 40, w).shortfall > 0);
        QCOMPARE(headerFit(collapsed - 40, w).title, kTitleFloorPx);
        QCOMPARE(headerFit(collapsed - 40, w).sshPx, w.sshEllipsis);
    }

    // Every rung is reached, in this order and no other, as the pane narrows.
    void theRungsComeInTheDecidedOrder() {
        const HeaderWants w = everythingOn();
        QList<int> seen;
        for (int width = fullWidth(w) + 60; width >= 200; --width) {
            const int step = rung(w, headerFit(width, w));
            if (seen.isEmpty() || seen.last() != step) {
                QVERIFY2(seen.isEmpty() || step == seen.last() + 1,
                         qPrintable(QStringLiteral("at %1 px the ladder jumped from rung %2 to rung %3")
                                        .arg(width).arg(seen.isEmpty() ? -1 : seen.last()).arg(step)));
                seen.append(step);
            }
        }
        QCOMPARE(seen, QList<int>({0, 1, 2, 3, 4, 5, 6, 7}));
    }

    // Widening gives the elements back in the reverse order: the fit at a width is the same whether
    // the pane has just been narrowed to it or just widened to it, because it is a function of the
    // width alone. Nothing here can oscillate: applying a fit changes what the chips ask for, the
    // row is laid out again and this function is asked again — and it must answer the same thing.
    void theLadderIsAFunctionOfTheWidthAlone() {
        const HeaderWants w = everythingOn();
        QMap<int, QString> narrowing, widening;
        for (int width = fullWidth(w) + 60; width >= 200; --width) narrowing.insert(width, describe(headerFit(width, w)));
        for (int width = 200; width <= fullWidth(w) + 60; ++width) widening.insert(width, describe(headerFit(width, w)));
        QCOMPARE(narrowing, widening);
        // And nothing an element shows ever shrinks as the header grows.
        HeaderFit last = headerFit(200, w);
        for (int width = 200; width <= fullWidth(w) + 60; ++width) {
            const HeaderFit fit = headerFit(width, w);
            QVERIFY2(rung(w, fit) <= rung(w, last), qPrintable(QStringLiteral("rung went up at %1 px").arg(width)));
            QVERIFY(fit.title >= last.title);
            QVERIFY(fit.directory >= last.directory);
            QVERIFY(fit.sshPx >= last.sshPx);
            QVERIFY(fit.usagePx >= last.usagePx);
            QVERIFY2(fits(w, fit, width) || fit.shortfall > 0,
                     qPrintable(QStringLiteral("the row does not fit in %1 px").arg(width)));
            last = fit;
        }
    }

    // A chip that is not there takes no room and is never asked to give way; the ladder is the same
    // ladder with one rung missing.
    void anAbsentChipCostsNothing() {
        HeaderWants w = everythingOn();
        w.ssh = w.sshHost = w.sshEllipsis = 0;           // a local pane
        for (int width : {1200, 800, 600, 420, 300}) {
            const HeaderFit fit = headerFit(width, w);
            QCOMPARE(fit.sshPx, 0);
            QVERIFY(fits(w, fit, width) || fit.shortfall > 0);
        }
        // Without the chip the title and the directory keep their room for longer.
        QVERIFY(headerFit(700, w).directory > headerFit(700, everythingOn()).directory);

        HeaderWants quiet = everythingOn();
        quiet.usage = quiet.usageCpu = 0;                // a pane costing nothing worth reading
        for (int width : {1200, 800, 600, 420, 300}) QCOMPARE(headerFit(width, quiet).usagePx, 0);
        // The badge and the glyph never give way, so their room is simply gone from the ladder.
        HeaderWants noBadge = everythingOn();
        noBadge.badge = 0;
        QVERIFY(headerFit(800, noBadge).directory > headerFit(800, everythingOn()).directory);
    }

    // A title shorter than its floor asks for what it is: the floor is a limit on eliding, not a
    // reservation, and the directory has the rest.
    void aShortTitleDoesNotHoardItsFloor() {
        HeaderWants w = everythingOn();
        w.title = 30;
        const HeaderFit fit = headerFit(fullWidth(w), w);
        QCOMPARE(fit.title, 30);
        QCOMPARE(fit.directory, w.directory);
        QVERIFY(fits(w, fit, fullWidth(w)));
    }
};

QTEST_MAIN(PaneLayoutTests)
#include "panelayout_test.moc"
