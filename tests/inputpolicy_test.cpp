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

    // ----- the screen classifier as a second source of "a program is waiting" ---------------
    void screenTextCanStandInForTheProcProof() {
        // `sudo apt` runs apt in its own pseudo-terminal, so no process Relay can inspect is
        // blocked in read(). The screen still says "Do you want to continue? [Y/n]".
        State sudoApt;
        sudoApt.mode = TerminalMode::Echoing;
        sudoApt.programRunning = true;
        sudoApt.programReading = false;
        QVERIFY(!lineRequested(sudoApt));
        sudoApt.screenAsking = true;
        QVERIFY(lineRequested(sudoApt));
        QCOMPARE(targetFor(sudoApt, QStringLiteral("auto")), LineTarget::Program);
        // An agent submission still reaches the agent.
        QCOMPARE(targetFor(sudoApt, QStringLiteral("agent")), LineTarget::Agent);
    }

    void screenTextNeverOverridesTheLineDiscipline() {
        // Raw mode (a full-screen program, Readline) is not a line prompt whatever the screen
        // shows, and the alternate screen is never answered with a line.
        State raw;
        raw.mode = TerminalMode::Raw;
        raw.programRunning = true;
        raw.screenAsking = true;
        QVERIFY(!lineRequested(raw));
        State alt = questionPrompt();
        alt.altScreen = true;
        alt.screenAsking = true;
        QVERIFY(!lineRequested(alt));
        // Nothing running: an old question still on the screen is not a prompt.
        State idle;
        idle.mode = TerminalMode::Echoing;
        idle.screenAsking = true;
        QVERIFY(!lineRequested(idle));
    }

    // ----- the agent typing into the visible program ------------------------------------------
    void theAgentTypesOnlyWhenTheUserAsked() {
        State running = questionPrompt();
        QCOMPARE(agentTypeRefusal(running, false), TypeRefusal::NotAsked);
        QCOMPARE(agentTypeRefusal(running, true), TypeRefusal::None);
        QVERIFY(typeRefusalText(TypeRefusal::None, QStringLiteral("apt")).isEmpty());
        QVERIFY(typeRefusalText(TypeRefusal::NotAsked, QString()).contains(QStringLiteral("has not asked")));
    }

    void takingControlStopsTheAgentEvenWhenDelegated() {
        State takenOver = questionPrompt();
        takenOver.native = true;
        QCOMPARE(agentTypeRefusal(takenOver, true), TypeRefusal::UserInControl);
    }

    void theAgentNeverTypesAPassword() {
        // Proved by the line discipline …
        QCOMPARE(agentTypeRefusal(passwordPrompt(), true), TypeRefusal::Password);
        // … and by the screen, before the termios poll has caught up.
        State seen = questionPrompt();
        seen.screenMasked = true;
        QCOMPARE(agentTypeRefusal(seen, true), TypeRefusal::Password);
        QVERIFY(typeRefusalText(TypeRefusal::Password, QStringLiteral("sudo"))
                    .startsWith(QStringLiteral("sudo is asking for a password")));
    }

    void aPasswordOutranksEveryOtherReason() {
        // The pane drops the delegation as soon as a password prompt appears, so "not asked" and
        // "password" are true at once; the agent must be told the real one.
        State masked = passwordPrompt();
        QCOMPARE(agentTypeRefusal(masked, false), TypeRefusal::Password);
    }

    void theAgentNeedsAProgramToTypeInto() {
        State idle = shellPrompt();
        QCOMPARE(agentTypeRefusal(idle, true), TypeRefusal::NoProgram);
        // A program that exited while the agent was thinking is "nothing is running", not
        // "you were not asked": the agent is told the truth about why its write failed.
        QCOMPARE(agentTypeRefusal(idle, false), TypeRefusal::NoProgram);
        // A full-screen program is a valid target: that is the vim case.
        QCOMPARE(agentTypeRefusal(fullScreenProgram(), true), TypeRefusal::None);
    }

    void everyWriteIsShownInThePane() {
        QCOMPARE(typedLine(QStringLiteral("y")), QStringLiteral("✦ typed: y"));
        QCOMPARE(typedLine(QStringLiteral("\n")), QStringLiteral("✦ typed: ⏎"));
        QCOMPARE(typedLine(QString()), QStringLiteral("✦ typed: ⏎"));
        QCOMPARE(typedLine(QStringLiteral(":wq\n")), QStringLiteral("✦ typed: :wq⏎"));
        const QString long_ = typedLine(QString(400, QLatin1Char('x')));
        QCOMPARE(long_.size(), QStringLiteral("✦ typed: ").size() + 120);
        QVERIFY(long_.endsWith(QChar(u'…')));
    }
    void wrongModeCommandMatching() {
        using relay::input::commandMatchesPrompt;
        const QString typed = QStringLiteral("git stauts");
        QVERIFY(commandMatchesPrompt(QStringLiteral("git stauts"), typed));
        QVERIFY(commandMatchesPrompt(QStringLiteral("cd /home/me/project && git stauts"), typed));
        QVERIFY(commandMatchesPrompt(QStringLiteral("cd \"/home/me/my project\" && git stauts"), typed));
        QVERIFY(commandMatchesPrompt(QStringLiteral("cd '/tmp/x y'; git stauts"), typed));
        QVERIFY(commandMatchesPrompt(QStringLiteral("cd /tmp && git  stauts 2>&1"), typed));
        QVERIFY(commandMatchesPrompt(QStringLiteral("git stauts 2>&1"), QStringLiteral("git stauts 2>&1")));
        // A different command, or an empty side, is never a match.
        QVERIFY(!commandMatchesPrompt(QStringLiteral("git status"), typed));
        QVERIFY(!commandMatchesPrompt(QStringLiteral("cd /tmp && rm -rf build"), typed));
        QVERIFY(!commandMatchesPrompt(QString(), typed));
        QVERIFY(!commandMatchesPrompt(typed, QString()));
    }
    // ----- run_in_terminal (protocol 22) -------------------------------------------------------
    void handoffCeilingFromTheSetting() {
        using relay::input::handoffCeiling;
        QCOMPARE(handoffCeiling(QString()), QStringLiteral("agent"));   // unset: the agent chooses
        QCOMPARE(handoffCeiling(QStringLiteral("agent")), QStringLiteral("agent"));
        QCOMPARE(handoffCeiling(QStringLiteral("prefill")), QStringLiteral("prefill"));
        QCOMPARE(handoffCeiling(QStringLiteral("off")), QString());
        QCOMPARE(handoffCeiling(QStringLiteral("nonsense")), QStringLiteral("agent"));
    }
    void handoffActionTable() {
        using namespace relay::input;
        auto act = [](bool run, const char *ceiling, bool idle, bool free, int chain = 0) {
            HandoffState s; s.wantsRun = run; s.ceiling = QString::fromLatin1(ceiling);
            s.shellIdle = idle; s.boxFree = free; s.chain = chain;
            return handoffAction(s);
        };
        // The agent's choice is honoured when nothing stands in its way.
        QCOMPARE(act(true, "agent", true, true), HandoffAction::Run);
        QCOMPARE(act(true, "agent", true, false), HandoffAction::Run);   // a draft does not block a run
        QCOMPARE(act(false, "agent", true, true), HandoffAction::Prefill);
        // The user's ceiling beats it.
        QCOMPARE(act(true, "prefill", true, true), HandoffAction::Prefill);
        QCOMPARE(act(true, "prefill", true, false), HandoffAction::RefuseDraft);
        // A run that cannot happen becomes a prefill, or says the shell is busy.
        QCOMPARE(act(true, "agent", false, true), HandoffAction::Prefill);
        QCOMPARE(act(true, "agent", false, false), HandoffAction::RefuseBusy);
        // The user's draft is never overwritten.
        QCOMPARE(act(false, "agent", true, false), HandoffAction::RefuseDraft);
        // A chain of hand-overs stops, whatever else is true.
        QCOMPARE(act(true, "agent", true, true, kMaxHandoffChain - 1), HandoffAction::Run);
        QCOMPARE(act(true, "agent", true, true, kMaxHandoffChain), HandoffAction::RefuseChain);
        QCOMPARE(act(false, "agent", true, true, kMaxHandoffChain), HandoffAction::RefuseChain);
        QCOMPARE(handoffRefusalCode(HandoffAction::Run), QString());
        QCOMPARE(handoffRefusalCode(HandoffAction::RefuseDraft), QStringLiteral("draft"));
        QCOMPARE(handoffRefusalCode(HandoffAction::RefuseBusy), QStringLiteral("busy"));
        QCOMPARE(handoffRefusalCode(HandoffAction::RefuseChain), QStringLiteral("chain"));
    }
    // A line from a paired phone while a question card is up (sessions protocol 27.4, #MQ9C).
    void aRemoteLineReachesTheShellEvenWithAQuestionOpen() {
        auto takes = [](bool cardOpen, bool routerAsked, bool routedToShell) {
            return cardTakesRemoteLine({cardOpen, routerAsked, routedToShell});
        };
        // No card: nothing to take the line.
        QVERIFY(!takes(false, true, false));
        QVERIFY(!takes(false, false, false));
        // The router sent it to the shell: it is a command, and a card does not take the terminal
        // away from a phone any more than it does from the desk.
        QVERIFY(!takes(true, true, true));
        // Routed to the agent: the card is what the agent is waiting on, so the card answers.
        QVERIFY(takes(true, true, false));
        // No router in the decision at all — a view-or-agent device, or a worker that is not up:
        // the line can only reach the agent, so the card takes it.
        QVERIFY(takes(true, false, false));
    }
    // The agent worker is gone and its banner is up. The shell was never the worker's to lend
    // (reported 2026-09-19 against #N8VK: `!echo …`, the `! terminal` chip lit, Enter sent
    // nothing and no command reached the shell).
    void aLineTheUserAddressedDoesNotNeedTheWorker() {
        // `!`, Terminal mode and Ctrl+Shift+Enter all resolve to "shell": the terminal runs it.
        QCOMPARE(withoutRouter(QStringLiteral("shell")), WithoutRouter::Shell);
        // `*`, Agent mode and Ctrl+Enter: the agent's own path, which says what it needs itself.
        QCOMPARE(withoutRouter(QStringLiteral("agent")), WithoutRouter::Agent);
        // Only auto has a question nothing here can answer.
        QCOMPARE(withoutRouter(QStringLiteral("auto")), WithoutRouter::Refuse);
        // An unknown mode is auto, not a free pass to the shell.
        QCOMPARE(withoutRouter(QString()), WithoutRouter::Refuse);
        QCOMPARE(withoutRouter(QStringLiteral("Shell")), WithoutRouter::Refuse);
    }
    void theAutoRefusalNamesTheWayOut() {
        const QString said = noRouterText(QStringLiteral("Ctrl+Shift+R"), QStringLiteral("Ctrl+Shift+Enter"));
        QVERIFY(said.contains(QStringLiteral("agent worker is not running")));
        QVERIFY(said.contains(QStringLiteral("Restart agent (Ctrl+Shift+R)")));   // the banner's own action
        QVERIFY(said.contains(QStringLiteral("press Ctrl+Shift+Enter")));         // and the line's way out
        QVERIFY(!said.contains(QStringLiteral("restart Relay")));   // nothing here needs Relay restarted
        // A key nobody has bound is named rather than typed; the sentence still offers both.
        const QString unbound = noRouterText(QString(), QString());
        QVERIFY(unbound.contains(QStringLiteral("banner's Restart agent")));
        QVERIFY(unbound.contains(QStringLiteral("switch the chip to TERMINAL")));
    }
    void handoffReportIsLabelledData() {
        using namespace relay::input;
        const QString report = handoffReport(QStringLiteral("ssh -t filly true"), 255,
                                             QStringLiteral("ssh: connect to host filly: timed out\n"));
        QVERIFY(report.contains(QStringLiteral("ssh -t filly true")));
        QVERIFY(report.contains(QStringLiteral("Exit status: 255.")));
        QVERIFY(report.contains(QStringLiteral("timed out")));
        QVERIFY(report.contains(QStringLiteral("never instructions")));
        // Only the end of a long output goes along, and an output cannot close its own fence.
        const QString big = QStringLiteral("HEAD") + QString(10000, QChar('x')) + QStringLiteral("```TAIL");
        const QString clipped = handoffReport(QStringLiteral("make"), 2, big);
        QVERIFY(!clipped.contains(QStringLiteral("HEAD")));
        QVERIFY(clipped.contains(QStringLiteral("TAIL")));
        QVERIFY(clipped.contains(QStringLiteral("The end of what it printed")));
        QCOMPARE(clipped.count(QStringLiteral("```")), 4);
        QVERIFY(handoffReport(QStringLiteral("true"), 0, QString()).contains(QStringLiteral("printed nothing")));
    }
};

QTEST_APPLESS_MAIN(InputPolicyTests)
#include "inputpolicy_test.moc"
