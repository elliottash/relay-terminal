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

// The model box's three mode pages (card #MDL1, section 5.1): the four mode rows, a separator,
// then that mode's own models, then the two action rows. Each page opens on its own current row.
QList<FilterPage> modePages()
{
    const auto modeRows = [](const QString &marked) {
        QList<FilterRow> rows;
        for (const QString &mode : {QStringLiteral("high"), QStringLiteral("main"), QStringLiteral("flash")})
            rows << FilterRow{(mode == marked ? QStringLiteral("• ") : QStringLiteral("  ")) + mode,
                              QStringLiteral("role:") + mode, {}, false, true, {}};
        rows << FilterRow{{}, {}, {}, true, false, {}};
        return rows;
    };
    QList<FilterPage> pages;
    FilterPage high{QStringLiteral("high"), QStringLiteral("high"), modeRows(QStringLiteral("high")), -1};
    high.rows << FilterRow{QStringLiteral("gpt-6-astra"), QStringLiteral("pick:high|openai|gpt-6-astra"),
                           {}, false, true, QStringLiteral("codex")};
    high.current = high.rows.size() - 1;
    pages << high;
    FilterPage main{QStringLiteral("main"), QStringLiteral("main"), modeRows(QStringLiteral("main")), -1};
    main.rows << FilterRow{QStringLiteral("kimi-k3"), QStringLiteral("pick:main|kimi|kimi-k3"),
                           {}, false, true, QStringLiteral("kimi")};
    main.rows << FilterRow{QStringLiteral("glm-5.3"), QStringLiteral("pick:main|glm|glm-5.3"),
                           {}, false, true, QStringLiteral("z.ai +1")};
    main.current = 4;   // kimi-k3
    pages << main;
    FilterPage flash{QStringLiteral("flash"), QStringLiteral("flash"), modeRows(QStringLiteral("flash")), -1};
    flash.rows << FilterRow{QStringLiteral("glm-5.3-flash"), QStringLiteral("pick:flash|glm|glm-5.3-flash"),
                            {}, false, true, QStringLiteral("z.ai")};
    flash.rows << FilterRow{QStringLiteral("kimi-k3-turbo"), QStringLiteral("pick:flash|kimi|turbo"),
                            {}, false, true, QStringLiteral("kimi")};
    flash.current = 5;   // kimi-k3-turbo
    pages << flash;
    return pages;
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
    //    level highlighted. It has no pages, so Left and Right are not the popup's at all — they
    //    stay the filter line's caret keys, and nothing about this box changed.
    void theEffortListIsTheSameControl()
    {
        QList<FilterRow> levels;
        for (const QString &name : {QStringLiteral("minimal"), QStringLiteral("low"),
                                    QStringLiteral("high"), QStringLiteral("max")})
            levels << FilterRow{name, name, {}, false, true};
        FilterPopup popup;
        popup.setRows(levels, 2);
        QCOMPARE(popup.pageCount(), 0);
        QCOMPARE(popup.currentPageId(), QString());
        QCOMPARE(popup.currentRow(), 2);
        popup.turnPage(1);                        // nothing to turn to: a no-op, not a crash
        QCOMPARE(popup.currentRow(), 2);
        popup.setFilterText(QStringLiteral("ma"));
        QCOMPARE(popup.visibleCount(), 2);        // "max", and "minimal" fuzzily
        QCOMPARE(popup.currentRow(), 0);
        popup.setFilterText(QStringLiteral("max"));
        QCOMPARE(popup.visibleCount(), 1);
        QCOMPARE(popup.activate(), 3);
    }

    // ----- pages: Left / Right change the mode in place (card #MDL1, owner: "left/right changes
    // mode") ---------------------------------------------------------------------------------

    // 12. The box opens on the page the caller named, with that page's rows and that page's own
    //     current row highlighted — not the first row, and not the row another page was on.
    void pagesOpenOnTheNamedPageAndItsCurrentRow()
    {
        FilterPopup popup;
        popup.setPages(modePages(), QStringLiteral("main"));
        QCOMPARE(popup.pageCount(), 3);
        QCOMPARE(popup.currentPageId(), QStringLiteral("main"));
        QCOMPARE(popup.currentRow(), 4);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|kimi|kimi-k3"));
        QCOMPARE(popup.rows().at(1).text, QStringLiteral("• main"));   // the marker is on this mode
        // A page id nobody knows opens the first page rather than nothing at all.
        popup.setPages(modePages(), QStringLiteral("nope"));
        QCOMPARE(popup.currentPageId(), QStringLiteral("high"));
    }

    // 13. Right and Left turn the page in place: the rows below are the new mode's, the highlight
    //     is that mode's own model, the popup never closes and nothing is picked.
    void leftAndRightTurnThePageInPlace()
    {
        QWidget anchor;
        anchor.resize(160, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        int picked = -2;
        QStringList turns;
        popup.onPicked = [&picked](int row) { picked = row; };
        popup.onPageChanged = [&turns](const QString &id) { turns << id; };
        popup.setPages(modePages(), QStringLiteral("main"));
        popup.openFor(&anchor);

        popup.turnPage(1);
        QVERIFY(popup.isVisible());
        QCOMPARE(popup.currentPageId(), QStringLiteral("flash"));
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:flash|kimi|turbo"));
        QCOMPARE(popup.rows().at(2).text, QStringLiteral("• flash"));
        QVERIFY(popup.rows().last().data.startsWith(QStringLiteral("pick:flash")));

        popup.turnPage(-1);
        popup.turnPage(-1);
        QCOMPARE(popup.currentPageId(), QStringLiteral("high"));
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:high|openai|gpt-6-astra"));
        popup.turnPage(-1);                                   // clamped: the first page holds
        QCOMPARE(popup.currentPageId(), QStringLiteral("high"));
        QCOMPARE(turns, QStringList({QStringLiteral("flash"), QStringLiteral("main"),
                                     QStringLiteral("high")}));
        QCOMPARE(picked, -2);                                 // nothing was picked by any of it
        popup.dismiss();
    }

    // 14. The filter survives a turn and is re-applied to the new page: typing "k" then Right
    //     leaves "k" in the line and narrows the flash list by it.
    void aTurnKeepsTheFilterAndReAppliesIt()
    {
        FilterPopup popup;
        popup.setPages(modePages(), QStringLiteral("main"));
        popup.setFilterText(QStringLiteral("kimi"));
        QCOMPARE(popup.visibleCount(), 1);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:main|kimi|kimi-k3"));

        popup.turnPage(1);
        QCOMPARE(popup.filterText(), QStringLiteral("kimi"));
        QCOMPARE(popup.currentPageId(), QStringLiteral("flash"));
        QCOMPARE(popup.visibleCount(), 1);
        QCOMPARE(popup.rows().at(popup.currentRow()).data, QStringLiteral("pick:flash|kimi|turbo"));

        // Cleared, the whole of the page that is showing is back — mode rows and all.
        popup.setFilterText(QString());
        QCOMPARE(popup.visibleCount(), 5);
        // And the page's own current row takes the highlight again once it is asked for.
        popup.showPage(QStringLiteral("main"));
        QCOMPARE(popup.currentRow(), 4);
    }

    // 15. Enter on a mode row and Enter on a model row are told apart by the row's own data, which
    //     is what the caller acts on: the mode row says "switch the mode", the model row says
    //     "this pane, this mode, that model". Escape after any number of turns picks nothing.
    void enterAnswersWithTheRowsOwnDataAndEscapePicksNothing()
    {
        QWidget anchor;
        anchor.resize(160, 22);
        anchor.show();
        FilterPopup popup(&anchor);
        QStringList picked;
        popup.onPicked = [&popup, &picked](int row) { picked << popup.rows().at(row).data; };
        popup.setPages(modePages(), QStringLiteral("main"));
        popup.openFor(&anchor);
        popup.turnPage(1);
        popup.activate();                                     // the flash page's own model
        QCOMPARE(picked, QStringList({QStringLiteral("pick:flash|kimi|turbo")}));

        popup.setPages(modePages(), QStringLiteral("main"));
        popup.openFor(&anchor);
        popup.moveCurrent(-99);                               // up to the first mode row
        popup.activate();
        QCOMPARE(picked.last(), QStringLiteral("role:high"));

        bool cancelled = false;
        popup.onCancelled = [&cancelled] { cancelled = true; };
        popup.setPages(modePages(), QStringLiteral("main"));
        popup.openFor(&anchor);
        popup.turnPage(1);
        popup.turnPage(-1);
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
};

QTEST_MAIN(FilterPopupTest)
#include "filterpopup_test.moc"
