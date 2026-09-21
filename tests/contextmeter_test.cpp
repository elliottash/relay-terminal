// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ContextMeter.h"
#include <QTest>
#include <QLabel>
#include <QVBoxLayout>
#include <QDir>
using relay::context::Reading;
class ContextMeterTest : public QObject {
    Q_OBJECT
    QJsonObject relayEvent() const {
        return {{"window",128000},{"used_tokens",64000},{"percent",50.0},
                {"limit_tokens",71232},{"estimated",true}};
    }
private slots:
    void guestsUseTheirOwnWindow_data() {
        QTest::addColumn<QString>("guest"); QTest::addColumn<int>("window");
        QTest::newRow("codex") << QString("codex") << 258400;
        QTest::newRow("claude") << QString("claude") << 1000000;
    }
    void guestsUseTheirOwnWindow() {
        QFETCH(QString,guest); QFETCH(int,window);
        auto e=relayEvent(); e["guest"]=guest;
        e["guest_context"]=QJsonObject{{"window",window},{"used_tokens",window/10},{"percent",10.0}};
        const auto r=Reading::fromEvent(e);
        QCOMPARE(r.window,qint64(window)); QCOMPARE(r.used,qint64(window/10));
        QCOMPARE(r.label(),QString("90% left")); QCOMPARE(r.limit,qint64(0));
        QVERIFY(!r.estimated); QVERIFY(r.tooltip().contains("manages compaction"));
        QVERIFY(!r.tooltip().contains("128,000")); QVERIFY(!r.tooltip().contains("Auto-compacts"));
    }
    void unknownIsNotEmptyOrRelayFallback() {
        auto e=relayEvent(); e["guest"]="claude";
        auto r=Reading::fromEvent(e);
        QCOMPARE(r.window,qint64(0)); QCOMPARE(r.label(),QString("context unknown"));
        e["guest_context"]=QJsonObject{{"window",200000}};
        r=Reading::fromEvent(e);
        QCOMPARE(r.window,qint64(200000)); QCOMPARE(r.label(),QString("context unknown"));
        QVERIFY(r.tooltip().contains("usage not reported"));
        e["guest_context"]=QJsonObject{{"window",200000},{"used_tokens",0}};
        QCOMPARE(Reading::fromEvent(e).label(),QString("100% left"));
    }
    void switchAndNewSessionDoNotRetainPreviousGuest() {
        auto e=relayEvent(); e["guest"]="codex";
        e["guest_context"]=QJsonObject{{"window",258400},{"used_tokens",25840}};
        QCOMPARE(Reading::fromEvent(e).label(),QString("90% left"));
        e["guest"]="claude"; e["guest_context"]=QJsonObject{};
        QCOMPARE(Reading::fromEvent(e).label(),QString("context unknown"));
        const auto native=Reading::fromEvent(relayEvent());
        QVERIFY(native.guest.isEmpty()); QCOMPARE(native.window,qint64(128000));
        QCOMPARE(native.label(),QString("50% left")); QVERIFY(native.tooltip().contains("Auto-compacts"));
        QCOMPARE(native.limit,qint64(71232)); QVERIFY(native.estimated);
    }
    void renderEvidence() {
        const auto output=qEnvironmentVariable("RELAY_CONTEXT_EVIDENCE");
        if(output.isEmpty()) return;
        QWidget widget; auto *layout=new QVBoxLayout(&widget);
        for(const QString guest:{QString("codex"),QString("claude")}) {
            auto e=relayEvent();e["guest"]=guest;
            const int window=guest=="codex"?258400:200000;
            e["guest_context"]=QJsonObject{{"window",window},{"used_tokens",window/10},{"percent",10.0}};
            const auto r=Reading::fromEvent(e);
            layout->addWidget(new QLabel(guest+" · "+r.label()+"\n"+r.tooltip()));
        }
        auto e=relayEvent();e["guest"]="claude";
        const auto r=Reading::fromEvent(e);
        layout->addWidget(new QLabel(r.label()+"\n"+r.tooltip()));
        widget.resize(500,300);widget.show();QTest::qWait(50);
        QVERIFY(widget.grab().save(output+"/meter.png"));
    }
};
QTEST_MAIN(ContextMeterTest)
#include "contextmeter_test.moc"
