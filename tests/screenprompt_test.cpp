// SPDX-License-Identifier: GPL-3.0-or-later
// "Is the foreground program waiting for me to type something?" decided from the last rows of
// the screen (src/ScreenPrompt.cpp), against recorded screens in tests/fixtures/screen/.
#include "ScreenPrompt.h"

#include <QFile>
#include <QTest>

using namespace relay::screen;
using relay::input::TerminalMode;

namespace {

QString fixture(const QString &name) {
    QFile file(QStringLiteral(RELAY_SCREEN_FIXTURES "/") + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("missing fixture %s", qPrintable(name));
        return {};
    }
    QString text = QString::fromUtf8(file.readAll());
    if (text.endsWith(QLatin1Char('\n'))) text.chop(1);   // the file's final newline is not a row
    return text;
}

QStringList rowsOf(const QString &name) { return lastRows(fixture(name)); }

// A program that kept the terminal in canonical mode with echo on, which is what every program
// that asks a question on one line does. `reading` is the /proc proof Relay usually cannot get.
Signals asking(bool reading = false) {
    Signals sig;
    sig.mode = TerminalMode::Echoing;
    sig.programRunning = true;
    sig.programReading = reading;
    return sig;
}

Signals secret() {
    Signals sig = asking();
    sig.mode = TerminalMode::Secret;
    return sig;
}

// An idle shell: Readline keeps the tty raw and no foreground program runs.
Signals idleShell() {
    Signals sig;
    sig.mode = TerminalMode::Raw;
    return sig;
}

}  // namespace

class ScreenPromptTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ----- the fixtures exist and are what the other tests read ---------------------------
    void fixturesAreReadable() {
        for (const QString &name : {QStringLiteral("apt-continue.txt"), QStringLiteral("ssh-hostkey.txt"),
                                    QStringLiteral("git-rebase-todo.txt"), QStringLiteral("python-input.txt"),
                                    QStringLiteral("read-p.txt"), QStringLiteral("sudo-password.txt"),
                                    QStringLiteral("looks-like-a-question.txt")})
            QVERIFY2(!fixture(name).isEmpty(), qPrintable(name));
    }

    // ----- yes/no questions ---------------------------------------------------------------
    void aptAsksToContinue() {
        const Detection detection = detect(rowsOf(QStringLiteral("apt-continue.txt")), asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QCOMPARE(detection.options, QStringLiteral("[Y/n]"));
        QCOMPARE(detection.defaultAnswer, QStringLiteral("y"));
        QCOMPARE(detection.question, QStringLiteral("Do you want to continue? [Y/n]"));
        QVERIFY(detection.actionable());
        QVERIFY(!detection.masked);
        QCOMPARE(bannerText(QStringLiteral("apt"), detection),
                 QStringLiteral("apt is asking: Do you want to continue? [Y/n]"));
    }

    void aptQuestionSplitOverTwoRows() {
        // Narrow windows wrap the question; the options row alone says nothing on its own.
        const Detection detection = detect(rowsOf(QStringLiteral("apt-question-wrapped.txt")), asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QCOMPARE(detection.question, QStringLiteral("Do you want to continue? [Y/n]"));
    }

    void sshHostKeyIsAYesNoWithNoDefault() {
        const Detection detection = detect(rowsOf(QStringLiteral("ssh-hostkey.txt")), asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QCOMPARE(detection.options, QStringLiteral("(yes/no/[fingerprint])"));
        // Nothing is capitalized, so Enter picks nothing: Relay must not suggest a default.
        QCOMPARE(detection.defaultAnswer, QString());
        QVERIFY(detection.actionable());
    }

    void lowercaseDefaultIsRead() {
        const Detection detection = detect({QStringLiteral("Remove the old key? [y/N] ")}, asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QCOMPARE(detection.defaultAnswer, QStringLiteral("n"));
    }

    // ----- passwords ----------------------------------------------------------------------
    void sudoPasswordIsMasked() {
        const Detection detection = detect(rowsOf(QStringLiteral("sudo-password.txt")), secret());
        QCOMPARE(detection.kind, Kind::Password);
        QVERIFY(detection.masked);
        QVERIFY(detection.actionable());
        // The banner names the program and never repeats the prompt with a typed line beside it.
        QCOMPARE(bannerText(QStringLiteral("sudo"), detection), QStringLiteral("sudo is asking for a password"));
    }

    void sshPassphraseIsMaskedEvenBeforeEchoGoesOff() {
        // The termios poll runs four times a second; the screen says "passphrase" immediately.
        const Detection detection = detect(rowsOf(QStringLiteral("ssh-passphrase.txt")), asking());
        QCOMPARE(detection.kind, Kind::Password);
        QVERIFY(detection.masked);
    }

    void echoOffAloneIsAPasswordPrompt() {
        // A program that turned echo off without a recognizable word is still a secret prompt.
        const Detection detection = detect({QStringLiteral("Unlock: ")}, secret());
        QCOMPARE(detection.kind, Kind::Password);
        QVERIFY(detection.masked);
    }

    // ----- menus, keys and free text --------------------------------------------------------
    void updateAlternativesIsANumberedChoice() {
        const Detection detection = detect(rowsOf(QStringLiteral("update-alternatives.txt")), asking());
        QCOMPARE(detection.kind, Kind::Choice);
        QVERIFY(detection.actionable());
        QVERIFY(detection.question.endsWith(QStringLiteral("type selection number:")));
    }

    void pressEnterIsRecognized() {
        const Detection detection = detect(rowsOf(QStringLiteral("press-enter.txt")), asking());
        QCOMPARE(detection.kind, Kind::PressKey);
        QVERIFY(detection.actionable());
        QCOMPARE(bannerText(QStringLiteral("needrestart"), detection),
                 QStringLiteral("needrestart is waiting: Press [ENTER] to continue."));
    }

    void readPIsFreeText() {
        const Detection detection = detect(rowsOf(QStringLiteral("read-p.txt")), asking(true));
        QCOMPARE(detection.kind, Kind::FreeText);
        QCOMPARE(detection.question, QStringLiteral("Continue?"));
        QVERIFY(detection.actionable());
    }

    void pythonInputIsFreeText() {
        const Detection detection = detect(rowsOf(QStringLiteral("python-input.txt")), asking(true));
        QCOMPARE(detection.kind, Kind::FreeText);
        QCOMPARE(detection.question, QStringLiteral("Enter your name:"));
        QVERIFY(detection.actionable());
    }

    void replPromptsAreProgramsWaiting() {
        // ">>> " and "relay=# " end in sigils but are not the shell: they read a line.
        QCOMPARE(detect(rowsOf(QStringLiteral("python-repl.txt")), asking(true)).kind, Kind::FreeText);
        QCOMPARE(detect(rowsOf(QStringLiteral("psql.txt")), asking(true)).kind, Kind::FreeText);
    }

    // ----- the negatives ---------------------------------------------------------------------
    void outputThatMentionsAQuestionIsNotAQuestion() {
        // grep over Relay's own sources: three rows quote "[Y/n]", "(yes/no)" and "Password:".
        const Detection detection = detect(rowsOf(QStringLiteral("looks-like-a-question.txt")), asking());
        QCOMPARE(detection.kind, Kind::None);
        QVERIFY(!detection.waiting());
        QVERIFY(bannerText(QStringLiteral("grep"), detection).isEmpty());
    }

    void aptWhileDownloadingIsNotWaiting() {
        // The question scrolled up three rows ago and was already answered.
        QCOMPARE(detect(rowsOf(QStringLiteral("apt-downloading.txt")), asking()).kind, Kind::None);
    }

    void shellPromptIsNotAProgramQuestion() {
        const Detection detection = detect(rowsOf(QStringLiteral("shell-prompt.txt")), idleShell());
        QCOMPARE(detection.kind, Kind::ShellPrompt);
        QVERIFY(!detection.waiting());
        QVERIFY(!detection.actionable());
    }

    void fullScreenProgramsAreNeverLinePrompts() {
        // git rebase -i opens the todo list in an editor on the alternate screen. The take-control
        // button covers that; "the last line" means nothing in a full-screen interface.
        Signals sig = asking();
        sig.altScreen = true;
        QCOMPARE(detect(rowsOf(QStringLiteral("git-rebase-todo.txt")), sig).kind, Kind::None);
        // Without the alternate screen the same text is still not a question.
        QCOMPARE(detect(rowsOf(QStringLiteral("git-rebase-todo.txt")), asking()).kind, Kind::None);
    }

    void aQuestionWithNoProgramRunningIsNotActedOn() {
        // `cat` of a script that happens to end with a prompt line, back at the shell.
        Signals sig = idleShell();
        const Detection detection = detect(rowsOf(QStringLiteral("apt-continue.txt")), sig);
        QCOMPARE(detection.kind, Kind::YesNo);
        QVERIFY(!detection.actionable());   // confidence is docked for "nothing is running"
    }

    void aPrintingProgramIsNotWaiting() {
        // A program in raw mode writing a line that ends in a colon is not asking for a line.
        Signals sig;
        sig.mode = TerminalMode::Raw;
        sig.programRunning = true;
        const Detection detection = detect({QStringLiteral("Cloning into 'relay':")}, sig);
        QCOMPARE(detection.kind, Kind::FreeText);
        QVERIFY(!detection.actionable());
    }

    // ----- the signal-only fallback (a backend without ScreenText) ----------------------------
    void withoutScreenTextTheProcSignalsStillDecide() {
        Signals blind = asking(true);
        blind.screenReadable = false;
        const Detection detection = detect({}, blind);
        QCOMPARE(detection.kind, Kind::FreeText);
        QVERIFY(detection.actionable());
        QCOMPARE(bannerText(QStringLiteral("apt"), detection), QStringLiteral("apt is asking for input"));

        Signals blindSecret = secret();
        blindSecret.screenReadable = false;
        const Detection password = detect({}, blindSecret);
        QCOMPARE(password.kind, Kind::Password);
        QVERIFY(password.masked);

        // Echo on, nothing blocked in read(): the old behaviour, which is "do not guess".
        Signals quiet = asking();
        quiet.screenReadable = false;
        QCOMPARE(detect({}, quiet).kind, Kind::None);
    }

    // ----- housekeeping -----------------------------------------------------------------------
    void lastRowsTakesTheBottomOfTheScreen() {
        const QString screen = QStringLiteral("a\nb\nc\nd");
        QCOMPARE(lastRows(screen, 2), QStringList({QStringLiteral("c"), QStringLiteral("d")}));
        QCOMPARE(lastRows(screen, 99).size(), 4);
        QVERIFY(lastRows(QString(), 4).isEmpty());
        QVERIFY(lastRows(QStringLiteral("\n  \n\n"), 4).isEmpty());
    }

    void aTallScreenIsMostlyEmptyAndThatIsNotTheBottom() {
        // The real case this exists for: a 46-row pane with a three-row command in it. Taking
        // the literal last eight rows gives eight blank ones and the question is missed.
        QString screen = fixture(QStringLiteral("apt-continue.txt"));
        for (int i = 0; i < 40; ++i) screen += QStringLiteral("\n") + QString(80, QLatin1Char(' '));
        const Detection detection = detect(lastRows(screen), asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QVERIFY(detection.actionable());
    }

    void escapeSequencesAndControlBytesAreDropped() {
        const Detection detection = detect({QStringLiteral("\x1B[1;32mDo you want to continue?\x1B[0m [Y/n] \x07")},
                                           asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QCOMPARE(detection.question, QStringLiteral("Do you want to continue? [Y/n]"));
    }

    void aVeryLongQuestionIsElidedFromTheLeft() {
        const Detection detection = detect({QString(400, QLatin1Char('x')) + QStringLiteral(" ? [Y/n] ")}, asking());
        QCOMPARE(detection.kind, Kind::YesNo);
        QCOMPARE(detection.question.size(), kQuestionChars);
        QVERIFY(detection.question.startsWith(QChar(u'…')));
        QVERIFY(detection.question.endsWith(QStringLiteral("[Y/n]")));
    }

    void blankRowsBelowTheQuestionAreIgnored() {
        QStringList rows = rowsOf(QStringLiteral("apt-continue.txt"));
        rows << QString() << QStringLiteral("   ") << QString();
        QCOMPARE(detect(rows, asking()).kind, Kind::YesNo);
    }

    void kindNamesAreStable() {
        QCOMPARE(QString::fromLatin1(kindName(Kind::YesNo)), QStringLiteral("yes_no"));
        QCOMPARE(QString::fromLatin1(kindName(Kind::Password)), QStringLiteral("password"));
        QCOMPARE(QString::fromLatin1(kindName(Kind::None)), QStringLiteral("none"));
    }

    void waitingLineTellsTheUserWhatEnterDoes() {
        const Detection yesNo = detect(rowsOf(QStringLiteral("apt-continue.txt")), asking(true));
        QVERIFY(waitingLine(QStringLiteral("apt"), yesNo).endsWith(QStringLiteral("Enter sends your answer to it")));
        const Detection password = detect(rowsOf(QStringLiteral("sudo-password.txt")), secret());
        QVERIFY(waitingLine(QStringLiteral("sudo"), password).contains(QStringLiteral("never reaches Relay")));
        const Detection nothing = detect(rowsOf(QStringLiteral("shell-prompt.txt")), idleShell());
        QVERIFY(waitingLine(QStringLiteral("bash"), nothing).isEmpty());
    }
};

QTEST_APPLESS_MAIN(ScreenPromptTests)
#include "screenprompt_test.moc"
