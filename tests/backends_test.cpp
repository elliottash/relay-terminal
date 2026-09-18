// SPDX-License-Identifier: GPL-3.0-or-later
// Per-pane terminal engine selection (--engine / RELAY_ENGINE and --engine-core) and the terminal
// pane's right-click menu (issue #X2F1).
#include "TerminalBackends.h"

#include <QStringList>
#include <QTest>

using relay::EngineKind;

namespace {
// The non-separator ids of a right-click menu, in order.
QStringList menuIds(const QList<relay::TerminalMenuItem> &items) {
    QStringList ids;
    for (const relay::TerminalMenuItem &item : items)
        if (!item.isSeparator()) ids << item.id;
    return ids;
}

bool enabledOf(const QList<relay::TerminalMenuItem> &items, const QString &id) {
    for (const relay::TerminalMenuItem &item : items)
        if (item.id == id) return item.enabled;
    return false;
}

// What Relay's own engine reports.
relay::TerminalMenuState relayEngineState() {
    relay::TerminalMenuState state;
    state.selectionKnown = true;
    state.canSearch = state.canReadOutput = state.canInject = state.canZoom = true;
    state.canTakeControl = true;
    return state;
}

// What a KonsolePart pane reports: no selection query, no hit-testing, no scrollback export.
relay::TerminalMenuState konsoleState() {
    relay::TerminalMenuState state;
    state.selectionKnown = false;
    state.canSearch = true;       // Relay's own find bar, not the engine's
    state.canReadOutput = false;
    state.canInject = true;
    state.canZoom = false;
    state.canTakeControl = true;
    return state;
}
}  // namespace

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

    // The Relay engine is the default while it is being tested (owner, 2026-09-17);
    // the command line still wins over the environment.
    void resolutionOrder() {
        QCOMPARE(int(relay::resolveEngineKind(QString(), QString())),
                 int(relay::engineAvailable() ? EngineKind::Relay : EngineKind::Konsole));
        QCOMPARE(int(relay::resolveEngineKind(QString(), QStringLiteral("konsole"))), int(EngineKind::Konsole));
        const EngineKind fromEnv = relay::resolveEngineKind(QString(), QStringLiteral("relay"));
        QCOMPARE(int(fromEnv), int(relay::engineAvailable() ? EngineKind::Relay : EngineKind::Konsole));
        // --engine=konsole beats RELAY_ENGINE=relay.
        QCOMPARE(int(relay::resolveEngineKind(QStringLiteral("konsole"), QStringLiteral("relay"))), int(EngineKind::Konsole));
    }

    void reportsUnknownValuesAndFallsBack() {
        QString warning;
        QCOMPARE(int(relay::resolveEngineKind(QStringLiteral("xterm"), QString(), &warning)),
                 int(relay::engineAvailable() ? EngineKind::Relay : EngineKind::Konsole));
        QVERIFY(warning.contains(QStringLiteral("xterm")));
        // An unusable command-line value still lets the environment decide.
        warning.clear();
        QCOMPARE(int(relay::resolveEngineKind(QStringLiteral("nope"), QStringLiteral("konsole"), &warning)),
                 int(EngineKind::Konsole));
        QVERIFY(!warning.isEmpty());
        warning.clear();
        QCOMPARE(int(relay::resolveEngineKind(QString(), QString(), &warning)),
                 int(relay::engineAvailable() ? EngineKind::Relay : EngineKind::Konsole));
        QVERIFY(warning.isEmpty());
    }

    void coreSelection() {
        QVERIFY(relay::resolveEngineCore(QString(), QString()).isEmpty());
        QVERIFY(relay::resolveEngineCore(QStringLiteral("sixel"), QString()).isEmpty());
        QCOMPARE(relay::resolveEngineCore(QStringLiteral("Ghostty"), QString()), QStringLiteral("ghostty"));
        QCOMPARE(relay::resolveEngineCore(QString(), QStringLiteral("libvterm")), QStringLiteral("libvterm"));
        QCOMPARE(relay::resolveEngineCore(QStringLiteral("libvterm"), QStringLiteral("ghostty")), QStringLiteral("libvterm"));
    }

    void processDefaultIsRelayEngine() {
        QCOMPARE(int(relay::defaultEngineKind()), int(EngineKind::Relay));
        relay::setDefaultEngineKind(EngineKind::Konsole);
        QCOMPARE(int(relay::defaultEngineKind()), int(EngineKind::Konsole));
        relay::setDefaultEngineKind(EngineKind::Relay);
        QCOMPARE(int(relay::defaultEngineKind()), int(EngineKind::Relay));
        relay::setDefaultEngineCore(QStringLiteral("libvterm"));
        QCOMPARE(relay::defaultEngineCore(), QStringLiteral("libvterm"));
        relay::setDefaultEngineKind(EngineKind::Konsole);
        relay::setDefaultEngineCore(QString());
        QVERIFY(relay::defaultEngineCore().isEmpty());
    }

    // ----- the terminal pane's right-click menu (issue #X2F1) ----------------------------------

    void menuKeepsRelayOwnEntriesFirst() {
        relay::TerminalMenuState state = relayEngineState();
        state.hasTurn = true;
        const QStringList ids = menuIds(relay::terminalContextMenu(state));
        QCOMPARE(ids.value(0), QStringLiteral("turn"));
        QCOMPARE(ids.value(1), QStringLiteral("takeControl"));
        QCOMPARE(ids.value(2), QStringLiteral("tasks"));
        // No finished turn yet: the entry is left out rather than shown dead.
        state.hasTurn = false;
        QCOMPARE(menuIds(relay::terminalContextMenu(state)).value(0), QStringLiteral("takeControl"));
    }

    void menuCarriesTheKonsoleItemsWorthKeeping() {
        const QStringList ids = menuIds(relay::terminalContextMenu(relayEngineState()));
        for (const QString &id : {QStringLiteral("copy"), QStringLiteral("paste"), QStringLiteral("selectAll"),
                                  QStringLiteral("find"), QStringLiteral("clearScrollback"), QStringLiteral("reset"),
                                  QStringLiteral("saveOutput"), QStringLiteral("zoomIn"), QStringLiteral("zoomOut"),
                                  QStringLiteral("zoomReset"), QStringLiteral("splitRight"), QStringLiteral("splitDown"),
                                  QStringLiteral("close")})
            QVERIFY2(ids.contains(id), qPrintable(id));
    }

    // Both engines offer the same menu; only what an engine cannot do is missing.
    void menuIsTheSameOnBothEnginesExceptForCapabilities() {
        const QStringList relayIds = menuIds(relay::terminalContextMenu(relayEngineState()));
        const QStringList konsoleIds = menuIds(relay::terminalContextMenu(konsoleState()));
        QStringList missing;
        for (const QString &id : relayIds)
            if (!konsoleIds.contains(id)) missing << id;
        QCOMPARE(missing, (QStringList{QStringLiteral("saveOutput"), QStringLiteral("zoomIn"),
                                       QStringLiteral("zoomOut"), QStringLiteral("zoomReset")}));
        for (const QString &id : konsoleIds)
            QVERIFY2(relayIds.contains(id), qPrintable(id));
        // The order of the shared entries is the same in both menus.
        QStringList shared;
        for (const QString &id : relayIds)
            if (konsoleIds.contains(id)) shared << id;
        QCOMPARE(shared, konsoleIds);
    }

    void menuGreysCopyOnlyWhenTheEngineKnowsTheSelection() {
        relay::TerminalMenuState state = relayEngineState();
        QVERIFY(!enabledOf(relay::terminalContextMenu(state), QStringLiteral("copy")));
        state.hasSelection = true;
        QVERIFY(enabledOf(relay::terminalContextMenu(state), QStringLiteral("copy")));
        // KonsolePart cannot answer the question, so its Copy stays live.
        QVERIFY(enabledOf(relay::terminalContextMenu(konsoleState()), QStringLiteral("copy")));
    }

    void menuOffersLinkAndFileEntriesOnlyWhenThereIsOneUnderThePointer() {
        relay::TerminalMenuState state = relayEngineState();
        QStringList ids = menuIds(relay::terminalContextMenu(state));
        QVERIFY(!ids.contains(QStringLiteral("openLink")));
        QVERIFY(!ids.contains(QStringLiteral("openFile")));
        state.link = QStringLiteral("https://example.invalid/x");
        ids = menuIds(relay::terminalContextMenu(state));
        QVERIFY(ids.contains(QStringLiteral("openLink")));
        QVERIFY(ids.contains(QStringLiteral("copyLink")));
        state.link.clear();
        state.filePath = QStringLiteral("/tmp/relay/notes.md");
        const auto items = relay::terminalContextMenu(state);
        QVERIFY(menuIds(items).contains(QStringLiteral("openFile")));
        for (const relay::TerminalMenuItem &item : items)
            if (item.id == QStringLiteral("openFile")) QVERIFY(item.label.contains(QStringLiteral("notes.md")));
    }

    void menuNeverHasALooseSeparator() {
        for (int mask = 0; mask < 256; ++mask) {
            relay::TerminalMenuState state;
            state.selectionKnown = mask & 1;
            state.hasSelection = mask & 2;
            state.canSearch = mask & 4;
            state.canReadOutput = mask & 8;
            state.canInject = mask & 16;
            state.canZoom = mask & 32;
            state.hasTurn = mask & 64;
            state.canTakeControl = mask & 128;
            if (mask & 4) state.link = QStringLiteral("https://example.invalid/");
            if (mask & 8) state.filePath = QStringLiteral("/tmp/relay/x");
            const auto items = relay::terminalContextMenu(state);
            QVERIFY(!items.isEmpty());
            QVERIFY(!items.first().isSeparator());
            QVERIFY(!items.last().isSeparator());
            for (int i = 1; i < items.size(); ++i)
                QVERIFY(!(items.at(i).isSeparator() && items.at(i - 1).isSeparator()));
            for (const relay::TerminalMenuItem &item : items)
                if (!item.isSeparator()) QVERIFY(!item.label.isEmpty());
        }
    }
};

QTEST_MAIN(BackendsTests)
#include "backends_test.moc"
