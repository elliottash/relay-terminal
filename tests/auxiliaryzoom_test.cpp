// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AuxiliaryZoom.h"
#include "AgentInternalsView.h"
#include <QtTest>
#include <QLineEdit>
#include <QVBoxLayout>

class AuxiliaryZoomTest : public QObject {
    Q_OBJECT
private slots:
    void activityView() {
        relay::AgentInternalsView activity;
        activity.resize(700, 420);
        activity.beginTurn("azp7", "Inspect auxiliary keyboard zoom");
        activity.setThinking("azp7", "thinking", "Activity transcript text grows with keyboard zoom.\nThe owning terminal is unaffected.", true, 1200);
        activity.show();
        activity.focusView();
        QTest::qWait(30);
        auto *log = activity.findChild<QPlainTextEdit *>();
        QVERIFY(log);
        const qreal base = log->font().pointSizeF();
        const QString evidence = qEnvironmentVariable("AZP7_EVIDENCE");
        if (!evidence.isEmpty()) QVERIFY(activity.grab().save(evidence + "/activity-before.png"));
        for (int i = 0; i < 4; ++i) QVERIFY(relay::auxiliaryZoom::zoom(&activity, log, 1));
        QCOMPARE(log->font().pointSizeF(), base + 4);
        QTest::qWait(30);
        if (!evidence.isEmpty()) QVERIFY(activity.grab().save(evidence + "/activity-zoomed.png"));
        QVERIFY(relay::auxiliaryZoom::zoom(&activity, log, 0));
        QCOMPARE(log->font().pointSizeF(), base);
        QTest::qWait(30);
        if (!evidence.isEmpty()) QVERIFY(activity.grab().save(evidence + "/activity-reset.png"));
    }
    void transcriptAndAskFocus() {
        QWidget leaf, other;
        QVBoxLayout layout(&leaf);
        QPlainTextEdit log;
        log.setReadOnly(true);
        log.setPlainText("Activity transcript");
        QLineEdit ask;
        layout.addWidget(&log); layout.addWidget(&ask);
        leaf.show();
        const qreal base = log.font().pointSizeF();
        QVERIFY(relay::auxiliaryZoom::zoom(&leaf, log.viewport(), 1));
        QCOMPARE(log.font().pointSizeF(), base + 1);
        QVERIFY(relay::auxiliaryZoom::zoom(&leaf, &ask, -1));
        QCOMPARE(log.font().pointSizeF(), base);
        relay::auxiliaryZoom::zoom(&leaf, &ask, 1);
        relay::auxiliaryZoom::zoom(&leaf, &ask, 0);
        QCOMPARE(log.font().pointSizeF(), base);
        QVERIFY(!relay::auxiliaryZoom::zoom(&other, &log, 1));
    }
    void wheelThenReset() {
        QWidget leaf;
        QTextEdit log(&leaf);
        log.setReadOnly(true);
        const QFont base = log.font();
        relay::auxiliaryZoom::rememberFont(&log);
        log.zoomIn(3); // Qt's read-only Ctrl+wheel uses this same zoom operation.
        QVERIFY(log.font().pointSizeF() > base.pointSizeF());
        QVERIFY(relay::auxiliaryZoom::zoom(&leaf, &log, 0));
        QCOMPARE(log.font(), base);
    }
    void editableAndHiddenIgnored() {
        QWidget leaf;
        QPlainTextEdit editor(&leaf), hidden(&leaf);
        hidden.setReadOnly(true); hidden.hide();
        QVERIFY(!relay::auxiliaryZoom::zoom(&leaf, &editor, 1));
        QVERIFY(!relay::auxiliaryZoom::zoom(nullptr, nullptr, 1));
    }
};
QTEST_MAIN(AuxiliaryZoomTest)
#include "auxiliaryzoom_test.moc"
