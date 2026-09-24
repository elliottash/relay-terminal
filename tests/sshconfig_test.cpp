// SPDX-License-Identifier: AGPL-3.0-or-later
// SSH hosts for "Connect to host…" and the command line "Split on the same host" re-runs
// (card #S5SH, docs/SSH-AND-MOSH.md section 8). Every test writes its own ~/.ssh into a
// temporary directory; nothing reads the real one.
#include "SshConfig.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::ssh;

namespace {

void write(const QString &path, const QByteArray &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
}

QStringList aliases(const QList<Host> &hosts) {
    QStringList out;
    for (const Host &host : hosts) out << host.alias;
    return out;
}

}  // namespace

class SshConfigTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void listsConcreteHostsInOrder() {
        QTemporaryDir home;
        const QString dir = home.path() + QStringLiteral("/.ssh");
        write(dir + QStringLiteral("/config"),
              "# my hosts\n"
              "Host filly  box2 # two names\n"
              "    HostName 65.109.126.152\n"
              "    User elliott\n"
              "  Port=2222\n"
              "Host *.internal web-? !bastion\n"
              "    User nobody\n"
              "host=Mixed\n"
              "  HOSTNAME = mixed.example.org\n"
              "Host filly\n"
              "    HostName other.example.org\n"
              "    User later\n"
              "Host \"quoted name\" plain\n"
              "Match host filly exec \"true\"\n"
              "    User matchuser\n"
              "    Port 99\n"
              "Host *\n"
              "    ServerAliveInterval 30\n");
        const QList<Host> hosts = parseConfig(dir + QStringLiteral("/config"), dir, home.path());
        QCOMPARE(aliases(hosts), QStringList({QStringLiteral("filly"), QStringLiteral("box2"), QStringLiteral("Mixed"),
                                              QStringLiteral("quoted name"), QStringLiteral("plain")}));
        // First value wins, as in ssh; a Match block's settings are not attributed to anyone.
        QCOMPARE(hosts.at(0).hostName, QStringLiteral("65.109.126.152"));
        QCOMPARE(hosts.at(0).user, QStringLiteral("elliott"));
        QCOMPARE(hosts.at(0).port, QStringLiteral("2222"));
        QCOMPARE(hosts.at(0).detail(), QStringLiteral("elliott@65.109.126.152:2222"));
        QCOMPARE(hosts.at(1).detail(), QStringLiteral("elliott@65.109.126.152:2222"));
        QCOMPARE(hosts.at(2).detail(), QStringLiteral("mixed.example.org"));
        QVERIFY(hosts.at(4).detail().isEmpty());
    }

    void followsIncludes() {
        QTemporaryDir home;
        const QString dir = home.path() + QStringLiteral("/.ssh");
        write(dir + QStringLiteral("/config"),
              "Include config.d/*.conf ~/extra/ssh.conf /nonexistent/*\n"
              "Host top\n"
              "Include loop\n");
        write(dir + QStringLiteral("/config.d/b.conf"), "Host bee\n  User b\n");
        write(dir + QStringLiteral("/config.d/a.conf"), "Host ay\nInclude nested\n");
        write(dir + QStringLiteral("/config.d/ignored.txt"), "Host notme\n");
        write(dir + QStringLiteral("/nested"), "Host deep\n  Port 7\n");
        write(home.path() + QStringLiteral("/extra/ssh.conf"), "Host fromhome\n");
        // A file that includes itself and the top config: read once each, no hang.
        write(dir + QStringLiteral("/loop"), "Host looped\nInclude loop config\n");
        const QList<Host> hosts = parseConfig(dir + QStringLiteral("/config"), dir, home.path());
        QCOMPARE(aliases(hosts), QStringList({QStringLiteral("ay"), QStringLiteral("deep"), QStringLiteral("bee"),
                                              QStringLiteral("fromhome"), QStringLiteral("top"), QStringLiteral("looped")}));
        QCOMPARE(hosts.at(1).port, QStringLiteral("7"));
        QCOMPARE(hosts.at(2).detail(), QStringLiteral("b@bee"));
    }

    void capsIncludeDepth() {
        QTemporaryDir home;
        const QString dir = home.path() + QStringLiteral("/.ssh");
        // A chain of 30 distinct files: past depth 16 nothing more is read.
        for (int i = 0; i < 30; ++i)
            write(dir + QStringLiteral("/f%1").arg(i), QStringLiteral("Host h%1\nInclude f%2\n").arg(i).arg(i + 1).toUtf8());
        const QList<Host> hosts = parseConfig(dir + QStringLiteral("/f0"), dir, home.path());
        QCOMPARE(hosts.size(), 17);
        QCOMPARE(hosts.last().alias, QStringLiteral("h16"));
    }

    void missingConfigIsEmpty() {
        QTemporaryDir home;
        QVERIFY(parseConfig(home.path() + QStringLiteral("/.ssh/config"), home.path() + QStringLiteral("/.ssh"), home.path()).isEmpty());
    }

    void keepsRecentHostsMostRecentFirst() {
        QStringList recent;
        recent = withRecent(recent, QStringLiteral("a"));
        recent = withRecent(recent, QStringLiteral("b"));
        recent = withRecent(recent, QStringLiteral(" a "));
        QCOMPARE(recent, QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        for (int i = 0; i < 30; ++i) recent = withRecent(recent, QStringLiteral("h%1").arg(i));
        QCOMPARE(recent.size(), kRecentLimit);
        QCOMPARE(recent.first(), QStringLiteral("h29"));
        QCOMPARE(withRecent(recent, QString()), recent);
    }

    void quotesForTheShell() {
        QCOMPARE(shellQuote(QStringLiteral("elliott@filly")), QStringLiteral("elliott@filly"));
        QCOMPARE(shellQuote(QStringLiteral("a b")), QStringLiteral("'a b'"));
        QCOMPARE(shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
        QCOMPARE(shellQuote(QStringLiteral("$(rm -rf ~)")), QStringLiteral("'$(rm -rf ~)'"));
        QCOMPARE(shellQuote(QString()), QStringLiteral("''"));
        QCOMPARE(connectCommand(QStringLiteral("quoted name")), QStringLiteral("ssh 'quoted name'"));
    }

    void readsATypedTarget() {
        QCOMPARE(typedTarget(QStringLiteral("elliott@filly")), QStringLiteral("elliott@filly"));
        QCOMPARE(typedTarget(QStringLiteral("ssh root@10.0.0.2")), QStringLiteral("root@10.0.0.2"));
        QCOMPARE(typedTarget(QStringLiteral("SSH newbox")), QStringLiteral("newbox"));
        // Ordinary searches never become a host.
        QVERIFY(typedTarget(QStringLiteral("newbox")).isEmpty());
        QVERIFY(typedTarget(QStringLiteral("split pane")).isEmpty());
        QVERIFY(typedTarget(QStringLiteral("ssh")).isEmpty());
        QVERIFY(typedTarget(QStringLiteral("ssh -oProxyCommand=x host")).isEmpty());
        QVERIFY(typedTarget(QStringLiteral("ssh a;b")).isEmpty());
    }

    void rebuildsAnSshCommandLine() {
        QString host;
        QCOMPARE(rerunCommand({QStringLiteral("ssh"), QStringLiteral("filly")}, &host), QStringLiteral("ssh filly"));
        QCOMPARE(host, QStringLiteral("filly"));
        // Quoting survives: the remote command is one argv word with spaces.
        QCOMPARE(rerunCommand({QStringLiteral("/usr/bin/ssh"), QStringLiteral("-p"), QStringLiteral("2222"),
                               QStringLiteral("-tA"), QStringLiteral("me@box"), QStringLiteral("cd /srv && tmux a")}, &host),
                 QStringLiteral("ssh -p 2222 -tA me@box 'cd /srv && tmux a'"));
        QCOMPARE(host, QStringLiteral("box"));
        // The wrapper's connection sharing comes out; the new pane's wrapper adds it back.
        QCOMPARE(rerunCommand({QStringLiteral("ssh"), QStringLiteral("-o"), QStringLiteral("ControlMaster=auto"),
                               QStringLiteral("-o"), QStringLiteral("ControlPath=/run/user/1000/relay-ssh/%C"),
                               QStringLiteral("-oControlPersist=600"), QStringLiteral("-v"), QStringLiteral("filly")}),
                 QStringLiteral("ssh -v filly"));
        // The user's own ControlMaster is theirs and stays.
        QCOMPARE(rerunCommand({QStringLiteral("ssh"), QStringLiteral("-o"), QStringLiteral("ControlMaster=auto"), QStringLiteral("filly")}),
                 QStringLiteral("ssh -o ControlMaster=auto filly"));
        // Not logins.
        QVERIFY(rerunCommand({QStringLiteral("ssh"), QStringLiteral("-O"), QStringLiteral("check"), QStringLiteral("filly")}).isEmpty());
        QVERIFY(rerunCommand({QStringLiteral("ssh"), QStringLiteral("-NL"), QStringLiteral("8080:localhost:80"), QStringLiteral("filly")}).isEmpty());
        QVERIFY(rerunCommand({QStringLiteral("ssh"), QStringLiteral("-G"), QStringLiteral("filly")}).isEmpty());
        QVERIFY(rerunCommand({QStringLiteral("ssh")}).isEmpty());
        // Only ssh and mosh are ever re-run.
        QVERIFY(rerunCommand({QStringLiteral("telnet"), QStringLiteral("box")}).isEmpty());
        QVERIFY(rerunCommand({QStringLiteral("rm"), QStringLiteral("-rf"), QStringLiteral("/")}).isEmpty());
        QVERIFY(rerunCommand({}).isEmpty());
    }

    void rebuildsAMoshCommandLine() {
        QString host;
        QCOMPARE(rerunCommand({QStringLiteral("/usr/bin/perl"), QStringLiteral("/usr/bin/mosh"), QStringLiteral("me@box")}, &host),
                 QStringLiteral("mosh me@box"));
        QCOMPARE(host, QStringLiteral("box"));
        QCOMPARE(rerunCommand({QStringLiteral("mosh-client"), QStringLiteral("-# --predict=always me@box |"),
                               QStringLiteral("10.0.0.2"), QStringLiteral("60001")}, &host),
                 QStringLiteral("mosh --predict=always me@box"));
        QCOMPARE(host, QStringLiteral("box"));
        // The wrapper's --ssh, as mosh-client shows it (joined with spaces) and as mosh's own argv.
        QCOMPARE(rerunCommand({QStringLiteral("mosh-client"),
                               QStringLiteral("-# --ssh=ssh -o ControlMaster=auto -o ControlPath=/run/user/1/relay-ssh/%C "
                                              "-o ControlPersist=600 --experimental-remote-ip=remote box |"),
                               QStringLiteral("10.0.0.2"), QStringLiteral("60001")}),
                 QStringLiteral("mosh box"));
        QCOMPARE(rerunCommand({QStringLiteral("perl"), QStringLiteral("/usr/bin/mosh"),
                               QStringLiteral("--ssh=ssh -o ControlMaster=auto -o ControlPath=/run/user/1/relay-ssh/%C"),
                               QStringLiteral("--experimental-remote-ip=remote"), QStringLiteral("box")}),
                 QStringLiteral("mosh box"));
        // The user's own --ssh and address mode stay.
        QCOMPARE(rerunCommand({QStringLiteral("mosh"), QStringLiteral("--ssh=ssh -p 2222"),
                               QStringLiteral("--experimental-remote-ip=remote"), QStringLiteral("box")}),
                 QStringLiteral("mosh '--ssh=ssh -p 2222' --experimental-remote-ip=remote box"));
        QVERIFY(rerunCommand({QStringLiteral("mosh-client"), QStringLiteral("10.0.0.2"), QStringLiteral("60001")}).isEmpty());
    }

    void readsItsOwnArgv() {
        const QStringList argv = processArgv(int(QCoreApplication::applicationPid()));
        QVERIFY(!argv.isEmpty());
        QVERIFY(argv.first().endsWith(QStringLiteral("sshconfig-tests")));
        QVERIFY(processArgv(0).isEmpty());
    }

    // "Connect to host (persistent)…" (card #VD2M): mosh when this machine has it, ssh -t
    // otherwise, and on the host zellij attach --create, or tmux attach-or-create, or a login
    // shell. The host script travels as one single-quoted word.
    void persistentCommandBuildsTheFallingChain() {
        const QString mosh = persistentCommand(QStringLiteral("filly"), true);
        QVERIFY(mosh.startsWith(QStringLiteral("mosh filly -- sh -c '")));
        QVERIFY(mosh.contains(QStringLiteral("zellij attach --create relay-filly")));
        QVERIFY(mosh.contains(QStringLiteral("tmux new -A -s relay-filly")));
        QVERIFY(mosh.contains(QStringLiteral("exec \"$SHELL\" -l'")));
        QCOMPARE(mosh.count(QLatin1Char('\'')), 2);   // only the host script's own quotes
        const QString ssh = persistentCommand(QStringLiteral("quoted name"), false);
        QVERIFY(ssh.startsWith(QStringLiteral("ssh -t 'quoted name' sh -c '")));
        QVERIFY(ssh.contains(QStringLiteral("relay-quotedname")));
        QCOMPARE(ssh.count(QLatin1Char('\'')), 4);   // host name and host script
    }

    void persistentSessionKeepsOneNamePerUserAndHost() {
        QCOMPARE(persistentSession(QStringLiteral("filly")), QStringLiteral("relay-filly"));
        QCOMPARE(persistentSession(QStringLiteral("elliott@box.example.com:2222")),
                 QStringLiteral("relay-elliott-box.example.com2222"));
        QCOMPARE(persistentSession(QStringLiteral("we! rd")), QStringLiteral("relay-werd"));
        QCOMPARE(persistentSession(QString()), QStringLiteral("relay-relay"));
    }
};

QTEST_MAIN(SshConfigTests)
#include "sshconfig_test.moc"
