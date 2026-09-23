// SPDX-License-Identifier: AGPL-3.0-or-later
// Read aloud (card #MDA7): what of an agent reply's Markdown is said, where a long reply is cut
// into pieces, which command-line speaker runs and how, and — with a fake speaker on a fake PATH —
// that the pieces are spoken in order and that Stop ends the running one. No sound card involved.
#include "Speech.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifndef Q_OS_WIN
#include <signal.h>
#include <sys/types.h>
#endif

using namespace relay::speech;

namespace {

// An executable shell script named `name` in `dir`.
void writeTool(const QString &dir, const QString &name, const QByteArray &body) {
    QFile file(QDir(dir).filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("#!/bin/sh\n" + body);
    file.close();
    file.setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeUser);
}

// The pid a fake speaker wrote, or 0 while it has not written one yet.
qint64 readPid(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    return file.readAll().trimmed().toLongLong();
}

// PATH set to one directory for the life of the object, and put back afterwards.
struct FakePath {
    explicit FakePath(const QString &dir) : saved(qgetenv("PATH")) { qputenv("PATH", dir.toLocal8Bit()); }
    ~FakePath() { qputenv("PATH", saved); }
    QByteArray saved;
};

}  // namespace

class SpeechTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ----- what is said -------------------------------------------------------------------------
    void aReplyIsReadAsProse() {
        const QString markdown = QStringLiteral(
            "## Summary\n"
            "\n"
            "I changed **two** files and ran the _tests_.\n"
            "The fix is in `src/Pane.h`, see [the card](https://example.com/card) and https://example.com/x.\n"
            "\n"
            "- first item\n"
            "- second item, done!\n"
            "1. numbered *one*\n"
            "- [x] a ticked task\n"
            "\n"
            "```cpp\n"
            "int main() { return 0; }\n"
            "```\n"
            "\n"
            "![a chart](chart.png)\n"
            "\n"
            "| File | Lines |\n"
            "|------|------:|\n"
            "| `a.cpp` | 12 |\n"
            "\n"
            "---\n"
            "> Quoted ~~old~~ text\n"
            "\n"
            "~~~\n"
            "second code block\n"
            "~~~\n"
            "Done.\n");
        QCOMPARE(speakableText(markdown), QStringLiteral(
            "Summary.\n"
            "I changed two files and ran the tests. The fix is in src/Pane.h, see the card and.\n"
            "first item.\n"
            "second item, done!\n"
            "numbered one.\n"
            "a ticked task.\n"
            "Code block omitted.\n"
            "File, Lines.\n"
            "a.cpp, 12.\n"
            "Quoted old text\n"
            "Done."));
    }

    void identifiersAndStraySymbolsSurvive() {
        // Underscores inside words are not emphasis, a lone asterisk is arithmetic, and inline code
        // keeps what emphasis would have eaten.
        QCOMPARE(speakableText(QStringLiteral("Set my_var_name to 2 * 3 and run `__init__`.")),
                 QStringLiteral("Set my_var_name to 2 * 3 and run __init__."));
        // A link's text stays; its URL, an autolink and an HTML tag go.
        QCOMPARE(speakableText(QStringLiteral("Read [the docs][1] <https://x.org> now.<br>\n\n[1]: https://x.org/docs")),
                 QStringLiteral("Read the docs now."));
        // A heading keeps its own punctuation; a setext heading is a heading too.
        QCOMPARE(speakableText(QStringLiteral("# Done?\nTitle\n=====\nbody")),
                 QStringLiteral("Done?\nTitle.\nbody"));
    }

    void codeBlockOmittedIsSaidOncePerReply() {
        QCOMPARE(speakableText(QStringLiteral("```\na\n```\n```\nb\n```\nafter")),
                 QStringLiteral("Code block omitted.\nafter"));
        // An unterminated fence swallows the rest, as a Markdown renderer would.
        QCOMPARE(speakableText(QStringLiteral("before\n```sh\nrm -rf build")),
                 QStringLiteral("before\nCode block omitted."));
        QCOMPARE(speakableText(QStringLiteral("```\nonly code\n```")), QStringLiteral("Code block omitted."));
        QCOMPARE(speakableText(QString()), QString());
    }

    // ----- chunking -----------------------------------------------------------------------------
    void piecesAreSentencesAndNeverCrossALine() {
        QCOMPARE(chunks(QStringLiteral("First one. Second one! Third?\nA new line without a stop")),
                 (QStringList{QStringLiteral("First one."), QStringLiteral("Second one!"), QStringLiteral("Third?"),
                              QStringLiteral("A new line without a stop")}));
        // File names, versions and abbreviations do not end a sentence.
        QCOMPARE(chunks(QStringLiteral("Edit src/Pane.h in Qt 6.8, e.g. the header. Then build.")),
                 (QStringList{QStringLiteral("Edit src/Pane.h in Qt 6.8, e.g. the header."), QStringLiteral("Then build.")}));
        // Closing quotes and brackets stay with their sentence.
        QCOMPARE(chunks(QStringLiteral("He said \"stop.\" (It did.) Next")),
                 (QStringList{QStringLiteral("He said \"stop.\""), QStringLiteral("(It did.)"), QStringLiteral("Next")}));
        QVERIFY(chunks(QString()).isEmpty());
        QVERIFY(chunks(QStringLiteral("\n  \n")).isEmpty());
    }

    void aLongSentenceIsCutAtASpace() {
        QCOMPARE(chunks(QStringLiteral("one two three four five six seven eight nine ten eleven"), 20),
                 (QStringList{QStringLiteral("one two three four"), QStringLiteral("five six seven eight"),
                              QStringLiteral("nine ten eleven")}));
        // No space to cut at: cut at the limit.
        QCOMPARE(chunks(QString(45, QLatin1Char('x')), 20),
                 (QStringList{QString(20, QLatin1Char('x')), QString(20, QLatin1Char('x')), QString(5, QLatin1Char('x'))}));
    }

    // ----- command-line speakers ----------------------------------------------------------------
    void toolsAreTriedInPreferenceOrder() {
        QCOMPARE(cliTools(Os::Linux), (QStringList{QStringLiteral("spd-say"), QStringLiteral("espeak-ng"), QStringLiteral("espeak")}));
        QCOMPARE(cliTools(Os::Mac).first(), QStringLiteral("say"));
        QCOMPARE(cliTools(Os::Windows), QStringList{QStringLiteral("powershell")});
        auto only = [](const QStringList &present) { return [present](const QString &tool) { return present.contains(tool); }; };
        QCOMPARE(chooseTool(only({QStringLiteral("espeak"), QStringLiteral("espeak-ng")}), Os::Linux), QStringLiteral("espeak-ng"));
        QCOMPARE(chooseTool(only({QStringLiteral("say")}), Os::Linux), QString());
        QCOMPARE(chooseTool(only({QStringLiteral("say"), QStringLiteral("espeak")}), Os::Mac), QStringLiteral("say"));
        QCOMPARE(chooseTool({}, Os::Linux), QString());
    }

    void theToolOnAFakePathIsChosen() {
#ifdef Q_OS_WIN
        QSKIP("shell-script fakes");
#endif
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        FakePath path(dir.path());
        QCOMPARE(chooseTool(toolOnPath, Os::Linux), QString());
        writeTool(dir.path(), QStringLiteral("espeak"), "exit 0\n");
        QCOMPARE(chooseTool(toolOnPath, Os::Linux), QStringLiteral("espeak"));
        writeTool(dir.path(), QStringLiteral("espeak-ng"), "exit 0\n");
        QCOMPARE(chooseTool(toolOnPath, Os::Linux), QStringLiteral("espeak-ng"));
        writeTool(dir.path(), QStringLiteral("spd-say"), "exit 0\n");
        QCOMPARE(chooseTool(toolOnPath, Os::Linux), QStringLiteral("spd-say"));
    }

    void textNeverGoesThroughAShell() {
        const QString text = QStringLiteral("-rf $(reboot); `id` & 'quoted' \"é\"");
        // spd-say: one argument after "--", so a leading "-" is not an option.
        const Invocation spd = speakInvocation(QStringLiteral("spd-say"), text);
        QCOMPARE(spd.arguments, (QStringList{QStringLiteral("-w"), QStringLiteral("-N"), QStringLiteral("relay"), QStringLiteral("--"), text}));
        QVERIFY(spd.input.isEmpty());
        // The others read it on stdin, as UTF-8; the command line never holds it.
        for (const QString &tool : {QStringLiteral("espeak-ng"), QStringLiteral("espeak"), QStringLiteral("say"), QStringLiteral("powershell")}) {
            const Invocation call = speakInvocation(tool, text);
            QVERIFY2(!call.arguments.isEmpty(), qPrintable(tool));
            QCOMPARE(call.input, text.toUtf8());
            for (const QString &argument : call.arguments) QVERIFY2(!argument.contains(QStringLiteral("reboot")), qPrintable(tool));
        }
        QCOMPARE(speakInvocation(QStringLiteral("espeak-ng"), text).arguments, QStringList{QStringLiteral("--stdin")});
        QCOMPARE(speakInvocation(QStringLiteral("say"), text).arguments, (QStringList{QStringLiteral("-f"), QStringLiteral("-")}));
        QVERIFY(speakInvocation(QStringLiteral("festival"), text).arguments.isEmpty());
        QCOMPARE(stopArguments(QStringLiteral("spd-say")), QStringList{QStringLiteral("-S")});
        QVERIFY(stopArguments(QStringLiteral("espeak-ng")).isEmpty());
    }

    // ----- speaking, with a fake speaker ---------------------------------------------------------
    void piecesAreSpokenInOrder() {
#ifdef Q_OS_WIN
        QSKIP("shell-script fakes");
#endif
        QTemporaryDir dir;
        const QString log = dir.filePath(QStringLiteral("spoken.txt"));
        writeTool(dir.path(), QStringLiteral("espeak-ng"), "/bin/cat >> '" + log.toUtf8() + "'\necho >> '" + log.toUtf8() + "'\n");
        FakePath path(dir.path());
        Speaker &speaker = Speaker::instance();
        speaker.setToolForTesting(QStringLiteral("espeak-ng"));
        QObject owner;
        QSignalSpy changed(&speaker, &Speaker::stateChanged);
        QString error;
        QVERIFY2(speaker.speak(QStringLiteral("# Result\nIt **worked**. Twice!\n```\ncode\n```"), &owner, &error), qPrintable(error));
        QVERIFY(speaker.speaking());
        QCOMPARE(speaker.owner(), &owner);
        QTRY_VERIFY_WITH_TIMEOUT(!speaker.speaking(), 10000);
        QCOMPARE(changed.count(), 2);   // started, finished
        QVERIFY(!speaker.owner());
        QFile file(log);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()),
                 QStringLiteral("Result.\nIt worked.\nTwice!\nCode block omitted.\n"));
        // Nothing to say is an error, not an utterance.
        QVERIFY(!speaker.speak(QStringLiteral("  \n"), &owner, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!speaker.speaking());
        speaker.setToolForTesting(QString());
    }

    void stopEndsTheRunningPieceAndANewUtteranceReplacesTheOld() {
#ifdef Q_OS_WIN
        QSKIP("shell-script fakes");
#else
        QTemporaryDir dir;
        const QString pidFile = dir.filePath(QStringLiteral("pid"));
        // Records its pid, then becomes a long sleep: a piece that would never end by itself.
        writeTool(dir.path(), QStringLiteral("espeak-ng"),
                  "echo $$ > '" + pidFile.toUtf8() + "'\nexec /bin/sleep 30\n");
        FakePath path(dir.path());
        Speaker &speaker = Speaker::instance();
        speaker.setToolForTesting(QStringLiteral("espeak-ng"));
        QObject first, second;
        QVERIFY(speaker.speak(QStringLiteral("One. Two."), &first));
        pid_t firstPid = 0;
        QTRY_VERIFY_WITH_TIMEOUT((firstPid = pid_t(readPid(pidFile))) > 0, 5000);
        QVERIFY(firstPid > 0);
        QFile::remove(pidFile);

        // A second utterance, from another pane, stops the first one.
        QVERIFY(speaker.speak(QStringLiteral("Three."), &second));
        QCOMPARE(speaker.owner(), &second);
        QTRY_VERIFY_WITH_TIMEOUT(::kill(firstPid, 0) != 0, 5000);
        pid_t secondPid = 0;
        QTRY_VERIFY_WITH_TIMEOUT((secondPid = pid_t(readPid(pidFile))) > 0, 5000);
        QVERIFY(secondPid > 0 && secondPid != firstPid);

        // Stop: not speaking at once, and the process is gone; "Two." never starts.
        QElapsedTimer timer;
        timer.start();
        speaker.stop();
        QVERIFY(!speaker.speaking());
        QVERIFY(!speaker.owner());
        QTRY_VERIFY_WITH_TIMEOUT(::kill(secondPid, 0) != 0, 5000);
        QVERIFY(timer.elapsed() < 5000);
        QFile::remove(pidFile);
        QTest::qWait(300);
        QVERIFY(!QFile::exists(pidFile));
        speaker.setToolForTesting(QString());
#endif
    }

    void aHungStopHelperIsKilled() {
#ifdef Q_OS_WIN
        QSKIP("shell-script fakes");
#else
        // A fake spd-say: speaking is a long sleep, and `-S` records its pid and hangs, the way a
        // real one did against a daemon whose runtime directory had been removed.
        QTemporaryDir dir;
        const QString stopPid = dir.filePath(QStringLiteral("stop-pid"));
        writeTool(dir.path(), QStringLiteral("spd-say"),
                  "if [ \"$1\" = -S ]; then echo $$ > '" + stopPid.toUtf8() + "'; exec /bin/sleep 30; fi\n"
                  "exec /bin/sleep 30\n");
        FakePath path(dir.path());
        Speaker &speaker = Speaker::instance();
        speaker.setToolForTesting(QStringLiteral("spd-say"));
        QObject owner;
        QVERIFY(speaker.speak(QStringLiteral("Hello."), &owner));
        QTest::qWait(300);
        speaker.stop();
        QVERIFY(!speaker.speaking());
        pid_t helper = 0;
        QTRY_VERIFY_WITH_TIMEOUT((helper = pid_t(readPid(stopPid))) > 0, 5000);
        QVERIFY(::kill(helper, 0) == 0);                              // told to stop, and hanging
        QTRY_VERIFY_WITH_TIMEOUT(::kill(helper, 0) != 0, 5000);       // killed at the deadline
        speaker.setToolForTesting(QString());
#endif
    }
};

QTEST_GUILESS_MAIN(SpeechTests)
#include "speech_test.moc"
