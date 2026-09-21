// SPDX-License-Identifier: AGPL-3.0-or-later
// view/FaintInk.h: the colour SGR 2 and a fold's dim rows are drawn in (#LG7T, #TK9C).
// The rule is one sentence — as faint as it can be while it still reaches 4.5:1 on the background
// it is drawn on, and never fainter than the host's own ink — so the cases here are the ways that
// can go wrong: ink with room to fade, ink with none, ink that never had 4.5:1 to begin with, and
// the greys Relay's shipped themes actually use.
#include "view/FaintInk.h"

#include <QtTest>

using namespace relay;

namespace {

// The muted text token and the ground it sits on, from data/theme/themes/*.toml.
const QColor kDarkMuted(QStringLiteral("#8b919c"));
const QColor kDarkGround(QStringLiteral("#0f1115"));
const QColor kLightMuted(QStringLiteral("#5c6472"));
const QColor kLightGround(QStringLiteral("#fbfbfd"));

} // namespace

class FaintInkTest : public QObject {
    Q_OBJECT

private slots:
    void luminanceAndContrastAreWcag()
    {
        QVERIFY(qFuzzyIsNull(relativeLuminance(QColor(Qt::black))));
        QVERIFY(std::abs(relativeLuminance(QColor(Qt::white)) - 1.0) < 1e-9);
        QVERIFY(std::abs(contrastRatio(QColor(Qt::white), QColor(Qt::black)) - 21.0) < 1e-6);
        QVERIFY(std::abs(contrastRatio(QColor(Qt::black), QColor(Qt::white)) - 21.0) < 1e-6);
        QVERIFY(std::abs(contrastRatio(kDarkMuted, kDarkMuted) - 1.0) < 1e-9);
    }

    // The old 0.6 alpha faded 40% toward the ground. On both shipped themes' muted grey that is
    // under the floor, which is the bug this helper exists for.
    void theOldSixtyPercentAlphaWasUnderTheFloor()
    {
        QVERIFY(contrastRatio(mixToward(kDarkMuted, kDarkGround, 0.4), kDarkGround) < kTextContrast);
        QVERIFY(contrastRatio(mixToward(kLightMuted, kLightGround, 0.4), kLightGround) < kTextContrast);
    }

    void everyPairStaysAboveTheFloorAndFadesWhereItCan_data()
    {
        QTest::addColumn<QColor>("fg");
        QTest::addColumn<QColor>("bg");
        QTest::addColumn<bool>("hasRoom"); // the full-strength ink clears the floor with something to spare
        QTest::newRow("relay dark muted grey") << kDarkMuted << kDarkGround << true;
        QTest::newRow("relay light muted grey") << kLightMuted << kLightGround << true;
        QTest::newRow("relay dark body text") << QColor(QStringLiteral("#e6e8ec")) << kDarkGround << true;
        QTest::newRow("relay light body text") << QColor(QStringLiteral("#1a1d24")) << kLightGround << true;
        QTest::newRow("white on black") << QColor(Qt::white) << QColor(Qt::black) << true;
        QTest::newRow("black on white") << QColor(Qt::black) << QColor(Qt::white) << true;
        // A diff's green on the fold's tinted band: coloured ink on a ground that is not the
        // terminal's own, which is the case the second call site exists for.
        QTest::newRow("green on a tinted band") << QColor(QStringLiteral("#7ee787")) << QColor(QStringLiteral("#12301a")) << true;
        // No room: ink that only just clears the floor, and ink that never did.
        QTest::newRow("just above the floor") << QColor(QStringLiteral("#767676")) << QColor(Qt::white) << false;
        QTest::newRow("already below the floor") << QColor(QStringLiteral("#999999")) << QColor(Qt::white) << false;
        QTest::newRow("ink on its own colour") << kDarkMuted << kDarkMuted << false;
    }

    void everyPairStaysAboveTheFloorAndFadesWhereItCan()
    {
        QFETCH(QColor, fg);
        QFETCH(QColor, bg);
        QFETCH(bool, hasRoom);
        const QColor faint = faintInk(fg, bg);

        QVERIFY2(faint.alpha() == 255, "faint ink is an opaque colour, not an alpha");
        // Never worse than what the host asked for.
        if (contrastRatio(fg, bg) >= kTextContrast)
            QVERIFY2(contrastRatio(faint, bg) >= kTextContrast - 1e-9,
                     qPrintable(QStringLiteral("%1 on %2 fell to %3:1")
                                    .arg(faint.name(), bg.name())
                                    .arg(contrastRatio(faint, bg), 0, 'f', 2)));
        else
            QCOMPARE(faint, QColor(fg.red(), fg.green(), fg.blue()));
        // And never fainter than the 40% the old alpha faded by.
        QVERIFY(contrastRatio(faint, bg) >= contrastRatio(mixToward(fg, bg, kFaintFade), bg) - 1e-9);

        if (hasRoom) {
            QVERIFY2(faint != QColor(fg.red(), fg.green(), fg.blue()),
                     qPrintable(QStringLiteral("%1 on %2 had room to fade and did not")
                                    .arg(fg.name(), bg.name())));
            QVERIFY(contrastRatio(faint, bg) < contrastRatio(fg, bg)); // visibly fainter
        } else {
            QCOMPARE(faint, QColor(fg.red(), fg.green(), fg.blue()));
        }
    }

    // The two shipped themes, in numbers: the fade the helper picks is real, and it lands on the
    // floor rather than under it the way the flat 40% did.
    void theShippedMutedGreysLandOnTheFloor()
    {
        for (const auto &pair : {std::make_pair(kDarkMuted, kDarkGround), std::make_pair(kLightMuted, kLightGround)}) {
            const QColor faint = faintInk(pair.first, pair.second);
            const double ratio = contrastRatio(faint, pair.second);
            QVERIFY2(ratio >= kTextContrast, qPrintable(QString::number(ratio)));
            QVERIFY2(ratio < kTextContrast + 0.2, qPrintable(QString::number(ratio))); // as faint as it may be
            QVERIFY(ratio < contrastRatio(pair.first, pair.second));
        }
    }

    // Alpha on the way in is ignored: the host's ink is a colour, and so is the answer.
    void anAlphaOnTheInputIsDropped()
    {
        QColor translucent = kDarkMuted;
        translucent.setAlphaF(0.6);
        QCOMPARE(faintInk(translucent, kDarkGround), faintInk(kDarkMuted, kDarkGround));
    }

    void aZeroFadeIsTheInkItself()
    {
        QCOMPARE(faintInk(kDarkMuted, kDarkGround, 0.0), kDarkMuted);
    }

    // legibleOn(): the same machinery for the opposite problem (#SQ3D). The bands here are the
    // agent colour each shipped theme paints a row the user typed in, and the inks are that
    // theme's own bright cyan — the palette entry the echoed `/command` is written with, which
    // was chosen against the terminal's ground and lands on the band by accident.
    void aWrittenInkIsMovedOntoTheFloorOfItsBand_data()
    {
        QTest::addColumn<QColor>("fg");
        QTest::addColumn<QColor>("bg");
        QTest::addColumn<bool>("mustMove");
        QTest::newRow("relay dark: bright cyan on the violet band")
            << QColor(QStringLiteral("#78ddea")) << QColor(QStringLiteral("#b48ef7")) << true;
        QTest::newRow("relay light: teal on the violet band")
            << QColor(QStringLiteral("#0f5d61")) << QColor(QStringLiteral("#7c3aed")) << true;
        QTest::newRow("ibm beige: teal on the purple band")
            << QColor(QStringLiteral("#0a4a46")) << QColor(QStringLiteral("#7500c3")) << true;
        QTest::newRow("gruvbox: bright aqua on the pink band")
            << QColor(QStringLiteral("#8ec07c")) << QColor(QStringLiteral("#c88fc2")) << true;
        // Already legible: handed back untouched, so nothing that reads well is repainted.
        QTest::newRow("cyan on the terminal's own ground")
            << QColor(QStringLiteral("#78ddea")) << QColor(QStringLiteral("#0f1115")) << false;
        QTest::newRow("black on white") << QColor(Qt::black) << QColor(Qt::white) << false;
    }

    void aWrittenInkIsMovedOntoTheFloorOfItsBand()
    {
        QFETCH(QColor, fg);
        QFETCH(QColor, bg);
        QFETCH(bool, mustMove);
        const QColor ink = legibleOn(fg, bg);
        QVERIFY2(contrastRatio(ink, bg) >= kTextContrast,
                 qPrintable(QStringLiteral("%1 on %2 is %3:1")
                                .arg(ink.name(), bg.name())
                                .arg(contrastRatio(ink, bg), 0, 'f', 2)));
        if (!mustMove) {
            QCOMPARE(ink, QColor(fg.red(), fg.green(), fg.blue()));
            return;
        }
        QVERIFY(ink != QColor(fg.red(), fg.green(), fg.blue()));
        // It moved as little as it could: it sits on the floor, not well past it.
        QVERIFY2(contrastRatio(ink, bg) < kTextContrast + 0.25,
                 qPrintable(QStringLiteral("%1 was pushed to %2:1").arg(ink.name()).arg(contrastRatio(ink, bg), 0, 'f', 2)));
        // And it kept its hue rather than becoming black or white.
        QVERIFY(ink != QColor(Qt::black) && ink != QColor(Qt::white));
        QVERIFY(std::abs(ink.toHsv().hue() - fg.toHsv().hue()) <= 2);
    }

    // The worst ground there is — the luminance where black and white score alike — still gets
    // ink that clears the floor, which is why legibleOn() needs no answer for "neither pole works".
    void theCrossoverGroundStillGetsLegibleInk()
    {
        for (const QString &grey : {QStringLiteral("#767676"), QStringLiteral("#797979"), QStringLiteral("#808080")}) {
            const QColor bg(grey);
            const QColor ink = legibleOn(QColor(QStringLiteral("#3ec5f0")), bg);
            QVERIFY2(contrastRatio(ink, bg) >= kTextContrast,
                     qPrintable(QStringLiteral("%1 on %2 is %3:1").arg(ink.name(), grey).arg(contrastRatio(ink, bg), 0, 'f', 2)));
        }
    }
};

QObject *makeFaintInkTest()
{
    return new FaintInkTest;
}

#include "FaintInkTest.moc"
