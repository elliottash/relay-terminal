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

class KeymapTests : public QObject {
    Q_OBJECT
private slots:
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
        QVERIFY(keymap.conflicts().isEmpty());
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

    void cleanupTestCase() { QFile::remove(Keymap::instance().path()); }
};

QTEST_MAIN(KeymapTests)
#include "keymap_test.moc"
