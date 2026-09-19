// SPDX-License-Identifier: GPL-3.0-or-later
// An unknown slash command is Relay's to answer, not the shell's: which lines are an attempt at
// a command, which are paths the shell keeps, and what the unknown-command line says.
#include "SlashCommands.h"

#include <QTest>

using namespace relay::slash;

namespace {
// The built-ins, in registry order (Pane::slashCommands()), plus one alias this window has.
QStringList registry() {
    return {QStringLiteral("new"), QStringLiteral("clear"), QStringLiteral("help"),
            QStringLiteral("model"), QStringLiteral("main"), QStringLiteral("flash"),
            QStringLiteral("compact"), QStringLiteral("context"), QStringLiteral("rewind"),
            QStringLiteral("rewind-code"), QStringLiteral("fork"), QStringLiteral("resume"),
            QStringLiteral("conversations"), QStringLiteral("find"), QStringLiteral("plan"),
            QStringLiteral("tasks"), QStringLiteral("todos"), QStringLiteral("shell"),
            QStringLiteral("agent"), QStringLiteral("deploy")};
}
// `/todos` is the one name Relay answers to and never teaches (card #SHE3).
QStringList hiddenNames() { return {QStringLiteral("todos")}; }
// A machine where only the real absolute paths of this test exist.
Probe machine() {
    return [](const QString &path) {
        return path == QStringLiteral("/tmp") || path == QStringLiteral("/usr") || path == QStringLiteral("/bin");
    };
}
}  // namespace

class SlashCommandTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ----- what is an attempt at a command ------------------------------------------------
    void unknownNameIsACommandAttempt() {
        QCOMPARE(attemptedName(QStringLiteral("/nosuchthing"), machine()), QStringLiteral("nosuchthing"));
        // With arguments, and with the whitespace a prompt box collects.
        QCOMPARE(attemptedName(QStringLiteral("  /nosuchthing with args  "), machine()), QStringLiteral("nosuchthing"));
        // The wrong case is still an attempt; the caller finds no such name and suggests /help.
        QCOMPARE(attemptedName(QStringLiteral("/Help"), machine()), QStringLiteral("Help"));
    }

    void realCommandsAreRecognisedAsNames() {
        // The Pane looks the name up: every built-in comes back so it can run, and `/shell` and
        // `/agent` come back as themselves so the router still sees them.
        for (const QString &name : registry())
            QCOMPARE(attemptedName(QLatin1Char('/') + name, machine()), name);
        QCOMPARE(attemptedName(QStringLiteral("/compact the older turns"), machine()), QStringLiteral("compact"));
    }

    // ----- a name Relay answers to but never teaches (card #SHE3) ---------------------------
    //
    // The list a person reads is the task list now, so the palette must not put "todos" back in
    // front of anyone; but `/todos` is in people's fingers and in saved prompts, so it has to keep
    // working. These are the rules Pane::slashCommandFor() and the `/` popup run.

    void aHiddenNameIsNeverOfferedByThePalette() {
        const QStringList shown = offered(registry(), hiddenNames());
        QVERIFY(!shown.contains(QStringLiteral("todos")));
        QVERIFY(shown.contains(QStringLiteral("tasks")));
        // Nothing else is dropped, and the registry's order is kept.
        QCOMPARE(shown.size(), registry().size() - 1);
        QCOMPARE(shown.first(), QStringLiteral("new"));
        QCOMPARE(shown.at(shown.indexOf(QStringLiteral("tasks")) - 1), QStringLiteral("plan"));
    }

    void aHiddenNameTypedInFullStillResolves() {
        bool exact = false;
        QCOMPARE(resolve(QStringLiteral("todos"), registry(), hiddenNames(), &exact), QStringLiteral("todos"));
        QVERIFY(exact);   // an exact match, so it runs even with arguments after it
        // And it is still a known name, so the unknown-command line never claims it does not exist.
        QVERIFY(registry().contains(attemptedName(QStringLiteral("/todos"), machine())));
    }

    void aPrefixNeverCompletesToAHiddenName() {
        bool exact = true;
        // "/to" could be "todos" by spelling; it must not be, or the palette teaches the old word.
        QCOMPARE(resolve(QStringLiteral("to"), registry(), hiddenNames(), &exact), QString());
        QVERIFY(!exact);
        // With nothing hidden it would have completed — the rule is the hiding, not the spelling.
        QCOMPARE(resolve(QStringLiteral("to"), registry(), {}), QStringLiteral("todos"));
        // A prefix of a visible name still completes, in registry order.
        QCOMPARE(resolve(QStringLiteral("ta"), registry(), hiddenNames()), QStringLiteral("tasks"));
        QCOMPARE(resolve(QStringLiteral("re"), registry(), hiddenNames()), QStringLiteral("rewind"));
        // An exact name wins over any completion, and an unknown prefix resolves to nothing.
        QCOMPARE(resolve(QStringLiteral("rewind"), registry(), hiddenNames()), QStringLiteral("rewind"));
        QCOMPARE(resolve(QStringLiteral("zz"), registry(), hiddenNames()), QString());
    }

    void pathsAreNeverCommands() {
        // An absolute path to a program: the shell keeps it, whether or not it exists.
        QVERIFY(attemptedName(QStringLiteral("/usr/bin/foo"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/usr/bin/foo --flag"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/home/me/run.sh"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/nosuchdir/nosuchthing"), machine()).isEmpty());
        // A single-segment path that is really there is a path too.
        QVERIFY(attemptedName(QStringLiteral("/tmp"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/bin"), machine()).isEmpty());
        // `/` alone, a lone slash with arguments, and anything that is not a name.
        QVERIFY(attemptedName(QStringLiteral("/"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/ etc"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/2fast"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/*.txt"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/path/with spaces"), machine()).isEmpty());
        // Not a slash line at all, and a multi-line draft (prose or a paste).
        QVERIFY(attemptedName(QStringLiteral("ls -la"), machine()).isEmpty());
        QVERIFY(attemptedName(QStringLiteral("/nosuchthing\nsecond line"), machine()).isEmpty());
    }

    // ----- the suggestions ----------------------------------------------------------------
    void closestNamesAreSuggested() {
        // A typing slip: a wrong letter, a missing one, an extra one, two swapped.
        QCOMPARE(closest(QStringLiteral("compsct"), registry()).value(0), QStringLiteral("compact"));
        QCOMPARE(closest(QStringLiteral("comact"), registry()).value(0), QStringLiteral("compact"));
        QCOMPARE(closest(QStringLiteral("compactt"), registry()).value(0), QStringLiteral("compact"));
        QCOMPARE(closest(QStringLiteral("comapct"), registry()).value(0), QStringLiteral("compact"));
        // A name half typed, and one typed in the wrong case.
        QCOMPARE(closest(QStringLiteral("conv"), registry()).value(0), QStringLiteral("conversations"));
        QCOMPARE(closest(QStringLiteral("HELP"), registry()).value(0), QStringLiteral("help"));
        // Aliases are in the list the Pane passes, so they are suggested like built-ins.
        QCOMPARE(closest(QStringLiteral("deplyo"), registry()).value(0), QStringLiteral("deploy"));
        // Nothing close: silence beats a wrong guess.
        QVERIFY(closest(QStringLiteral("nosuchthing"), registry()).isEmpty());
        // A short name gets one slip, not two: "plan" is two from "man".
        QVERIFY(!closest(QStringLiteral("man"), registry()).contains(QStringLiteral("plan")));
        QVERIFY(closest(QStringLiteral("mian"), registry()).contains(QStringLiteral("main")));
        QVERIFY(closest(QStringLiteral("x"), registry(), 3).size() <= 3);
    }

    // ----- the line Relay prints ------------------------------------------------------------
    void unknownLineNamesTheCommandAndThePathOut() {
        const QString typo = unknownLine(QStringLiteral("compsct"), registry());
        QVERIFY(typo.contains(QStringLiteral("Unknown command: /compsct")));
        QVERIFY(typo.contains(QStringLiteral("did you mean /compact?")));
        QVERIFY(typo.contains(QStringLiteral("/help")));
        QVERIFY(typo.contains(QStringLiteral("type / for every command")));
        // No suggestion to make: the line still says what happened and where to look.
        const QString none = unknownLine(QStringLiteral("nosuchthing"), registry());
        QVERIFY(none.contains(QStringLiteral("Unknown command: /nosuchthing")));
        QVERIFY(!none.contains(QStringLiteral("did you mean")));
        QVERIFY(none.contains(QStringLiteral("/help")));
        // Several candidates read as a list.
        const QString many = unknownLine(QStringLiteral("re"), registry());
        QVERIFY(many.contains(QStringLiteral("did you mean")));
        QVERIFY(many.contains(QStringLiteral(" or ")));
    }
};

QTEST_MAIN(SlashCommandTests)
#include "slashcommands_test.moc"
