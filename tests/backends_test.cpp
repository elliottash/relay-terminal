// SPDX-License-Identifier: GPL-3.0-or-later
// Per-pane terminal engine selection: --engine / RELAY_ENGINE and --engine-core.
#include "TerminalBackends.h"

#include <QTest>

using relay::EngineKind;

class BackendsTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void namesRoundTrip() {
        QCOMPARE(relay::engineKindName(EngineKind::Konsole), QStringLiteral("konsole"));
        QCOMPARE(relay::engineKindName(EngineKind::Relay), QStringLiteral("relay"));
        EngineKind kind = EngineKind::Relay;
        QVERIFY(relay::parseEngineKind(relay::engineKindName(EngineKind::Konsole), &kind));
        QCOMPARE(int(kind), int(EngineKind::Konsole));
        QVERIFY(relay::parseEngineKind(relay::engineKindName(EngineKind::Relay), &kind));
        QCOMPARE(int(kind), int(EngineKind::Relay));
    }

    void parsesAliasesAndRejectsJunk() {
        EngineKind kind = EngineKind::Konsole;
        QVERIFY(relay::parseEngineKind(QStringLiteral("  KONSOLE "), &kind));
        QCOMPARE(int(kind), int(EngineKind::Konsole));
        QVERIFY(relay::parseEngineKind(QStringLiteral("kpart"), &kind));
        QCOMPARE(int(kind), int(EngineKind::Konsole));
        for (const QString &alias : {QStringLiteral("relay"), QStringLiteral("vterm"), QStringLiteral("Engine"),
                                     QStringLiteral("own")}) {
            kind = EngineKind::Konsole;
            QVERIFY2(relay::parseEngineKind(alias, &kind), qPrintable(alias));
            QCOMPARE(int(kind), int(EngineKind::Relay));
        }
        QVERIFY(!relay::parseEngineKind(QString(), nullptr));
        QVERIFY(!relay::parseEngineKind(QStringLiteral("xterm"), nullptr));
    }

    // KonsolePart stays the default; the command line wins over the environment.
    void resolutionOrder() {
        QCOMPARE(int(relay::resolveEngineKind(QString(), QString())), int(EngineKind::Konsole));
        QCOMPARE(int(relay::resolveEngineKind(QString(), QStringLiteral("konsole"))), int(EngineKind::Konsole));
        const EngineKind fromEnv = relay::resolveEngineKind(QString(), QStringLiteral("relay"));
        QCOMPARE(int(fromEnv), int(relay::engineAvailable() ? EngineKind::Relay : EngineKind::Konsole));
        // --engine=konsole beats RELAY_ENGINE=relay.
        QCOMPARE(int(relay::resolveEngineKind(QStringLiteral("konsole"), QStringLiteral("relay"))), int(EngineKind::Konsole));
    }

    void reportsUnknownValuesAndFallsBack() {
        QString warning;
        QCOMPARE(int(relay::resolveEngineKind(QStringLiteral("xterm"), QString(), &warning)), int(EngineKind::Konsole));
        QVERIFY(warning.contains(QStringLiteral("xterm")));
        // An unusable command-line value still lets the environment decide.
        warning.clear();
        QCOMPARE(int(relay::resolveEngineKind(QStringLiteral("nope"), QStringLiteral("konsole"), &warning)),
                 int(EngineKind::Konsole));
        QVERIFY(!warning.isEmpty());
        warning.clear();
        QCOMPARE(int(relay::resolveEngineKind(QString(), QString(), &warning)), int(EngineKind::Konsole));
        QVERIFY(warning.isEmpty());
    }

    void coreSelection() {
        QVERIFY(relay::resolveEngineCore(QString(), QString()).isEmpty());
        QVERIFY(relay::resolveEngineCore(QStringLiteral("sixel"), QString()).isEmpty());
        QCOMPARE(relay::resolveEngineCore(QStringLiteral("Ghostty"), QString()), QStringLiteral("ghostty"));
        QCOMPARE(relay::resolveEngineCore(QString(), QStringLiteral("libvterm")), QStringLiteral("libvterm"));
        QCOMPARE(relay::resolveEngineCore(QStringLiteral("libvterm"), QStringLiteral("ghostty")), QStringLiteral("libvterm"));
    }

    void processDefaultIsKonsole() {
        QCOMPARE(int(relay::defaultEngineKind()), int(EngineKind::Konsole));
        relay::setDefaultEngineKind(EngineKind::Relay);
        QCOMPARE(int(relay::defaultEngineKind()), int(EngineKind::Relay));
        relay::setDefaultEngineCore(QStringLiteral("libvterm"));
        QCOMPARE(relay::defaultEngineCore(), QStringLiteral("libvterm"));
        relay::setDefaultEngineKind(EngineKind::Konsole);
        relay::setDefaultEngineCore(QString());
        QVERIFY(relay::defaultEngineCore().isEmpty());
    }
};

QTEST_MAIN(BackendsTests)
#include "backends_test.moc"
