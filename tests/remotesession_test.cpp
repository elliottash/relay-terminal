// SPDX-License-Identifier: AGPL-3.0-or-later
// SSH and mosh sessions (card #S5SH): which `ssh -G` arguments describe the running login, what
// the dump says, and that the typed bootstrap line decodes, with a row count that fits.
#include "RemoteSession.h"

#include <QLocalServer>
#include <QProcess>
#include <QTemporaryDir>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <QStandardPaths>
#include <QTest>

using namespace relay::remote;

class RemoteSessionTest : public QObject {
    Q_OBJECT
private slots:
    void sshArguments() {
        QCOMPARE(dumpArguments({"ssh", "filly"}), (QStringList{"-G", "filly"}));
        QCOMPARE(dumpArguments({"/usr/bin/ssh", "-t", "-p", "2222", "me@box", "uptime"}),
                 (QStringList{"-G", "-t", "-p", "2222", "me@box"}));
        QCOMPARE(dumpArguments({"ssh", "-tt", "-p22", "-oBatchMode=yes", "box", "--", "ls"}),
                 (QStringList{"-G", "-tt", "-p22", "-oBatchMode=yes", "box"}));
        QCOMPARE(dumpArguments({"ssh", "-4o", "ControlMaster=no", "box"}), (QStringList{"-G", "-4o", "ControlMaster=no", "box"}));
        QCOMPARE(dumpArguments({"ssh", "-o", "ControlMaster=auto", "-o", "ControlPath=/run/r/%C", "filly"}),
                 (QStringList{"-G", "-o", "ControlMaster=auto", "-o", "ControlPath=/run/r/%C", "filly"}));
        QCOMPARE(dumpArguments({"ssh", "--", "box"}), (QStringList{"-G", "box"}));
        QVERIFY(dumpArguments({"ssh", "-V"}).isEmpty());
        QVERIFY(dumpArguments({}).isEmpty());
        QCOMPARE(destination({"ssh", "-l", "me", "box", "ls"}), QStringLiteral("box"));
    }

    void moshArguments() {
        // No --ssh of its own: mosh ran the wrapper's ssh, whose socket is in Relay's directory.
        QCOMPARE(dumpArguments({"mosh", "filly"}, "/run/user/1/relay-ssh"),
                 (QStringList{"-G", "-o", "ControlPath=/run/user/1/relay-ssh/%C", "filly"}));
        QCOMPARE(dumpArguments({"mosh", "--ssh=ssh -p 2222", "-p", "60001", "me@box", "tmux"}, "/r"),
                 (QStringList{"-G", "-p", "2222", "-o", "ControlPath=/r/%C", "me@box"}));
        QCOMPARE(dumpArguments({"mosh", "--ssh", "ssh -o ControlPath=/mine/%C", "box"}, "/r"),
                 (QStringList{"-G", "-o", "ControlPath=/mine/%C", "box"}));
        // mosh-client carries the original arguments after -#.
        QCOMPARE(dumpArguments({"mosh-client", "-#",
                                "--ssh=ssh -o ControlMaster=auto -o ControlPath=/r/%C filly | 1.2.3.4 60001",
                                "1.2.3.4", "60001"}),
                 (QStringList{"-G", "-o", "ControlMaster=auto", "-o", "ControlPath=/r/%C", "filly"}));
        QCOMPARE(destination({"mosh-client", "-#", "filly | 1.2.3.4 60001", "1.2.3.4", "60001"}), QStringLiteral("filly"));
        // What mosh 1.4 actually runs (read from /proc): the flag and the words are one argument.
        QCOMPARE(dumpArguments({"mosh-client", "-# localhost -- sleep 20 |", "127.0.0.1", "60003"}),
                 (QStringList{"-G", "localhost"}));
        QCOMPARE(dumpArguments({"mosh-client", "-# --ssh=ssh -o ControlMaster=auto -o ControlPath=/r/%C "
                                "-o ControlPersist=600 --experimental-remote-ip=remote filly |", "127.0.0.1", "60001"}),
                 (QStringList{"-G", "-o", "ControlMaster=auto", "-o", "ControlPath=/r/%C", "-o", "ControlPersist=600", "filly"}));
        QVERIFY(dumpArguments({"mosh-client", "1.2.3.4", "60001"}).isEmpty());
    }

    void dump() {
        const QByteArray text = "user elliott\nhostname 65.109.126.152\nport 2222\ncontrolmaster auto\n"
                                "controlpath /run/user/1000/relay-ssh/956d\nforwardagent no\n";
        const Resolved r = parseDump(text, "me@filly");
        QVERIFY(r.ok);
        QCOMPARE(r.host, QStringLiteral("filly"));
        QCOMPARE(r.hostname, QStringLiteral("65.109.126.152"));
        QCOMPARE(r.user, QStringLiteral("elliott"));
        QCOMPARE(r.port, 2222);
        QCOMPARE(r.controlPath, QStringLiteral("/run/user/1000/relay-ssh/956d"));
        QCOMPARE(r.controlMaster, QStringLiteral("auto"));
        QVERIFY(parseDump("controlpath none\nhostname h\n").controlPath.isEmpty());
        QVERIFY(!parseDump("").ok);
    }

    void localHost() {
        QVERIFY(isLocalHost("", "spark"));
        QVERIFY(isLocalHost("localhost", "spark"));
        QVERIFY(isLocalHost("spark", "spark.lan"));
        QVERIFY(isLocalHost("SPARK.lan", "spark"));
        QVERIFY(!isLocalHost("filly", "spark"));
    }

    void rows() {
        QCOMPARE(rowsFor(0, 1, 80), 1);
        QCOMPARE(rowsFor(10, 70, 80), 1);   // ends exactly at the margin: still one row
        QCOMPARE(rowsFor(10, 71, 80), 2);
        QCOMPARE(rowsFor(18, 300, 80), 4);
        QCOMPARE(rowsFor(5, 10, 0), 1);
    }

    void promptEchoKeepsColourAndNothingElse() {
        int columns = -1;
        // A coloured bash prompt as the host drew it, with its OSC 133 marks and a title change.
        const QByteArray raw = "\x1b]0;elliott@filly\a\x1b]133;A\a\x1b[01;32melliott@filly\x1b[00m:"
                               "\x1b[01;34m~/src\x1b[00m$ \x1b]133;B\a";
        const QByteArray kept = promptEcho(raw, &columns);
        QCOMPARE(kept, QByteArray("\x1b[01;32melliott@filly\x1b[00m:\x1b[01;34m~/src\x1b[00m$ "));
        QCOMPARE(columns, QByteArray("elliott@filly:~/src$ ").size());
        QVERIFY(!kept.contains("133"));
        // Cursor moves, erases and other controls are dropped; their text is not.
        QCOMPARE(promptEcho("\x1b[2K\r\x1b[5Cabc\x1b[K\tdef\x08"), QByteArray("abcdef"));
        // A zsh prompt with a multi-byte character counts one column for it, not three.
        promptEcho(QString::fromUtf8("➜  code ").toUtf8(), &columns);
        QCOMPARE(columns, 8);
        QCOMPARE(promptEcho(""), QByteArray());
    }

    void pruningLeavesLiveSocketsAlone() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // One socket with a server on it, one left behind by a process that is gone, one file.
        QLocalServer live;
        QVERIFY(live.listen(dir.filePath("live")));
        const QByteArray stale = dir.filePath("stale").toLocal8Bit();
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        QVERIFY(fd >= 0);
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        ::strncpy(address.sun_path, stale.constData(), sizeof(address.sun_path) - 1);
        QCOMPARE(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
        ::close(fd);   // nothing listens on it now, and the file stays
        QFile plain(dir.filePath("notes.txt"));
        QVERIFY(plain.open(QIODevice::WriteOnly));
        plain.write("not a socket");
        plain.close();

        QCOMPARE(pruneSockets(dir.path()), 1);
        QVERIFY(QFile::exists(dir.filePath("live")));
        QVERIFY(!QFile::exists(dir.filePath("stale")));
        QVERIFY(QFile::exists(dir.filePath("notes.txt")));
        QCOMPARE(pruneSockets(dir.filePath("nowhere")), 0);
    }

    void bootstrapDecodes() {
        const QByteArray script = "echo relay-bootstrap-ok \"$RELAY_R\"\n";
        const QString line = bootstrapLine(script, 18, 80);
        // Nothing in front of `eval`: a shell that is not bash or zsh must be able to parse the
        // line, or it prints the payload back at the user (#S5SH).
        QVERIFY(line.startsWith(QStringLiteral(" eval \"$(printf")));
        QVERIFY(!line.contains(QStringLiteral("RELAY_R=")));   // it is inside the payload now
        if (QStandardPaths::findExecutable("bash").isEmpty() || QStandardPaths::findExecutable("gzip").isEmpty()
            || QStandardPaths::findExecutable("base64").isEmpty())
            QSKIP("bash, gzip and base64 are needed to decode the line");
        QProcess bash;
        bash.start("bash", {"--noprofile", "--norc", "-c", line});
        QVERIFY(bash.waitForFinished(5000));
        const QByteArray printed = bash.readAllStandardOutput().trimmed();
        QVERIFY2(printed.startsWith("relay-bootstrap-ok "), printed.constData());
        // The erase count the script was given is the one the line itself occupies.
        QCOMPARE(printed.mid(printed.lastIndexOf(' ') + 1).toInt(), rowsFor(18, line.size(), 80));
        // A larger script, so the deflate stream has more than one block to get right.
        QByteArray big;
        for (int i = 0; i < 400; ++i) big += "x=" + QByteArray::number(i * 7919) + "\n";
        big += "echo $x\n";
        QProcess again;
        again.start("bash", {"--noprofile", "--norc", "-c", bootstrapLine(big, 0, 120)});
        QVERIFY(again.waitForFinished(5000));
        QCOMPARE(again.readAllStandardOutput(), QByteArray::number(399 * 7919) + "\n");
    }

    void bootstrapMoshDecodes() {
        // #XQ8F: the mosh login carries RELAY_M the same way as RELAY_R — inside the payload,
        // where a shell that is neither bash nor zsh can still parse the typed line (#S5SH).
        const QByteArray script = "echo relay-mosh-ok \"$RELAY_R\" \"$RELAY_M\"\n";
        const QString plain = bootstrapLine(script, 18, 80);
        const QString mosh = bootstrapLine(script, 18, 80, true);
        QVERIFY(mosh.startsWith(QStringLiteral(" eval \"$(printf")));
        QVERIFY(!mosh.contains(QStringLiteral("RELAY_M=")));   // not typed in front of the eval
        QVERIFY(mosh.size() > plain.size());                   // the setting rides inside the payload
        if (QStandardPaths::findExecutable("bash").isEmpty() || QStandardPaths::findExecutable("gzip").isEmpty()
            || QStandardPaths::findExecutable("base64").isEmpty())
            QSKIP("bash, gzip and base64 are needed to decode the line");
        QProcess bash;
        bash.start("bash", {"--noprofile", "--norc", "-c", mosh});
        QVERIFY(bash.waitForFinished(5000));
        const QByteArray printed = bash.readAllStandardOutput().trimmed();
        QVERIFY2(printed.startsWith("relay-mosh-ok "), printed.constData());
        QVERIFY2(printed.endsWith(" 1"), printed.constData());   // RELAY_M reached the script
        // The row count settles on the mosh line's own, longer size.
        QCOMPARE(printed.mid(printed.indexOf(' ') + 1).split(' ').first().toInt(),
                 rowsFor(18, mosh.size(), 80));
    }
};

QTEST_GUILESS_MAIN(RemoteSessionTest)
#include "remotesession_test.moc"
