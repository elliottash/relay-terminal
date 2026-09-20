// SPDX-License-Identifier: AGPL-3.0-or-later
// The terminal engine's core selection (--engine-core / RELAY_ENGINE_CORE) and the terminal
// pane's right-click menu (issue #X2F1).
#include "TerminalBackends.h"

#include <QStringList>
#include <QTest>

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

// What a backend that cannot answer every question reports. KonsolePart was the one that did
// this until it was retired (2026-09-18); the menu still has to degrade for any future backend
// that cannot hit-test, export its scrollback or zoom.
relay::TerminalMenuState limitedState() {
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
    void coreSelection() {
        QVERIFY(relay::resolveEngineCore(QString(), QString()).isEmpty());
        QVERIFY(relay::resolveEngineCore(QStringLiteral("sixel"), QString()).isEmpty());
        QCOMPARE(relay::resolveEngineCore(QStringLiteral("Ghostty"), QString()), QStringLiteral("ghostty"));
        QCOMPARE(relay::resolveEngineCore(QString(), QStringLiteral("libvterm")), QStringLiteral("libvterm"));
        QCOMPARE(relay::resolveEngineCore(QStringLiteral("libvterm"), QStringLiteral("ghostty")), QStringLiteral("libvterm"));
    }

    void processDefaultCoreIsRemembered() {
        QVERIFY(relay::defaultEngineCore().isEmpty());
        relay::setDefaultEngineCore(QStringLiteral("libvterm"));
        QCOMPARE(relay::defaultEngineCore(), QStringLiteral("libvterm"));
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

    void menuCarriesTheTerminalItemsWorthKeeping() {
        const QStringList ids = menuIds(relay::terminalContextMenu(relayEngineState()));
        for (const QString &id : {QStringLiteral("copy"), QStringLiteral("paste"), QStringLiteral("selectAll"),
                                  QStringLiteral("find"), QStringLiteral("clearScrollback"), QStringLiteral("reset"),
                                  QStringLiteral("saveOutput"), QStringLiteral("zoomIn"), QStringLiteral("zoomOut"),
                                  QStringLiteral("zoomReset"), QStringLiteral("splitRight"), QStringLiteral("splitDown"),
                                  QStringLiteral("equalize"), QStringLiteral("close")})
            QVERIFY2(ids.contains(id), qPrintable(id));
    }

    // pane.equalize: carried in every menu, greyed while the tab holds this pane alone —
    // equalizing then has nothing to share the space with — and live once a sibling exists.
    void menuCarriesEqualizeGreyedWhileThePaneIsAlone() {
        relay::TerminalMenuState state = relayEngineState();
        const auto items = relay::terminalContextMenu(state);
        const QStringList ids = menuIds(items);
        QVERIFY(ids.contains(QStringLiteral("equalize")));
        QCOMPARE(ids.indexOf(QStringLiteral("equalize")), ids.indexOf(QStringLiteral("close")) - 1);
        QVERIFY(!enabledOf(items, QStringLiteral("equalize")));
        state.canEqualize = true;
        QVERIFY(enabledOf(relay::terminalContextMenu(state), QStringLiteral("equalize")));
    }

    // A backend that can do less offers the same menu minus what it cannot do.
    void menuIsTheSameOnAnyBackendExceptForCapabilities() {
        const QStringList relayIds = menuIds(relay::terminalContextMenu(relayEngineState()));
        const QStringList limitedIds = menuIds(relay::terminalContextMenu(limitedState()));
        QStringList missing;
        for (const QString &id : relayIds)
            if (!limitedIds.contains(id)) missing << id;
        QCOMPARE(missing, (QStringList{QStringLiteral("saveOutput"), QStringLiteral("zoomIn"),
                                       QStringLiteral("zoomOut"), QStringLiteral("zoomReset")}));
        for (const QString &id : limitedIds)
            QVERIFY2(relayIds.contains(id), qPrintable(id));
        // The order of the shared entries is the same in both menus.
        QStringList shared;
        for (const QString &id : relayIds)
            if (limitedIds.contains(id)) shared << id;
        QCOMPARE(shared, limitedIds);
    }

    void menuGreysCopyOnlyWhenTheEngineKnowsTheSelection() {
        relay::TerminalMenuState state = relayEngineState();
        QVERIFY(!enabledOf(relay::terminalContextMenu(state), QStringLiteral("copy")));
        state.hasSelection = true;
        QVERIFY(enabledOf(relay::terminalContextMenu(state), QStringLiteral("copy")));
        // A backend that cannot answer the question keeps its Copy live.
        QVERIFY(enabledOf(relay::terminalContextMenu(limitedState()), QStringLiteral("copy")));
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

    // A pane in an ssh or mosh session offers a split that logs in to the same host (#S5SH).
    void menuOffersANewPaneOnTheSameHostOnlyInARemotePane() {
        relay::TerminalMenuState state = relayEngineState();
        QVERIFY(!menuIds(relay::terminalContextMenu(state)).contains(QStringLiteral("splitSameHost")));
        state.remoteHost = QStringLiteral("filly");
        const auto items = relay::terminalContextMenu(state);
        const QStringList ids = menuIds(items);
        QCOMPARE(ids.indexOf(QStringLiteral("splitSameHost")), ids.indexOf(QStringLiteral("splitDown")) + 1);
        for (const relay::TerminalMenuItem &item : items)
            if (item.id == QStringLiteral("splitSameHost")) QCOMPARE(item.label, QStringLiteral("New pane on filly"));
    }

    void menuOffersACardReferenceUnderThePointer() {
        relay::TerminalMenuState state = relayEngineState();
        QVERIFY(!menuIds(relay::terminalContextMenu(state)).contains(QStringLiteral("openCard")));
        state.cardId = QStringLiteral("K7Q2");
        state.cardTitle = QStringLiteral("Voice transcription");
        const auto items = relay::terminalContextMenu(state);
        const QStringList ids = menuIds(items);
        // Open the card, copy `#K7Q2`, and put `#K7Q2` in the prompt box.
        QVERIFY(ids.contains(QStringLiteral("openCard")));
        QVERIFY(ids.contains(QStringLiteral("copyCard")));
        QVERIFY(ids.contains(QStringLiteral("cardToPrompt")));
        for (const relay::TerminalMenuItem &item : items) {
            if (item.id == QStringLiteral("openCard")) {
                QVERIFY(item.label.contains(QStringLiteral("#K7Q2")));
                QVERIFY(item.label.contains(QStringLiteral("Voice transcription")));
            }
            if (item.id == QStringLiteral("copyCard")) QVERIFY(item.label.contains(QStringLiteral("#K7Q2")));
        }
        // A card the board cannot name still offers all three.
        state.cardTitle.clear();
        QCOMPARE(menuIds(relay::terminalContextMenu(state)).count(QStringLiteral("openCard")), 1);
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
            if (mask & 16) state.cardId = QStringLiteral("K7Q2");
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
