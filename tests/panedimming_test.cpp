// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneDimming.h"
#include <QTest>
#include <QLabel>
#include <QDir>

using relay::dimming::State;
class DimmingTest : public QObject {
    Q_OBJECT
private slots:
    void automaticLifecycle() {
        State s;
        s.observe(true, false, false, false);
        QCOMPARE(s.amount(true, false, 90), 0);
        s.observe(true, true, false, false);
        QCOMPARE(s.amount(true, false, 90), 90);
        s.observe(true, true, true, false);
        QCOMPARE(s.amount(true, false, 90), 0);
        s.observe(true, true, false, false);
        QCOMPARE(s.amount(true, false, 90), 90);
        s.observe(true, false, false, true);
        QCOMPARE(s.amount(true, false, 90), 0);
    }
    void manualSurvivesCompletionAndAttention() {
        State s;
        s.observe(false, true, false, false);
        s.toggle(85);
        s.observe(false, false, false, true);
        QCOMPARE(s.amount(true, true, 90), 85);
        s.observe(false, false, true, true);
        QCOMPARE(s.amount(true, true, 90), 0);
        s.observe(false, false, false, true);
        QCOMPARE(s.amount(true, true, 90), 85);
    }
    void enterRevealAndExplicitAdjustment() {
        State s;
        s.toggle(90);
        s.observe(true, false, false, false);
        QCOMPARE(s.amount(false, false, 90), 0);
        s.adjust(5, 0);
        QCOMPARE(s.amount(false, false, 90), 5);
        s.observe(false, false, false, false);
        s.observe(true, false, false, false);
        QCOMPARE(s.amount(false, false, 90), 0);
        s.observe(false, false, false, false);
        QCOMPARE(s.amount(false, false, 90), 5);
    }
    void focusAndBounds() {
        State s;
        QCOMPARE(s.amount(false, true, 90), 90);
        s.observe(false, false, false, true);
        QCOMPARE(s.amount(false, true, 90), 0);
        s.adjust(500, 0);
        QCOMPARE(s.amount(false, true, 90), 95);
        s.adjust(-500, 95);
        QCOMPARE(s.amount(false, true, 90), 0);
        QCOMPARE(s.wheelSteps(60), 0);
        QCOMPARE(s.wheelSteps(60), 1);
        QCOMPARE(s.wheelSteps(-240), -2);
    }
    void overlayLightDarkAndInput() {
        QWidget pane;
        pane.setAutoFillBackground(true);
        QPalette palette = pane.palette();
        palette.setColor(QPalette::Window, QColor(100, 100, 100));
        pane.setPalette(palette);
        pane.resize(300, 200);
        QLabel label("Pane header remains readable", &pane);
        label.setGeometry(10, 0, 280, 30);
        relay::dimming::Overlay overlay(&pane);
        pane.show();
        overlay.refresh(90, Qt::black, 30);
        QApplication::processEvents();
        QVERIFY(overlay.testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(overlay.focusPolicy(), Qt::NoFocus);
        QCOMPARE(overlay.geometry(), QRect(0, 30, 300, 170));
        auto dark = pane.grab().toImage();
        QVERIFY(dark.pixelColor(150, 100).red() < 20);
        QCOMPARE(dark.pixelColor(299, 10).red(), 100);
        overlay.refresh(90, Qt::white, 30);
        auto light = pane.grab().toImage();
        QVERIFY(light.pixelColor(150, 100).red() > 230);
        const QString evidence = qEnvironmentVariable("RELAY_DIM_EVIDENCE");
        if (!evidence.isEmpty()) {
            QVERIFY(dark.save(evidence + "/overlay-dark.png"));
            QVERIFY(light.save(evidence + "/overlay-light.png"));
        }
        pane.resize(400, 250);
        overlay.refresh(50, Qt::black, 40);
        QCOMPARE(overlay.geometry(), QRect(0, 40, 400, 210));
        overlay.refresh(0, Qt::black, 40);
        QVERIFY(overlay.isHidden());
    }
};
QTEST_MAIN(DimmingTest)
#include "panedimming_test.moc"
