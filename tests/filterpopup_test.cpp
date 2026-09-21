// SPDX-License-Identifier: AGPL-3.0-or-later
// The list Alt+M and Alt+E drop open (owner, 2026-09-21: "when you use alt+m or alt+e, your
// current selection should be highlighted. then you should be able to select with up/down arrows,
// and also filter with text typing (like warp's model picker)").
//
// What is asserted here is what went wrong in the box this replaces (src/FilterPopup.h): the
// current row must *be* the highlighted row when the list opens, and the highlight must be a
// visible band rather than a hairline nobody can see — so one case renders the popup and measures
// the pixels. The rest is the keyboard: Up/Down step over separators and switched-off rows, typing
// filters and lands the highlight on the first match, Enter gives back the row's index in the
// list as it was handed in (not the filtered position), and Escape gives back nothing.
#include "FilterPopup.h"
#include "Theme.h"

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QLayout>
#include <QTest>

using namespace relay;

namespace {

// A model box's shape: two role rows, a separator, two catalog rows, one switched off (a preset
// whose key is gone), a separator, and the two action rows that are not models at all.
QList<FilterRow> modelRows()
{
    QList<FilterRow> rows;
    rows << FilterRow{QStringLiteral("glm-5.3 (main)"), QStringLiteral("role:main"), {}, false, true};
    rows << FilterRow{QStringLiteral("glm-5.3 flash (flash)"), QStringLiteral("role:flash"), {}, false, true};
    rows << FilterRow{{}, {}, {}, true, false};                                                    // 2: separator
    rows << FilterRow{QStringLiteral("kimi-k2"), QStringLiteral("entry:kimi|kimi-k2"), {}, false, true};
    rows << FilterRow{QStringLiteral("kimi-k2 turbo"), QStringLiteral("entry:kimi|turbo"), {}, false, false};  // 4: off
    rows << FilterRow{{}, {}, {}, true, false};                                                    // 5: separator
    rows << FilterRow{QStringLiteral("more models…"), QStringLiteral("gear:picker"), {}, false, true};
    rows << FilterRow{QStringLiteral("⚙  customize…"), QStringLiteral("gear:modelOptions"), {}, false, true};
    return rows;
}

// The model box as design 5.3 draws it: a header per class, that class's top models indented
// under it, then a separator and "more models…". `expandMain` is the same list with the main class
// opened to its whole length, which is what the caller hands back from onExpandKey.
QList<FilterRow> classRows(bool expandMain = false)
{
    const auto head = [](const QString &klass, const QString &mark) {
        return FilterRow{klass, QStringLiteral("class:") + klass, {}, false, false, mark, klass, true};
    };
    const auto model = [](const QString &klass, const QString &name, const QString &via) {
        return FilterRow{name, QStringLiteral("pick:%1|%2").arg(klass, name), {}, false, true, via, klass, false};
    };
    QList<FilterRow> rows;
    rows << head(QStringLiteral("high"), QString());
    rows << model(QStringLiteral("high"), QStringLiteral("gpt-6-astra"), QStringLiteral("codex"));
    rows << head(QStringLiteral("main"), expandMain ? QStringLiteral("\u2304") : QStringLiteral("\u203a"));
    rows << model(QStringLiteral("main"), QStringLiteral("kimi-k3"), QStringLiteral("kimi"));
    rows << model(QStringLiteral("main"), QStringLiteral("glm-5.3"), QStringLiteral("z.ai +1"));
    if (expandMain) rows << model(QStringLiteral("main"), QStringLiteral("claude-opus-5"), QStringLiteral("anthropic"));
    rows << head(QStringLiteral("flash"), QString());
    rows << model(QStringLiteral("flash"), QStringLiteral("glm-5.3-flash"), QStringLiteral("z.ai"));
    rows << FilterRow{{}, {}, {}, true, false, {}, {}, false};
    rows << FilterRow{QStringLiteral("more models…"), QStringLiteral("gear:picker"), {}, false, true};
    return rows;
}

// Where kimi-k3 sits in that list, collapsed: the row the box opens highlighted.
constexpr int kMainModelRow = 3;

bool hasData(const QList<FilterRow> &rows, const QString &data)
{
    for (const FilterRow &row : rows) if (row.data == data) return true;
    return false;
}

}  // namespace

class FilterPopupTest : public QObject {
    Q_OBJECT

private slots:
    // 1. The row the caller says is current is the highlighted row the moment the list opens.
    //    This is the whole complaint: the box used to open with nothing highlighted at all.
    void currentRowIsHighlightedOnOpen()
    {
        FilterPopup popup;
        popup.setRows(modelRows(), 3);
        QCOMPARE(popup.currentRow(), 3);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("entry:kimi|kimi-k2"));
        // Re-setting the rows with another current row moves the highlight with it.
        popup.setRows(modelRows(), 1);
        QCOMPARE(popup.currentRow(), 1);
        // A separator or a switched-off row can never be the current one.
        popup.setRows(modelRows(), 2);
        QCOMPARE(popup.currentRow(), 0);
        popup.setRows(modelRows(), 4);
        QCOMPARE(popup.currentRow(), 0);
    }

    // 2. And the highlight is *visible*: a band in the accent, not the 3%-of-a-shade hairline
    //    Fusion drew on the combo box's own list (src/FilterPopup.h). Rendered and measured, so
    //    that a future change of delegate cannot quietly bring the invisible one back.
    void theHighlightIsAVisibleBand()
    {
        theme::SurfaceRaised = QColor(0x24, 0x1c, 0x18);   // Dark Copper, the default theme
        theme::Accent = QColor(0xc0, 0x7a, 0x4a);
        QWidget anchor;
        anchor.resize(320, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        popup.setRows(modelRows(), 0);
        popup.openFor(&anchor);
        const QImage shot = popup.grab().toImage();
        popup.dismiss();
        // The first row sits under the filter line; the second is the one below it. Their left
        // edges differ only by the highlight, so a flat difference means nothing was drawn.
        int lit = 0;
        int plain = 0;
        for (int y = 0; y < shot.height(); ++y) {
            const QColor pixel = shot.pixelColor(shot.width() - 10, y);
            const int distance = std::abs(pixel.red() - theme::SurfaceRaised.red())
                + std::abs(pixel.green() - theme::SurfaceRaised.green())
                + std::abs(pixel.blue() - theme::SurfaceRaised.blue());
            if (distance > 40) ++lit; else ++plain;
        }
        QVERIFY2(lit >= 8, qPrintable(QStringLiteral("the highlighted row covered %1 rows of pixels").arg(lit)));
        QVERIFY(plain > lit);   // one row is highlighted, not the whole list
    }

    // 3. Up and Down step over the separators and the switched-off row, and stop at the ends.
    void arrowsSkipSeparatorsAndDisabledRows()
    {
        FilterPopup popup;
        popup.setRows(modelRows(), 0);
        QCOMPARE(popup.currentRow(), 0);
        popup.moveCurrent(1);
        QCOMPARE(popup.currentRow(), 1);
        popup.moveCurrent(1);
        QCOMPARE(popup.currentRow(), 3);   // 2 is a separator
        popup.moveCurrent(1);
        QCOMPARE(popup.currentRow(), 6);   // 4 is switched off, 5 is a separator
        popup.moveCurrent(1);
        QCOMPARE(popup.currentRow(), 7);
        popup.moveCurrent(1);
        QCOMPARE(popup.currentRow(), 7);   // the end holds
        popup.moveCurrent(-1);
        QCOMPARE(popup.currentRow(), 6);
        popup.moveCurrent(-1);
        QCOMPARE(popup.currentRow(), 3);
        popup.moveCurrent(-1);
        QCOMPARE(popup.currentRow(), 1);
        popup.moveCurrent(-99);
        QCOMPARE(popup.currentRow(), 0);   // Home, and the start holds
    }

    // 4. Typing narrows the list and puts the highlight on the first match — the rows that are
    //    not models stay reachable by their own words, which is how "more models…" is picked.
    void typingFiltersAndHighlightsTheFirstMatch()
    {
        FilterPopup popup;
        popup.setRows(modelRows(), 0);
        QCOMPARE(popup.visibleCount(), 5);   // eight rows less two separators and the switched-off one

        popup.setFilterText(QStringLiteral("kimi"));
        QCOMPARE(popup.filterText(), QStringLiteral("kimi"));
        QCOMPARE(popup.visibleCount(), 1);   // the turbo row is switched off, so it does not count
        QCOMPARE(popup.currentRow(), 3);

        popup.setFilterText(QStringLiteral("flash"));
        QCOMPARE(popup.currentRow(), 1);
        QCOMPARE(popup.visibleCount(), 1);

        popup.setFilterText(QStringLiteral("MORE"));   // case-insensitive, and an action row
        QCOMPARE(popup.currentRow(), 6);

        popup.setFilterText(QStringLiteral("gl53"));   // fuzzy, the way the palette matches
        QCOMPARE(popup.currentRow(), 0);

        popup.setFilterText(QString());                // cleared: every row is back
        QCOMPARE(popup.visibleCount(), 5);
    }

    // 5. Nothing matches: a quiet "no match" line rather than an empty box, and nothing to pick.
    void noMatchSaysSoAndPicksNothing()
    {
        FilterPopup popup;
        popup.setRows(modelRows(), 0);
        popup.setFilterText(QStringLiteral("zzzzq"));
        QCOMPARE(popup.visibleCount(), 0);
        QVERIFY(!popup.hasMatches());
        QCOMPARE(popup.currentRow(), -1);
        int picked = -2;
        popup.onPicked = [&picked](int row) { picked = row; };
        QCOMPARE(popup.activate(), -1);
        QCOMPARE(picked, -2);   // Enter on "no match" does nothing at all
    }

    // 6. Enter answers with the row's index in the list as it was handed in, not its position
    //    among the rows the filter left — the caller looks its own `data` up by that index.
    void enterReturnsTheOriginalRowIndex()
    {
        FilterPopup popup;
        popup.setRows(modelRows(), 0);
        popup.setFilterText(QStringLiteral("customize"));
        QCOMPARE(popup.visibleCount(), 1);
        int picked = -2;
        popup.onPicked = [&picked](int row) { picked = row; };
        QCOMPARE(popup.activate(), 7);
        QCOMPARE(picked, 7);
        QCOMPARE(popup.rows().at(picked).data, QStringLiteral("gear:modelOptions"));
    }

    // 7. Escape closes with nothing picked, and re-opening starts from an empty filter with the
    //    current row highlighted again.
    void escapeAnswersWithNothingAndClearsTheFilter()
    {
        QWidget anchor;
        anchor.resize(120, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        popup.setRows(modelRows(), 1);
        int picked = -2;
        bool cancelled = false;
        popup.onPicked = [&picked](int row) { picked = row; };
        popup.onCancelled = [&cancelled] { cancelled = true; };

        popup.openFor(&anchor);
        QVERIFY(popup.isVisible());
        QCOMPARE(popup.currentRow(), 1);
        popup.setFilterText(QStringLiteral("kimi"));
        QCOMPARE(popup.currentRow(), 3);
        popup.dismiss();
        QVERIFY(!popup.isVisible());
        QVERIFY(cancelled);
        QCOMPARE(picked, -2);

        popup.openFor(&anchor);
        QCOMPARE(popup.filterText(), QString());
        QCOMPARE(popup.visibleCount(), 5);
        QCOMPARE(popup.currentRow(), 3);   // the row Escape left highlighted, not a reset to the top
        popup.dismiss();
    }

    // 8. A list short enough to draw whole never shows a scrollbar — the four reasoning levels,
    //    and the two a model with only high and max offers, which is what Alt+E opened over a
    //    scrollbar before. Long lists still scroll, and then the bar's width is added to the box
    //    rather than taken out of the rows.
    void aListThatFitsDoesNotScroll()
    {
        QWidget anchor;
        anchor.resize(70, 22);
        anchor.show();
        QList<FilterRow> levels;
        for (const QString &name : {QStringLiteral("high"), QStringLiteral("max")})
            levels << FilterRow{name, name, {}, false, true};
        FilterPopup popup(&anchor);
        popup.setRows(levels, 0);
        popup.openFor(&anchor);
        QCOMPARE(popup.visibleCount(), 2);
        QVERIFY2(!popup.scrolling(), "two rows must not need a scrollbar");
        QVERIFY(popup.rowVisible(0));
        QVERIFY(popup.rowVisible(1));
        // Exactly the room the filter line and the rows ask for: taller and the box has a gap
        // under the last row, shorter and it cuts through it.
        QCOMPARE(popup.height(), popup.layout()->totalSizeHint().height());
        const int twoRows = popup.height();

        // Filtering down to one row keeps it that way, and the box shrinks with the list.
        popup.setFilterText(QStringLiteral("max"));
        QVERIFY(!popup.scrolling());
        QCOMPARE(popup.height(), popup.layout()->totalSizeHint().height());
        QVERIFY(popup.height() < twoRows);
        popup.setFilterText(QString());
        QCOMPARE(popup.height(), twoRows);

        // Forty rows is past the cap, so that one does scroll — and is still no taller than the
        // room it was given.
        QList<FilterRow> many;
        for (int i = 0; i < 40; ++i)
            many << FilterRow{QStringLiteral("model-%1").arg(i), QStringLiteral("entry:%1").arg(i), {}, false, true};
        popup.setRows(many, 0);
        popup.openFor(&anchor);
        QVERIFY2(popup.scrolling(), "forty rows must scroll");
        QCOMPARE(popup.currentRow(), 0);
        QVERIFY(popup.rowVisible(0));
        popup.dismiss();
    }

    // 10. The three levels kimi offers, with the middle one current. The list used to be sized for
    //     two and scrolled down to reach "high"; when the sizing was fixed it kept the offset, so
    //     it drew "high", "max" and a row of empty ground with "low" off the top. Every row of a
    //     list that fits is drawn, wherever the current one is.
    void agrownListIsNotLeftScrolled()
    {
        QWidget anchor;
        anchor.resize(70, 22);
        anchor.show();
        QList<FilterRow> levels;
        for (const QString &name : {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")})
            levels << FilterRow{name, name, {}, false, true};
        FilterPopup popup(&anchor);
        popup.setRows(levels, 1);
        popup.openFor(&anchor);
        QCOMPARE(popup.currentRow(), 1);
        QVERIFY(!popup.scrolling());
        for (int row = 0; row < 3; ++row)
            QVERIFY2(popup.rowVisible(row), qPrintable(QStringLiteral("row %1 is not drawn").arg(row)));
        popup.dismiss();
    }

    // 9. The box it hangs from is at the right-hand end of the composer strip, so the list has to
    //    be right-aligned to it rather than run off the window. It never leaves the window, and it
    //    is never narrower than the box.
    void theListStaysInsideTheWindow()
    {
        QWidget window;
        window.resize(500, 300);
        auto *anchor = new QWidget(&window);
        anchor->setGeometry(430, 270, 66, 22);   // the far right of the strip
        window.move(0, 0);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QList<FilterRow> rows;
        for (const QString &name : {QStringLiteral("glm-5.3 flash · z.ai (glm)"),
                                    QStringLiteral("kimi-for-coding-highspeed · kimi")})
            rows << FilterRow{name, name, {}, false, true};
        FilterPopup popup(anchor);
        popup.setRows(rows, 0);
        popup.openFor(anchor);

        const QRect box = popup.geometry();
        const QRect frame = window.frameGeometry();
        QVERIFY2(box.right() <= frame.right(),
                 qPrintable(QStringLiteral("popup right %1 past window right %2").arg(box.right()).arg(frame.right())));
        QVERIFY(box.left() >= frame.left());
        QVERIFY(box.width() >= anchor->width());
        // Right-aligned to the box, not left-aligned off the edge.
        QVERIFY(box.left() < anchor->mapToGlobal(QPoint(0, 0)).x());
        popup.dismiss();
    }

    // 11. The short list Alt+E opens is the same popup: four rows, no separators, and the pane's
    //    level highlighted. It has no sections, so Left and Right are not the popup's at all —
    //    they stay the filter line's caret keys, and nothing about this box changed.
    void theEffortListIsTheSameControl()
    {
        QList<FilterRow> levels;
        for (const QString &name : {QStringLiteral("minimal"), QStringLiteral("low"),
                                    QStringLiteral("high"), QStringLiteral("max")})
            levels << FilterRow{name, name, {}, false, true};
        FilterPopup popup;
        int asked = 0;
        popup.onExpandKey = [&asked](const QString &, int) { ++asked; return true; };
        popup.setRows(levels, 2);
        QCOMPARE(popup.currentRow(), 2);
        QVERIFY(!popup.expandCurrent(1));   // no group on the row: never offered to the caller
        QCOMPARE(asked, 0);
        QCOMPARE(popup.currentRow(), 2);
        popup.setFilterText(QStringLiteral("ma"));
        QCOMPARE(popup.visibleCount(), 2);        // "max", and "minimal" fuzzily
        QCOMPARE(popup.currentRow(), 0);
        popup.setFilterText(QStringLiteral("max"));
        QCOMPARE(popup.visibleCount(), 1);
        QCOMPARE(popup.activate(), 3);
    }

    // ----- classes: headers are labels, Right opens one (card #MDL1, design 5.3) -----------

    // 12. Up and Down never land on a header (owner: "the class header rows are not selectable in
    //     the picker. thats redundant."), and the list opens on the row the caller named.
    void upAndDownStepOverTheClassHeaders()
    {
        FilterPopup popup;
        popup.setRows(classRows(), kMainModelRow);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|kimi-k3"));
        QCOMPARE(popup.visibleCount(), 5);   // four models and "more models…"; the headers are not rows to land on

        // Up from kimi-k3 skips the "main" header and lands on the high class's model.
        popup.moveCurrent(-1);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:high|gpt-6-astra"));
        popup.moveCurrent(-1);               // nothing above it: the header is not a stop
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:high|gpt-6-astra"));

        // And all the way down, every stop is a model or the action row — never a header.
        QStringList walked;
        popup.moveCurrent(-99);
        for (int i = 0; i < 8; ++i) {
            walked << popup.rows().at(popup.currentRow()).data;
            popup.moveCurrent(1);
        }
        for (const QString &data : walked) QVERIFY2(!data.startsWith(QStringLiteral("class:")), qPrintable(data));
        QCOMPARE(walked.last(), QStringLiteral("gear:picker"));
        // The caller cannot force one either: a header handed in as the current row is refused.
        popup.setRows(classRows(), 2);
        QVERIFY(popup.currentRow() != 2);
    }

    // 13. Right opens the highlighted row's class, Left closes it. The popup asks the caller, the
    //     caller hands back a new list, and the highlight goes back on the row it was on.
    void rightExpandsAClassAndLeftCollapsesIt()
    {
        QWidget anchor;
        anchor.resize(160, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        QStringList asked;
        bool expanded = false;
        popup.onExpandKey = [&](const QString &group, int delta) {
            asked << (delta > 0 ? QStringLiteral("+") : QStringLiteral("-")) + group;
            if (group != QStringLiteral("main")) return false;
            if ((delta > 0) == expanded) return false;      // already where it would go
            expanded = delta > 0;
            popup.setRows(classRows(expanded), -1);
            return true;
        };
        popup.setRows(classRows(), kMainModelRow);
        popup.openFor(&anchor);

        popup.expandCurrent(1);
        QCOMPARE(asked, QStringList({QStringLiteral("+main")}));
        QVERIFY(popup.isVisible());                          // nothing closed, nothing was picked
        QCOMPARE(popup.visibleCount(), 6);                   // the third main model joined the list
        QVERIFY(popup.rows().at(5).data.endsWith(QStringLiteral("claude-opus-5")));
        // The highlight is still on the row it was on, found by its data rather than its index.
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|kimi-k3"));
        // And the header now says so the other way round.
        QCOMPARE(popup.rows().at(2).trailing, QStringLiteral("⌄"));

        popup.expandCurrent(-1);
        QCOMPARE(popup.visibleCount(), 5);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|kimi-k3"));
        QCOMPARE(popup.rows().at(2).trailing, QStringLiteral("›"));

        // Left again: the caller says "nothing changed" and the list is left exactly as it is.
        const int before = popup.visibleCount();
        QVERIFY(!popup.expandCurrent(-1));
        QCOMPARE(popup.visibleCount(), before);
        QCOMPARE(asked, QStringList({QStringLiteral("+main"), QStringLiteral("-main"), QStringLiteral("-main")}));
        popup.dismiss();
    }

    // 14. Typing filters across every class, and a header stays exactly as long as its own class
    //     still has a match (design 5.3).
    void aFilterKeepsTheHeadersWhoseClassStillMatches()
    {
        FilterPopup popup;
        popup.setRows(classRows(), kMainModelRow);
        popup.setFilterText(QStringLiteral("glm"));
        // glm-5.3 under main and glm-5.3-flash under flash, with both of their headers — and
        // nothing of the high class, whose one model does not match.
        QCOMPARE(popup.visibleCount(), 2);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|glm-5.3"));
        QVERIFY(popup.rowShown(2));    // the main header
        QVERIFY(popup.rowShown(5));    // the flash header
        QVERIFY(!popup.rowShown(0));   // high has no match: its header goes with it

        // A header is never kept on its own words: typing a class's name finds its models, not it.
        popup.setFilterText(QStringLiteral("flash"));
        QCOMPARE(popup.visibleCount(), 1);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:flash|glm-5.3-flash"));
        QVERIFY(popup.rowShown(5));
        QVERIFY(!popup.rowShown(2));

        popup.setFilterText(QStringLiteral("zzz"));
        QCOMPARE(popup.visibleCount(), 0);
        QCOMPARE(popup.currentRow(), -1);

        popup.setFilterText(QString());
        QCOMPARE(popup.visibleCount(), 5);
        QVERIFY(popup.rowShown(0));
    }

    // 15. Enter answers with the row's own data — the class and the model together — and Escape,
    //     after any number of expansions, picks nothing.
    void enterAnswersWithTheRowsOwnDataAndEscapePicksNothing()
    {
        QWidget anchor;
        anchor.resize(160, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        QStringList picked;
        popup.onPicked = [&popup, &picked](int row) { picked << popup.rows().at(row).data; };
        bool expanded = false;
        popup.onExpandKey = [&](const QString &group, int delta) {
            if (group != QStringLiteral("main") || (delta > 0) == expanded) return false;
            expanded = delta > 0;
            popup.setRows(classRows(expanded), -1);
            return true;
        };
        popup.setRows(classRows(), kMainModelRow);
        popup.openFor(&anchor);
        popup.moveCurrent(1);
        popup.activate();
        QCOMPARE(picked, QStringList({QStringLiteral("pick:main|glm-5.3")}));

        // A row that only the expansion put there is picked the same way.
        popup.setRows(classRows(), kMainModelRow);
        popup.openFor(&anchor);
        popup.expandCurrent(1);
        popup.moveCurrent(2);
        popup.activate();
        QCOMPARE(picked.last(), QStringLiteral("pick:main|claude-opus-5"));

        bool cancelled = false;
        popup.onCancelled = [&cancelled] { cancelled = true; };
        expanded = false;
        popup.setRows(classRows(), kMainModelRow);
        popup.openFor(&anchor);
        popup.expandCurrent(1);
        popup.expandCurrent(-1);
        popup.dismiss();
        QVERIFY(cancelled);
        QCOMPARE(picked.size(), 2);                           // still only the two
    }

    // 16. The "via" column is drawn at the far end of the row, in the muted ink, and the row is
    //     wide enough for both. Rendered and measured: the column is the only thing that says
    //     which provider a one-row-per-model list would actually spend (card #MDL1, rule 2).
    void theViaColumnIsDrawnAtTheEndOfTheRow()
    {
        theme::SurfaceRaised = QColor(0x24, 0x1c, 0x18);
        theme::Accent = QColor(0xc0, 0x7a, 0x4a);
        theme::Text = QColor(0xf0, 0xe6, 0xdd);
        theme::TextMuted = QColor(0x9a, 0x8b, 0x7f);
        QWidget anchor;
        anchor.resize(120, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        QList<FilterRow> rows;
        rows << FilterRow{QStringLiteral("kimi-k3"), QStringLiteral("a"), {}, false, true,
                          QStringLiteral("kimi")};
        rows << FilterRow{QStringLiteral("kimi-k3"), QStringLiteral("b"), {}, false, true, QString()};
        popup.setRows(rows, 0);
        popup.openFor(&anchor);
        const QImage shot = popup.grab().toImage();
        popup.dismiss();
        // Ink in the right-hand third of the first row and none in the same band of the second.
        const auto inkInBand = [&shot](int fromY, int toY) {
            int ink = 0;
            for (int y = std::max(0, fromY); y < std::min(shot.height(), toY); ++y)
                for (int x = shot.width() * 2 / 3; x < shot.width() - 6; ++x) {
                    const QColor pixel = shot.pixelColor(x, y);
                    if (std::abs(pixel.red() - theme::SurfaceRaised.red())
                            + std::abs(pixel.green() - theme::SurfaceRaised.green())
                            + std::abs(pixel.blue() - theme::SurfaceRaised.blue()) > 60) ++ink;
                }
            return ink;
        };
        const int top = shot.height() - 2 * 23;
        QVERIFY2(inkInBand(top, top + 23) > 0, "nothing was drawn in the via column");
        QCOMPARE(inkInBand(top + 23, shot.height()), 0);
        // And the box is wide enough that the two never overlap.
        QVERIFY(popup.width() > 120);
    }

    // 17. The class indent. A header is drawn at the left margin and the models under it are
    //     stepped in, so the picture on the screen is the picture in the design. Rendered and
    //     measured, and measured as a *difference* between two renders of the same three words,
    //     one of them under a header, so a glyph's own shape (an "f" leans right at the top)
    //     stays out of the measurement. The header itself is drawn in the muted ink and so is not
    //     counted: what is asserted is where the *models* land.
    // ----- onQueryRows: the filter searches a wider list than the box shows ------------------
    // Owner, 2026-09-21: "the text filter isnt working -- its supposed to show all available
    // models, not just the ones selected for the box picker" (card #MDL1, design 5.7). The list
    // drops open with the classes down to their cutoff; the moment something is typed the caller
    // hands over the wider list, and the popup filters it exactly as it filters its own rows.
    void aTypedFilterSearchesTheWiderListTheCallerHandsBack()
    {
        FilterPopup popup;
        popup.setRows(classRows(), kMainModelRow);
        int asked = 0;
        QString sawQuery;
        popup.onQueryRows = [&](const QString &query) {
            ++asked;
            sawQuery = query;
            QList<FilterRow> wider = classRows(true);   // main opened to its whole list
            wider.insert(wider.size() - 2, FilterRow{QStringLiteral("other models"), QStringLiteral("class:other"),
                                                     {}, false, false, {}, QStringLiteral("other"), true});
            wider.insert(wider.size() - 2, FilterRow{QStringLiteral("deepseek-v4.1-flash"),
                                                     QStringLiteral("pick:main|openrouter|deepseek/deepseek-v4.1-flash"),
                                                     {}, false, true, QStringLiteral("openrouter"),
                                                     QStringLiteral("other"), false});
            return wider;
        };
        // Nothing typed: nothing is asked and the list is the one it was given.
        QCOMPARE(asked, 0);
        QCOMPARE(popup.rows().size(), classRows().size());
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|kimi-k3"));

        popup.setFilterText(QStringLiteral("deepseek"));
        QVERIFY(asked > 0);
        QCOMPARE(sawQuery, QStringLiteral("deepseek"));
        // A model that was in none of the box's own rows is reachable — it is the one selectable
        // row the filter left — and its header survives with it (a header is never selectable, so
        // `visibleCount` does not count it).
        QCOMPARE(popup.visibleCount(), 1);
        for (int i = 0; i < popup.rows().size(); ++i)
            if (popup.rows().at(i).data == QStringLiteral("class:other")) QVERIFY(popup.rowShown(i));
        const int at = popup.currentRow();
        QVERIFY(at >= 0);
        QCOMPARE(popup.rows().at(at).data, QStringLiteral("pick:main|openrouter|deepseek/deepseek-v4.1-flash"));
        QCOMPARE(popup.activate(), at);   // Enter answers with an index into rows(), which is the wider list

        // A model of a class that the cutoff had left out is reachable too.
        FilterPopup second;
        second.setRows(classRows(), kMainModelRow);
        second.onQueryRows = popup.onQueryRows;
        QVERIFY(!hasData(second.rows(), QStringLiteral("pick:main|claude-opus-5")));
        second.setFilterText(QStringLiteral("opus"));
        QVERIFY(hasData(second.rows(), QStringLiteral("pick:main|claude-opus-5")));
        // …and clearing the filter puts the box back exactly as it was.
        second.setFilterText(QString());
        QCOMPARE(second.rows().size(), classRows().size());
        QVERIFY(!hasData(second.rows(), QStringLiteral("pick:main|claude-opus-5")));
    }

    // No hook — the Alt+E level box, and every other list — and typing narrows what is there.
    void withNoHookTheFilterNarrowsTheRowsItWasGiven()
    {
        FilterPopup popup;
        popup.setRows(classRows(), kMainModelRow);
        popup.setFilterText(QStringLiteral("opus"));
        QCOMPARE(popup.rows().size(), classRows().size());
        QCOMPARE(popup.visibleCount(), 0);
    }

    void theModelsAreIndentedUnderTheirClassHeader()
    {
        theme::SurfaceRaised = QColor(0x24, 0x1c, 0x18);
        theme::Accent = QColor(0xc0, 0x7a, 0x4a);
        theme::Text = QColor(0xf0, 0xe6, 0xdd);
        theme::TextMuted = QColor(0x9a, 0x8b, 0x7f);
        QWidget anchor;
        anchor.resize(240, 22);
        anchor.show();

        // Every scanline's leftmost pixel that is the theme's text ink (the muted header and the
        // highlight band are other colours and are stepped over).
        const auto leftEdges = [](const QImage &shot) {
            QSet<int> edges;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor pixel = shot.pixelColor(x, y);
                    if (std::abs(pixel.red() - theme::Text.red())
                            + std::abs(pixel.green() - theme::Text.green())
                            + std::abs(pixel.blue() - theme::Text.blue()) < 120) { edges << x; break; }
                }
            return edges;
        };
        const auto render = [&anchor, &leftEdges](const QList<FilterRow> &rows) {
            FilterPopup popup(&anchor);
            popup.setRows(rows, 0);
            popup.openFor(&anchor);
            const QSet<int> edges = leftEdges(popup.grab().toImage());
            popup.dismiss();
            return edges;
        };
        const auto plainRow = [](const QString &text) {
            return FilterRow{text, text, {}, false, true, {}, {}, false};
        };
        const auto modelRow = [](const QString &text) {
            return FilterRow{text, text, {}, false, true, {}, QStringLiteral("main"), false};
        };

        // The same three words twice over, once with a "main" header above them. Comparing two
        // renders is what keeps the filter line's caret — drawn in the text ink at the far left of
        // every render — out of the measurement.
        const QSet<int> flat = render({plainRow(QStringLiteral("main")), plainRow(QStringLiteral("main")),
                                       plainRow(QStringLiteral("main"))});
        const QSet<int> sectioned = render({FilterRow{QStringLiteral("main"), QStringLiteral("class:main"),
                                                      {}, false, false, {}, QStringLiteral("main"), true},
                                            modelRow(QStringLiteral("main")), modelRow(QStringLiteral("main")),
                                            modelRow(QStringLiteral("main"))});
        QVERIFY2(!flat.isEmpty() && !sectioned.isEmpty(), "the rows were not drawn at all");
        const int caret = *std::min_element(flat.cbegin(), flat.cend());
        const auto past = [](const QSet<int> &columns, int from) {
            QSet<int> out;
            for (const int x : columns) if (x > from) out << x;
            return out;
        };
        const QSet<int> flatNames = past(flat, caret);
        const QSet<int> indented = past(sectioned, caret);
        QVERIFY(!flatNames.isEmpty() && !indented.isEmpty());
        const int flatAt = *std::min_element(flatNames.cbegin(), flatNames.cend());
        const int indentedAt = *std::min_element(indented.cbegin(), indented.cend());
        QVERIFY2(indentedAt > flatAt + 2,
                 qPrintable(QStringLiteral("flat names at %1, indented at %2").arg(flatAt).arg(indentedAt)));

        // A list with no header gets no indent: the Alt+E level box is drawn exactly as it was.
        const QSet<int> levels = past(render({plainRow(QStringLiteral("high")), plainRow(QStringLiteral("max"))}), caret);
        QVERIFY(!levels.isEmpty());
        QCOMPARE(*std::min_element(levels.cbegin(), levels.cend()), flatAt);
    }
};

QTEST_MAIN(FilterPopupTest)
#include "filterpopup_test.moc"
