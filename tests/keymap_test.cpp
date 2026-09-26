// Keymap (src/Keymap.h) on its own: the header is otherwise compiled only inside the one
// translation unit of the relay executable. Card #MAGP ("Change shortcut…" on a palette row).
// moc cannot parse the raw-string preset table in Keymap.h, and needs only this file's class.
#ifndef Q_MOC_RUN
#include "Keymap.h"
#endif

#include <QCoreApplication>
#include <QKeyEvent>
#include <QSettings>
#include <QtTest>

#include <algorithm>

class KeymapTests : public QObject {
    Q_OBJECT
private slots:
    void onePaneLabels() {
        const auto &actions = Keymap::instance().actions();
        const auto description = [&actions](const QString &id) {
            for (const ActionDef &action : actions)
                if (action.id == id) return action.description;
            return QString();
        };
        QCOMPARE(description(QStringLiteral("pane.splitRight")),
                 QStringLiteral("New shell to the right (then ← ↑ ↓ places it)"));
        QCOMPARE(description(QStringLiteral("pane.splitDown")), QStringLiteral("New shell below"));
        QVERIFY(description(QStringLiteral("helper.ask")).startsWith(QStringLiteral("Ask the agent")));
        QCOMPARE(description(QStringLiteral("tab.new")), QStringLiteral("New tab"));
    }
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTests"));
        QCoreApplication::setApplicationName(QStringLiteral("keymap"));
        QFile::remove(Keymap::instance().path());
        Keymap::instance().reload();
    }

    // "Change shortcut…" (#MAGP): setBinding writes one action's keys and takes effect at once;
    // the rest of keybindings.json stays; actionForKey names who holds a key; empty unbinds.
    void keymapSetBindingRebindsOneAction() {
        Keymap &keymap = Keymap::instance();
        QVERIFY(keymap.path().contains(QStringLiteral("RelayTerminalTests")));   // never the owner's file
        keymap.setPreset(QStringLiteral("relay"));
        keymap.clearOverrides();
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+A")), QStringLiteral("board.open"));
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+R")), QStringLiteral("review.open"));
        QVERIFY(keymap.keysFor(QStringLiteral("pane.restartShell")).isEmpty());
        QVERIFY(keymap.actionForKey(QStringLiteral("Ctrl+Alt+F9")).isEmpty());
        keymap.setBinding(QStringLiteral("app.update"), {QStringLiteral("Ctrl+Alt+F9")});
        QCOMPARE(keymap.keysFor(QStringLiteral("app.update")), QStringList{QStringLiteral("Ctrl+Alt+F9")});
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Alt+F9")), QStringLiteral("app.update"));
        QVERIFY(Keymap::sameKey(QStringLiteral("Ctrl+Shift+A"), QStringLiteral("Shift+Ctrl+A")));
        QVERIFY(!Keymap::sameKey(QStringLiteral("Ctrl+A"), QStringLiteral("Ctrl+Shift+A")));
        keymap.setBinding(QStringLiteral("board.open"), {});
        QVERIFY(keymap.keysFor(QStringLiteral("board.open")).isEmpty());
        QCOMPARE(keymap.keysFor(QStringLiteral("app.update")), QStringList{QStringLiteral("Ctrl+Alt+F9")});   // kept
        QCOMPARE(keymap.preset(), QStringLiteral("relay"));                                                 // kept
        keymap.clearOverrides();
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+A")), QStringLiteral("board.open"));
    }

    // A multiplexer inside a Relay pane — zellij, tmux — uses plain Alt+arrows to move between
    // its own panes, so those go to the program; Shift+Alt+arrows are the fallback that still
    // escapes a full-screen program (owner decision 2026-09-24, card #VD2M).
    void programKeysGivePlainAltArrowsToTheProgram() {
        Keymap &keymap = Keymap::instance();
        keymap.setPreset(QStringLiteral("relay"));
        keymap.clearOverrides();
        keymap.setProgramKeys(QStringLiteral("shift-only"));
        QKeyEvent plainAlt(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier, QString());
        QKeyEvent shiftAlt(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier | Qt::ShiftModifier, QString());
        QKeyEvent ctrlAlt(QEvent::KeyPress, Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier, QString());
        QVERIFY(!keymap.actsInsidePrograms(&plainAlt));
        QVERIFY(!keymap.actsInsidePrograms(&ctrlAlt));
        QVERIFY(keymap.actsInsidePrograms(&shiftAlt));
        QCOMPARE(keymap.match(&plainAlt), QStringLiteral("pane.focusLeft"));
        QCOMPARE(keymap.match(&shiftAlt), QStringLiteral("pane.focusLeft"));
        // Outside a program both spellings focus panes; inside one only the shifted twin acts.
        QKeyEvent shiftAltDown(QEvent::KeyPress, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier, QString());
        QCOMPARE(keymap.match(&shiftAltDown), QStringLiteral("pane.focusDown"));
        QVERIFY(keymap.actsInsidePrograms(&shiftAltDown));
    }
    // Alt+Esc is Relay's stop key "anytime" (its action says so, card #234Z): a remote session
    // client such as ssh or mosh would otherwise swallow the only key that leaves the session,
    // when the pane's keyboard is the terminal's. The first press is still only a Ctrl+C for
    // every other program, and "none" program keys still hand the key over.
    void stopKeyActsInsidePrograms() {
        Keymap &keymap = Keymap::instance();
        keymap.setPreset(QStringLiteral("relay"));
        keymap.clearOverrides();
        keymap.setProgramKeys(QStringLiteral("shift-only"));
        QKeyEvent altEsc(QEvent::KeyPress, Qt::Key_Escape, Qt::AltModifier, QString());
        QCOMPARE(keymap.match(&altEsc), QStringLiteral("terminal.interrupt"));
        QVERIFY(keymap.actsInsidePrograms(&altEsc));
        keymap.setProgramKeys(QStringLiteral("none"));
        QVERIFY(!keymap.actsInsidePrograms(&altEsc));
        keymap.setProgramKeys(QStringLiteral("shift-only"));
    }
    // The console scroll jumps (#X55K): Alt+Home / Alt+End are viewer keys — the engine's
    // scrollback moves, nothing reaches the program — so they answer while a program runs, like
    // the view's own Ctrl+Shift+Home. Plain Alt+arrows stay the program's (#VD2M); Home and End
    // under plain Alt are not multiplexer keys.
    void altHomeEndScrollActsInsidePrograms() {
        Keymap &keymap = Keymap::instance();
        keymap.setPreset(QStringLiteral("relay"));
        keymap.clearOverrides();
        keymap.setProgramKeys(QStringLiteral("shift-only"));
        QKeyEvent altHome(QEvent::KeyPress, Qt::Key_Home, Qt::AltModifier, QString());
        QKeyEvent altEnd(QEvent::KeyPress, Qt::Key_End, Qt::AltModifier, QString());
        QCOMPARE(keymap.match(&altHome), QStringLiteral("terminal.scrollTop"));
        QCOMPARE(keymap.match(&altEnd), QStringLiteral("terminal.scrollBottom"));
        QVERIFY(keymap.actsInsidePrograms(&altHome));
        QVERIFY(keymap.actsInsidePrograms(&altEnd));
        // "none" hands every key to the program — the owner's program-keys choice wins over ours.
        keymap.setProgramKeys(QStringLiteral("none"));
        QVERIFY(!keymap.actsInsidePrograms(&altHome));
        QVERIFY(!keymap.actsInsidePrograms(&altEnd));
        keymap.setProgramKeys(QStringLiteral("shift-only"));
    }
    // Program mode (#S976) is reached from the chip, the cycle and the busy row; no key of its own.
    void programModeActionIsAvailableAndUnbound() {
        const auto &actions = Keymap::instance().actions();
        bool found = false;
        for (const ActionDef &action : actions)
            if (action.id == QStringLiteral("input.modeProgram")) found = true;
        QVERIFY(found);
        QVERIFY(Keymap::instance().keysFor(QStringLiteral("input.modeProgram")).isEmpty());
    }
    // The owner's pairing rule (#QWAS): no letter has one Relay action on Ctrl and another on
    // Ctrl+Shift, and the everyday editing keys stay the editor's.
    void ctrlAndCtrlShiftNeverDiffer() {
        Keymap &keymap = Keymap::instance();
        keymap.clearOverrides();
        for (const auto &preset : Keymap::presets()) {
            keymap.setPreset(preset.first);
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+R")), QStringLiteral("review.open"));
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+S")), QStringLiteral("sessions.open"));
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+B")), QStringLiteral("background.open"));
            if (preset.first == QStringLiteral("warp"))
                QVERIFY(keymap.keysFor(QStringLiteral("files.explorer")).isEmpty());
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+P")), QStringLiteral("projects.open"));
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+?")), QStringLiteral("help.shortcuts"));
            QVERIFY(keymap.keysFor(QStringLiteral("palette.open")).isEmpty());
            for (char letter = 'A'; letter <= 'Z'; ++letter) {
                const QString plain = keymap.actionForKey(QStringLiteral("Ctrl+%1").arg(QLatin1Char(letter)));
                const QString shifted = keymap.actionForKey(QStringLiteral("Ctrl+Shift+%1").arg(QLatin1Char(letter)));
                if (!plain.isEmpty() && !shifted.isEmpty())
                    QVERIFY2(plain == shifted, qPrintable(QStringLiteral("%1: Ctrl+%2 is %3, Ctrl+Shift+%2 is %4")
                                                              .arg(preset.first).arg(QLatin1Char(letter)).arg(plain, shifted)));
            }
        }
        keymap.setPreset(QStringLiteral("relay"));
        for (const char *key : {"Ctrl+A", "Ctrl+S", "Ctrl+Z", "Ctrl+X", "Ctrl+C", "Ctrl+D", "Ctrl+G", "Ctrl+P", "F1"})
            QVERIFY2(keymap.actionForKey(QString::fromLatin1(key)).isEmpty(), key);
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+G")), QStringLiteral("globals.open"));
        // #XPEB: the fold walk owns both J keys; delegating has no key (the agent drives by default).
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+J")), QStringLiteral("folds.step"));
        QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Shift+J")), QStringLiteral("folds.step"));
        QVERIFY(keymap.keysFor(QStringLiteral("program.delegate")).isEmpty());
        QVERIFY(keymap.conflicts().isEmpty());
    }

    // Ctrl+Alt+E opens the "New pane" chooser in every preset (#83YV), and nothing else holds it.
    void newPaneChooserOwnsCtrlAltE() {
        Keymap &keymap = Keymap::instance();
        keymap.clearOverrides();
        for (const auto &preset : Keymap::presets()) {
            keymap.setPreset(preset.first);
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Alt+E")), QStringLiteral("pane.newChooser"));
            QVERIFY(keymap.conflicts().isEmpty());
        }
        keymap.setPreset(QStringLiteral("relay"));
    }

    void backgroundRunOwnsCtrlAltEnterAcrossPresets() {
        Keymap &keymap = Keymap::instance();
        keymap.clearOverrides();
        for (const auto &preset : Keymap::presets()) {
            keymap.setPreset(preset.first);
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Alt+Return")), QStringLiteral("pane.runInBackground"));
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Alt+Enter")), QStringLiteral("pane.runInBackground"));
            QCOMPARE(keymap.actionForKey(QStringLiteral("Ctrl+Enter")), QStringLiteral("agent.interrupt"));
            QVERIFY(keymap.conflicts().isEmpty());
        }
    }

    // The holder actions (#XQ8F): every pane's ssh lands in a session on the host, so the defaults
    // split onto the host and need a local split of their own, closing a pane needs a way to end
    // the session it leaves behind, and the sessions on a host need one place to reattach or end.
    // The persistent Connect is retired with that: the wrapper persists a plain login now.
    void holderActionsAreRegisteredAndPersistentConnectIsRetired() {
        const QList<ActionDef> actions = Keymap::instance().actions();
        const auto registered = [&actions](const QString &id) -> const ActionDef * {
            const auto it = std::find_if(actions.cbegin(), actions.cend(),
                                         [&id](const ActionDef &action) { return action.id == id; });
            return it == actions.cend() ? nullptr : &*it;
        };
        const auto hasNoDefaultKey = [&registered](const QString &id, const QString &category) {
            const ActionDef *action = registered(id);
            QVERIFY2(action, qPrintable(id));
            QCOMPARE(action->category, category);
            QVERIFY2(action->defaults.isEmpty(), qPrintable(id));   // none of the three takes a key
        };
        hasNoDefaultKey(QStringLiteral("pane.splitLocal"), QStringLiteral("pane"));
        hasNoDefaultKey(QStringLiteral("pane.closeEndRemote"), QStringLiteral("pane"));
        hasNoDefaultKey(QStringLiteral("ssh.remoteSessions"), QStringLiteral("tab"));
        hasNoDefaultKey(QStringLiteral("ssh.splitSameHost"), QStringLiteral("pane"));
        QVERIFY2(std::none_of(actions.cbegin(), actions.cend(), [](const ActionDef &action) {
                     return action.id == QStringLiteral("ssh.connectPersistent");
                 }), "ssh.connectPersistent is retired: the wrapper persists every login (#XQ8F)");
    }

    void cleanupTestCase() { QFile::remove(Keymap::instance().path()); }
};

QTEST_MAIN(KeymapTests)
#include "keymap_test.moc"
