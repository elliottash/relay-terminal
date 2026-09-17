// SPDX-License-Identifier: GPL-3.0-or-later
// The rules behind "the prompt box is the only keyboard input": where a submitted line goes,
// when the pane offers "Take control", what may be kept, and how a password is wiped.
#include "InputPolicy.h"

#include <QTest>

using namespace relay::input;

namespace {
State shellPrompt() {
    State state;
    state.mode = TerminalMode::Raw;   // Readline keeps the tty raw at an idle prompt
    return state;
}
State passwordPrompt() {
    State state;
    state.mode = TerminalMode::Secret;
    state.programRunning = true;
    return state;
}
State questionPrompt() {
    State state;
    state.mode = TerminalMode::Echoing;
    state.programRunning = true;
    state.programReading = true;
    return state;
}
State fullScreenProgram() {
    State state;
    state.mode = TerminalMode::Raw;
    state.programRunning = true;
    state.altScreen = true;
    return state;
}
}  // namespace

class InputPolicyTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ----- detection --------------------------------------------------------------------
    void secretNeedsCanonicalInputWithoutEcho() {
        QVERIFY(secretPrompt(passwordPrompt()));
        // A full-screen program turns canonical input off, so it is never a password prompt.
        QVERIFY(!secretPrompt(fullScreenProgram()));
        QVERIFY(!secretPrompt(shellPrompt()));
        // Echo off at an idle shell (no foreground program) is not a password prompt either.
        State idle = passwordPrompt();
        idle.programRunning = false;
        QVERIFY(!secretPrompt(idle));
        // vim can turn echo off on the alternate screen; the button, not masked input.
        State alt = passwordPrompt();
        alt.altScreen = true;
        QVERIFY(!secretPrompt(alt));
    }

    void lineRequestedNeedsAReaderAndEcho() {
        QVERIFY(lineRequested(questionPrompt()));
        // Nothing blocked in read(): the program is only printing, so commands queue.
        State printing = questionPrompt();
        printing.programReading = false;
        QVERIFY(!lineRequested(printing));
        // Echo off is the password case, not this one.
        QVERIFY(!lineRequested(passwordPrompt()));
        QVERIFY(!lineRequested(shellPrompt()));
    }

    // ----- where a submitted line goes ---------------------------------------------------
    void linesGoToTheShellUnlessAProgramIsReading() {
        QCOMPARE(targetFor(shellPrompt(), QStringLiteral("auto")), LineTarget::Shell);
        QCOMPARE(targetFor(questionPrompt(), QStringLiteral("auto")), LineTarget::Program);
        QCOMPARE(targetFor(passwordPrompt(), QStringLiteral("auto")), LineTarget::Program);
        QCOMPARE(targetFor(questionPrompt(), QStringLiteral("shell")), LineTarget::Program);
        // A long-running program that is not reading keeps the queue behaviour.
        State running = questionPrompt();
        running.programReading = false;
        QCOMPARE(targetFor(running, QStringLiteral("auto")), LineTarget::Shell);
        // A full-screen program needs native input; nothing is written to it from here.
        QCOMPARE(targetFor(fullScreenProgram(), QStringLiteral("auto")), LineTarget::Shell);
    }

    void agentSubmissionsAreNeverDivertedToAProgram() {
        // Decision 4: Ctrl+I / Ctrl+Enter still reach the agent at a password prompt.
        QCOMPARE(targetFor(passwordPrompt(), QStringLiteral("agent")), LineTarget::Agent);
        QCOMPARE(targetFor(questionPrompt(), QStringLiteral("agent")), LineTarget::Agent);
    }

    void nativeInputSubmitsNothing() {
        State native = questionPrompt();
        native.native = true;
        QCOMPARE(targetFor(native, QStringLiteral("auto")), LineTarget::Shell);
        QVERIFY(!offerTakeControl(native, false));
    }

    // ----- the take-control affordance ----------------------------------------------------
    void takeControlIsOfferedForFullScreenAndRemotePrograms() {
        QVERIFY(offerTakeControl(fullScreenProgram(), false));
        State ssh = questionPrompt();
        ssh.programReading = false;
        QVERIFY(offerTakeControl(ssh, true));       // ssh/mosh never switch screens
        QVERIFY(!offerTakeControl(ssh, false));
        QVERIFY(!offerTakeControl(shellPrompt(), false));
        State taken = fullScreenProgram();
        taken.native = true;
        QVERIFY(!offerTakeControl(taken, false));   // already in control
    }

    // ----- what may be kept ---------------------------------------------------------------
    void answersAndPasswordsAreNeverKept() {
        // Prompt history, the queue, the ledger, the session file, logs, route assist,
        // suggestions and model prompts all go through this one predicate.
        QVERIFY(retainable(LineTarget::Shell, false));
        QVERIFY(retainable(LineTarget::Agent, false));
        QVERIFY(!retainable(LineTarget::Program, false));   // "y" answered to apt
        QVERIFY(!retainable(LineTarget::Program, true));    // a password
        QVERIFY(!retainable(LineTarget::Shell, true));      // a password, whatever the target
        QVERIFY(!retainable(LineTarget::Agent, true));
    }

    // ----- wording -------------------------------------------------------------------------
    void statusAndChipNameTheProgram() {
        QCOMPARE(sentToProgram(QStringLiteral("apt")), QStringLiteral("Sent to apt"));
        QCOMPARE(sentToProgram(QString()), QStringLiteral("Sent to the program"));
        QCOMPARE(passwordChip(QStringLiteral("sudo")), QStringLiteral("password for sudo"));
        QCOMPARE(passwordChip(QString()), QStringLiteral("password for the program"));
    }

    // ----- the password itself --------------------------------------------------------------
    void takeHandsTheLineOverOnceAndWipesIt() {
        Secret secret;
        QVERIFY(secret.isEmpty());
        secret.set(QStringLiteral("hunter2"));
        QCOMPARE(secret.size(), 7);
        QCOMPARE(secret.take(), QStringLiteral("hunter2\n"));
        QVERIFY(secret.isEmpty());
        QCOMPARE(secret.size(), 0);
        QCOMPARE(secret.take(), QStringLiteral("\n"));   // nothing left to hand over
    }

    void wipeOverwritesTheCharactersInPlace() {
        QString line = QStringLiteral("hunter2");
        zero(line);
        QCOMPARE(line.size(), 7);   // the buffer is overwritten, not just released
        for (const QChar c : std::as_const(line)) QCOMPARE(c, QChar(u'\0'));
        wipe(line);
        QVERIFY(line.isEmpty());
    }

    void settingASecretKeepsAPrivateCopy() {
        Secret secret;
        secret.set(QStringLiteral("first"));
        QString typed = QStringLiteral("second-secret");
        secret.set(typed);                                  // wipes "first"
        QCOMPARE(typed, QStringLiteral("second-secret"));   // the caller's string is a separate copy
        QCOMPARE(secret.take(), QStringLiteral("second-secret\n"));
        QCOMPARE(typed, QStringLiteral("second-secret"));
    }

    void emptySecretsAreHarmless() {
        Secret secret;
        secret.set(QString());
        QVERIFY(secret.isEmpty());
        secret.wipe();
        QString empty;
        wipe(empty);
        QVERIFY(empty.isEmpty());
    }
};

QTEST_APPLESS_MAIN(InputPolicyTests)
#include "inputpolicy_test.moc"
