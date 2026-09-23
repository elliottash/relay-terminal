// Keymap (src/Keymap.h) on its own: the header is otherwise compiled only inside the one
// translation unit of the relay executable. Card #MAGP ("Change shortcut…" on a palette row).
// moc cannot parse the raw-string preset table in Keymap.h, and needs only this file's class.
#ifndef Q_MOC_RUN
#include "Keymap.h"
#endif

#include <QCoreApplication>
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
    // The owner's pairing rule (#QWAS): no letter has one Relay action on Ctrl and another on
    // Ctrl+Shift, and the everyday editing keys stay the editor's.
    void ctrlAndCtrlShiftNeverDiffer() {
        Keymap &keymap = Keymap::instance();
        keymap.clearOverrides();
        for (const auto &preset : Keymap::presets()) {
            keymap.setPreset(preset.first);
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

    void cleanupTestCase() { QFile::remove(Keymap::instance().path()); }
};

QTEST_MAIN(KeymapTests)
#include "keymap_test.moc"
