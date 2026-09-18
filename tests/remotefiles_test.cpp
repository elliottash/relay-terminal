// SPDX-License-Identifier: GPL-3.0-or-later
// Files on the host a pane is logged into (card #S5SH, docs/SSH-AND-MOSH.md section 9).
//
// The rules first, with no network and no process: how a path is quoted for the remote shell,
// what `stat -c %s:%Y:%a` parses to, when a save must refuse because the host's copy moved on,
// what the scripts say, and the `ssh://host/path` a pane hands to a preview pane.
//
// Then the QProcess half, against a fake `ssh` on PATH that writes down its own argv and its
// stdin: that the file's bytes go over stdin and never in an argument, that a fetch brings back
// the stat and the content, and that the batched probe asks once per path.
#include "RemoteFiles.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace relay::remote;

namespace {

bool writeFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(text);
    return true;
}

// `sh -c '<script>'` is what every remote command is wrapped in; this is the script back out.
QString scriptOf(const QStringList &argv)
{
    const QString last = argv.isEmpty() ? QString() : argv.last();
    const QString prefix = QStringLiteral("sh -c '");
    if (!last.startsWith(prefix) || !last.endsWith(QLatin1Char('\''))) return {};
    return last.mid(prefix.size(), last.size() - prefix.size() - 1).replace(QStringLiteral("'\\''"), QStringLiteral("'"));
}

}  // namespace

class RemoteFilesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_socket = QDir(m_dir.path()).filePath(QStringLiteral("socket"));
        QVERIFY(writeFile(m_socket, QByteArray()));
    }

    void cleanup()
    {
        if (!m_savedPath.isNull()) {
            qputenv("PATH", m_savedPath);
            m_savedPath = QByteArray();
        }
        forgetLogin(QStringLiteral("filly"));
    }

    // ----- quoting -------------------------------------------------------------------------

    void quotingSurvivesEveryPath()
    {
        QCOMPARE(shellQuote(QStringLiteral("/etc/nginx/nginx.conf")), QStringLiteral("'/etc/nginx/nginx.conf'"));
        QCOMPARE(shellQuote(QString()), QStringLiteral("''"));
        QCOMPARE(shellQuote(QStringLiteral("/tmp/two words")), QStringLiteral("'/tmp/two words'"));
        QCOMPARE(shellQuote(QStringLiteral("/tmp/$HOME`id`")), QStringLiteral("'/tmp/$HOME`id`'"));
        QCOMPARE(shellQuote(QStringLiteral("/tmp/it's")), QStringLiteral("'/tmp/it'\\''s'"));
        QCOMPARE(shellQuote(QStringLiteral("/tmp/say \"hi\"")), QStringLiteral("'/tmp/say \"hi\"'"));
        QCOMPARE(shellQuote(QStringLiteral("/tmp/line\nbreak")), QStringLiteral("'/tmp/line\nbreak'"));
        QCOMPARE(shellQuote(QStringLiteral("/tmp/réveillé/日本語")), QStringLiteral("'/tmp/réveillé/日本語'"));
        QCOMPARE(shellQuote(QStringList{QStringLiteral("/a b"), QStringLiteral("/c")}), QStringLiteral("'/a b' '/c'"));
    }

    // The quoting is only right if a real shell agrees, so a real shell is asked: every awkward
    // path is echoed back through `sh -c` and has to come out byte for byte.
    void aRealShellReadsTheQuotingBack()
    {
        const QStringList paths{QStringLiteral("/etc/nginx/nginx.conf"), QStringLiteral("/tmp/two words"),
                                QStringLiteral("/tmp/it's"), QStringLiteral("/tmp/$HOME`id`\\"),
                                QStringLiteral("/tmp/say \"hi\""), QStringLiteral("/tmp/réveillé"),
                                QStringLiteral("/tmp/semi;colon&pipe|"), QStringLiteral("/tmp/star*question?")};
        for (const QString &path : paths) {
            QProcess sh;
            sh.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("printf %s ") + shellQuote(path)});
            QVERIFY(sh.waitForFinished(5000));
            QCOMPARE(QString::fromUtf8(sh.readAllStandardOutput()), path);
        }
        // And a whole script, nested one level deeper: `sh -c '<script that quotes a path>'`.
        QProcess sh;
        sh.start(QStringLiteral("sh"), {QStringLiteral("-c"),
                                        QStringLiteral("sh -c ") + shellQuote(QStringLiteral("printf %s ") + shellQuote(QStringLiteral("/tmp/it's a \"file\"")))});
        QVERIFY(sh.waitForFinished(5000));
        QCOMPARE(QString::fromUtf8(sh.readAllStandardOutput()), QStringLiteral("/tmp/it's a \"file\""));
    }

    // ----- stat, conflicts, binary -----------------------------------------------------------

    void statParses()
    {
        const FileStat s = parseStat("1234:1758153600:644\n");
        QVERIFY(s.ok);
        QCOMPARE(s.size, 1234);
        QCOMPARE(s.mtime, 1758153600);
        QCOMPARE(s.mode, QStringLiteral("644"));
        QCOMPARE(s.line(), QStringLiteral("1234:1758153600:644"));
        QCOMPARE(parseStat("0:0:1777").mode, QStringLiteral("1777"));
        QVERIFY(!parseStat("").ok);
        QVERIFY(!parseStat("stat: cannot stat '/x'").ok);
        QVERIFY(!parseStat("1234:1758153600").ok);
        QVERIFY(!parseStat("x:1:644").ok);
        QVERIFY(!parseStat("1:2:rw-r--r--").ok);   // a mode that is not octal is not a mode
    }

    void aChangedFileIsNoticed()
    {
        const FileStat fetched = parseStat("10:100:644");
        QCOMPARE(conflictOf(fetched, parseStat("10:100:644")), Conflict::None);
        QCOMPARE(conflictOf(fetched, parseStat("11:100:644")), Conflict::Changed);   // grew
        QCOMPARE(conflictOf(fetched, parseStat("10:101:644")), Conflict::Changed);   // rewritten
        QCOMPARE(conflictOf(fetched, parseStat("10:100:600")), Conflict::Changed);   // chmod
        QCOMPARE(conflictOf(fetched, FileStat()), Conflict::Vanished);
        // Nothing was fetched (a new file), so there is nothing to be surprised by.
        QCOMPARE(conflictOf(FileStat(), parseStat("10:100:644")), Conflict::None);

        const QString message = conflictMessage(QStringLiteral("filly"), QStringLiteral("/etc/hosts"),
                                                fetched, parseStat("11:101:644"));
        QVERIFY(message.contains(QStringLiteral("hosts")));
        QVERIFY(message.contains(QStringLiteral("filly")));
        QVERIFY(conflictMessage(QStringLiteral("filly"), QStringLiteral("/etc/hosts"), fetched, FileStat())
                    .contains(QStringLiteral("no longer")));
    }

    void binaryIsRefusedAsText()
    {
        QVERIFY(looksBinary(QByteArray("\x7f" "ELF\0\0\0", 8)));
        QVERIFY(!looksBinary("#!/bin/sh\necho hello\n"));
        QVERIFY(!looksBinary(QByteArray()));
        // A NUL past the first kilobyte is not sniffed: the file already read as text.
        QVERIFY(!looksBinary(QByteArray(kBinarySniffBytes, 'x') + QByteArray(1, '\0')));
    }

    // ----- the scripts -------------------------------------------------------------------------

    void theFetchScriptSaysWhatItWillDo()
    {
        const QString script = fetchScript(QStringLiteral("/etc/it's"));
        QVERIFY(script.contains(QStringLiteral("p='/etc/it'\\''s'")));
        QVERIFY(script.contains(QStringLiteral("stat -c %s:%Y:%a")));
        QVERIFY(script.contains(QStringLiteral("stat -f %z:%m:%Lp")));   // the BSDs
        QVERIFY(script.contains(QStringLiteral("cat -- \"$p\"")));
        QVERIFY(script.contains(QString::number(kMaxFileBytes)));
        QVERIFY(script.contains(QStringLiteral("exit 12")));             // a folder
    }

    void theSaveScriptChecksThenMovesIntoPlace()
    {
        const FileStat expected = parseStat("10:100:644");
        const QString script = saveScript(QStringLiteral("/etc/hosts"), expected);
        QVERIFY(script.contains(QStringLiteral("want='10:100:644'")));
        QVERIFY(script.contains(QStringLiteral("exit 20")));                       // refuse, do not write
        QVERIFY(script.contains(QStringLiteral("mktemp -- \"$d/.relay-save.XXXXXX\"")));
        QVERIFY(script.contains(QStringLiteral("cat > \"$t\"")));                  // the bytes come from stdin
        QVERIFY(script.contains(QStringLiteral("chmod --reference=\"$p\"")));      // the mode survives
        QVERIFY(script.contains(QStringLiteral("mv -f -- \"$t\" \"$p\"")));        // atomic
        QVERIFY(script.indexOf(QStringLiteral("exit 20")) < script.indexOf(QStringLiteral("mktemp")));
        // "Overwrite anyway": the same script with nothing to compare.
        QVERIFY(saveScript(QStringLiteral("/etc/hosts"), FileStat()).contains(QStringLiteral("want=''")));
    }

    void theProbeScriptAsksForAHandfulAtOnce()
    {
        const QString script = probeScript({QStringLiteral("/etc"), QStringLiteral("/tmp/a b")});
        QVERIFY(script.contains(QStringLiteral("for p in '/etc' '/tmp/a b'; do")));
        QCOMPARE(parseProbe("d\nf\nm\n", 3), (QVector<Entry>{Entry::Directory, Entry::File, Entry::Missing}));
        // A batch cut short leaves the rest to be asked again, not called missing.
        QCOMPARE(parseProbe("d\n", 3), (QVector<Entry>{Entry::Directory, Entry::Unknown, Entry::Unknown}));
        QCOMPARE(parseProbe("", 1), (QVector<Entry>{Entry::Unknown}));
    }

    // The scripts are the part that runs on someone else's machine, so they are run here too:
    // a real `sh`, real files, a name with a space and a quote in it, and the mode checked after
    // the save. Only ssh is left out of the loop.
    void theScriptsRunInARealShell()
    {
        const QString path = QDir(m_dir.path()).filePath(QStringLiteral("it's a conf"));
        QVERIFY(writeFile(path, "one\n"));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));   // 0640

        QByteArray out;
        QCOMPARE(runScript(fetchScript(path), {}, &out), 0);
        const int newline = out.indexOf('\n');
        const FileStat fetched = parseStat(out.left(newline));
        QVERIFY(fetched.ok);
        QCOMPARE(fetched.size, 4);
        QCOMPARE(fetched.mode, QStringLiteral("640"));
        QCOMPARE(out.mid(newline + 1), QByteArray("one\n"));

        // The save: bytes on stdin, a temporary file beside it, mode kept, nothing left behind.
        QCOMPARE(runScript(saveScript(path, fetched), "two words\n", &out), 0);
        QCOMPARE(readFile(path), QByteArray("two words\n"));
        QCOMPARE(parseStat(out).mode, QStringLiteral("640"));
        QCOMPARE(QFile::permissions(path) & QFile::WriteOther, QFileDevice::Permissions());
        QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral(".relay-save.*")}, QDir::Files | QDir::Hidden).size(), 0);

        // Someone else wrote it in the meantime: refused, and the file is still theirs.
        QVERIFY(writeFile(path, "theirs\n"));
        QCOMPARE(runScript(saveScript(path, fetched), "mine\n", &out), int(ChangedStatus));
        QCOMPARE(readFile(path), QByteArray("theirs\n"));
        QVERIFY(parseStat(out).ok);                      // and it says what is there now
        // "Overwrite anyway".
        QCOMPARE(runScript(saveScript(path, FileStat()), "mine\n", &out), 0);
        QCOMPARE(readFile(path), QByteArray("mine\n"));

        // A file that is not there, a folder, and one that is over the cap.
        QCOMPARE(runScript(fetchScript(QDir(m_dir.path()).filePath(QStringLiteral("nope"))), {}, &out), int(MissingStatus));
        QCOMPARE(runScript(fetchScript(m_dir.path()), {}, &out), int(DirectoryStatus));
        QCOMPARE(runScript(fetchScript(path, 2), {}, &out), int(TooLargeStatus));
        QVERIFY(parseStat(out).ok);                      // with the size, so the pane can name it

        // And the probe, on the same real files.
        QCOMPARE(runScript(probeScript({m_dir.path(), path, QDir(m_dir.path()).filePath(QStringLiteral("nope"))}), {}, &out), 0);
        QCOMPARE(parseProbe(out, 3), (QVector<Entry>{Entry::Directory, Entry::File, Entry::Missing}));
    }

    // The folder listing, likewise: a real `sh` over real entries, including a dot file, a
    // subfolder, a name with a space and a symlink into it.
    void theListingScriptRunsInARealShell()
    {
        const QString dir = QDir(m_dir.path()).filePath(QStringLiteral("listing"));
        QVERIFY(QDir().mkpath(QDir(dir).filePath(QStringLiteral("sub"))));
        QVERIFY(writeFile(QDir(dir).filePath(QStringLiteral("two words.txt")), "hello\n"));
        QVERIFY(writeFile(QDir(dir).filePath(QStringLiteral(".dotfile")), "x"));
        QVERIFY(QFile::link(QDir(dir).filePath(QStringLiteral("sub")), QDir(dir).filePath(QStringLiteral("link"))));

        QByteArray out;
        QCOMPARE(runScript(listScript(dir), {}, &out), 0);
        const QVector<DirEntry> entries = parseListing(out);
        QStringList names;
        for (const DirEntry &entry : entries) names << (entry.directory ? QStringLiteral("d ") : QStringLiteral("f ")) + entry.name;
        // Folders first (the symlink into a folder counts as one, because `stat -L` follows it),
        // then the files by name; the dot file is there for the pane to hide or show.
        QCOMPARE(names, (QStringList{QStringLiteral("d link"), QStringLiteral("d sub"),
                                     QStringLiteral("f .dotfile"), QStringLiteral("f two words.txt")}));
        for (const DirEntry &entry : entries)
            if (entry.name == QStringLiteral("two words.txt")) QCOMPARE(entry.size, 6);

        // An empty folder lists nothing rather than the literal `*` of an unmatched glob.
        const QString empty = QDir(m_dir.path()).filePath(QStringLiteral("empty"));
        QVERIFY(QDir().mkpath(empty));
        QCOMPARE(runScript(listScript(empty), {}, &out), 0);
        QVERIFY(parseListing(out).isEmpty());
        // And a folder that is not there says so with the same status a missing file uses.
        QCOMPARE(runScript(listScript(QDir(m_dir.path()).filePath(QStringLiteral("nope"))), {}, &out), int(MissingStatus));
    }

    void aFolderIsListedOverTheConnection()
    {
        installFakeSsh();
        answerWith("directory|4096|200|sub\nregular file|12|100|readme.md\n", 0);
        RemoteDir dir;
        dir.setHost(QStringLiteral("filly"), m_socket);
        QVector<DirEntry> entries;
        QString failure, listed;
        bool truncated = true;
        dir.onListed = [&](const QString &path, const QVector<DirEntry> &rows, bool cut) {
            listed = path; entries = rows; truncated = cut;
        };
        dir.onFailed = [&](const QString &message) { failure = message; };
        dir.list(QStringLiteral("/srv/archive"));
        QTRY_VERIFY_WITH_TIMEOUT(!dir.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(listed, QStringLiteral("/srv/archive"));
        QCOMPARE(entries.size(), 2);
        QVERIFY(!truncated);
        QVERIFY(scriptOf(fakeArgv()).contains(QStringLiteral("cd -- '/srv/archive'")));

        // With no connection it says so instead of showing an empty folder.
        failure.clear();
        dir.setHost(QStringLiteral("filly"), QDir(m_dir.path()).filePath(QStringLiteral("gone")));
        forgetLogin(QStringLiteral("filly"));
        dir.list(QStringLiteral("/srv/archive"));
        QVERIFY(failure.contains(QStringLiteral("no connection")));
    }

    void sshReusesTheUsersOwnConnection()
    {
        const QStringList argv = sshCommand(QStringLiteral("filly"), QStringLiteral("/run/r/abc"), QStringLiteral("echo hi"));
        QCOMPARE(argv.mid(0, 11), (QStringList{"-S", "/run/r/abc", "-o", "ControlMaster=no", "-o", "BatchMode=yes",
                                               "-o", "ConnectTimeout=10", "-T", "filly", "--"}));
        QCOMPARE(argv.last(), QStringLiteral("sh -c 'echo hi'"));
    }

    void failuresReadAsSentences()
    {
        QVERIFY(statusMessage(MissingStatus, QStringLiteral("filly"), QStringLiteral("/etc/hosts"), {})
                    .contains(QStringLiteral("not on filly")));
        QVERIFY(statusMessage(UnreadableStatus, QStringLiteral("filly"), QStringLiteral("/etc/shadow"), {})
                    .contains(QStringLiteral("permission")));
        QVERIFY(statusMessage(WriteFailedStatus, QStringLiteral("filly"), QStringLiteral("/srv/x"),
                              "sh: line 1: /srv/x: No space left on device")
                    .contains(QStringLiteral("No space left on device")));
        QVERIFY(statusMessage(255, QStringLiteral("filly"), QStringLiteral("/etc/hosts"), "Connection closed")
                    .contains(QStringLiteral("filly")));
    }

    // ----- the URL a pane hands to a preview pane -------------------------------------------

    void remoteFilesTravelAsUrls()
    {
        QCOMPARE(fileUrl(QStringLiteral("filly"), QStringLiteral("/etc/nginx/nginx.conf")),
                 QStringLiteral("ssh://filly/etc/nginx/nginx.conf"));
        QVERIFY(isFileUrl(QStringLiteral("ssh://filly/etc/hosts")));
        QVERIFY(!isFileUrl(QStringLiteral("/etc/hosts")));
        QVERIFY(!isFileUrl(QStringLiteral("https://example.com/x")));
        QVERIFY(!isFileUrl(QStringLiteral("ssh://filly")));
        const FileRef ref = parseFileUrl(fileUrl(QStringLiteral("Filly"), QStringLiteral("/srv/two words/a#b")));
        QVERIFY(ref.ok);
        QCOMPARE(ref.host, QStringLiteral("Filly"));   // the host keeps the case the user typed
        QCOMPARE(ref.path, QStringLiteral("/srv/two words/a#b"));
        QCOMPARE(displayName(QStringLiteral("filly"), QStringLiteral("/etc/hosts")), QStringLiteral("filly:/etc/hosts"));
    }

    // A folder's URL ends in `/`, which is how the window knows to open an explorer pane and not
    // a preview without asking the host a second time.
    void aFolderSaysSoInItsUrl()
    {
        QCOMPARE(folderUrl(QStringLiteral("filly"), QStringLiteral("/etc/nginx")), QStringLiteral("ssh://filly/etc/nginx/"));
        QCOMPARE(folderUrl(QStringLiteral("filly"), QStringLiteral("/")), QStringLiteral("ssh://filly/"));
        QVERIFY(parseFileUrl(QStringLiteral("ssh://filly/etc/nginx/")).directory);
        QCOMPARE(parseFileUrl(QStringLiteral("ssh://filly/etc/nginx/")).path, QStringLiteral("/etc/nginx"));
        QVERIFY(!parseFileUrl(QStringLiteral("ssh://filly/etc/nginx")).directory);
        const FileRef root = parseFileUrl(QStringLiteral("ssh://filly/"));
        QVERIFY(root.ok);
        QVERIFY(root.directory);
        QCOMPARE(root.path, QStringLiteral("/"));

        QCOMPARE(parentPath(QStringLiteral("/etc/nginx/nginx.conf")), QStringLiteral("/etc/nginx"));
        QCOMPARE(parentPath(QStringLiteral("/etc")), QStringLiteral("/"));
        QCOMPARE(parentPath(QStringLiteral("/")), QStringLiteral("/"));
        QCOMPARE(childPath(QStringLiteral("/etc"), QStringLiteral("hosts")), QStringLiteral("/etc/hosts"));
        QCOMPARE(childPath(QStringLiteral("/"), QStringLiteral("etc")), QStringLiteral("/etc"));
    }

    void aListingIsFoldersFirstThenNames()
    {
        const QString script = listScript(QStringLiteral("/srv/two words"));
        QVERIFY(script.contains(QStringLiteral("cd -- '/srv/two words'")));
        QVERIFY(script.contains(QStringLiteral("set -- * .[!.]* ..?*")));   // dot files included
        QVERIFY(script.contains(QStringLiteral("stat -L -c '%F|%s|%Y|%n'")));
        QVERIFY(script.contains(QStringLiteral("stat -L -f '%HT|%z|%m|%N'")));   // the BSDs
        QVERIFY(script.contains(QStringLiteral("exit 10")));                     // not a folder

        bool truncated = true;
        const QVector<DirEntry> entries = parseListing(
            "regular file|12|100|readme.md\n"
            "directory|4096|200|sub\n"
            "regular empty file|0|300|.hidden\n"
            "symbolic link|9|400|dangling\n", 100, &truncated);
        QVERIFY(!truncated);
        QCOMPARE(entries.size(), 4);
        QCOMPARE(entries.at(0).name, QStringLiteral("sub"));       // folders first
        QVERIFY(entries.at(0).directory);
        QCOMPARE(entries.at(1).name, QStringLiteral(".hidden"));   // then by name, case-insensitively
        QCOMPARE(entries.at(2).name, QStringLiteral("dangling"));
        QCOMPARE(entries.at(3).name, QStringLiteral("readme.md"));
        QCOMPARE(entries.at(3).size, 12);
        QCOMPARE(entries.at(3).mtime, 100);
        QVERIFY(!entries.at(3).directory);
        // A name with a `|` in it keeps it: the name is the last field for exactly that reason.
        QCOMPARE(parseListing("regular file|1|2|a|b.txt\n").at(0).name, QStringLiteral("a|b.txt"));
        // Junk, `.` and `..` are skipped rather than shown as rows.
        QVERIFY(parseListing("stat: cannot stat '*'\n").isEmpty());
        QVERIFY(parseListing("directory|1|2|.\ndirectory|1|2|..\n").isEmpty());
        // Longer than the cap: cut, and it says so.
        QByteArray many;
        for (int i = 0; i < 10; ++i) many += QStringLiteral("regular file|1|2|f%1\n").arg(i).toUtf8();
        QCOMPARE(parseListing(many, 4, &truncated).size(), 4);
        QVERIFY(truncated);
    }

    void aLoginIsAnnouncedAndForgotten()
    {
        QVERIFY(!loginLive(QStringLiteral("filly")));
        announceLogin(QStringLiteral("filly"), m_socket);
        QCOMPARE(loginControlPath(QStringLiteral("filly")), m_socket);
        // The record is not enough: when the socket goes, so does the login.
        announceLogin(QStringLiteral("filly"), QDir(m_dir.path()).filePath(QStringLiteral("gone")));
        QVERIFY(!loginLive(QStringLiteral("filly")));
        announceLogin(QStringLiteral("filly"), m_socket);
        forgetLogin(QStringLiteral("filly"));
        QVERIFY(!loginLive(QStringLiteral("filly")));
    }

    // ----- against a fake ssh ------------------------------------------------------------------

    void aFetchBringsBackTheStatAndTheFile()
    {
        installFakeSsh();
        answerWith("48:1758153600:640\nserver { listen 80; }\n", 0);

        RemoteFile file;
        file.setHost(QStringLiteral("filly"), m_socket);
        QByteArray got;
        FileStat stat;
        QString failure;
        file.onFetched = [&](const QByteArray &content, const FileStat &s) { got = content; stat = s; };
        file.onFailed = [&](const QString &message, Conflict, const FileStat &) { failure = message; };
        file.fetch(QStringLiteral("/etc/nginx/nginx.conf"));
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(got, QByteArray("server { listen 80; }\n"));
        QCOMPARE(stat.mode, QStringLiteral("640"));
        QCOMPARE(file.fetched().size, 48);
        // The file it asked for, quoted, over the user's socket.
        const QStringList argv = fakeArgv();
        QVERIFY(argv.contains(m_socket));
        QVERIFY(scriptOf(argv).contains(QStringLiteral("p='/etc/nginx/nginx.conf'")));
    }

    // Binary is the text viewer's rule, not the transport's: an image and a PDF are binary and
    // are meant to be, so the bytes come back and the pane decides (FilePreview does, and
    // tests/filepanes_test.cpp checks that it refuses a binary file as text).
    void binaryBytesComeBackWhole()
    {
        installFakeSsh();
        const QByteArray elf("\x7f" "ELF\0\0\0\0", 8);
        answerWith(QByteArray("8:1:644\n") + elf, 0);
        RemoteFile file;
        file.setHost(QStringLiteral("filly"), m_socket);
        QString failure;
        QByteArray got;
        bool fetched = false;
        file.onFetched = [&](const QByteArray &content, const FileStat &) { got = content; fetched = true; };
        file.onFailed = [&](const QString &message, Conflict, const FileStat &) { failure = message; };
        file.fetch(QStringLiteral("/bin/ls"));
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty() || fetched, 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(got, elf);
        QVERIFY(looksBinary(got));
    }

    void aRefusedFileSaysWhy()
    {
        installFakeSsh();
        answerWith(QByteArray(), int(TooLargeStatus));
        RemoteFile file;
        file.setHost(QStringLiteral("filly"), m_socket);
        QString failure;
        file.onFailed = [&](const QString &message, Conflict, const FileStat &) { failure = message; };
        file.fetch(QStringLiteral("/var/log/huge.log"));
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 10000);
        QVERIFY(failure.contains(QStringLiteral("larger than")));
    }

    // The point of the exercise: a file's bytes go over ssh's stdin. In argv they would be in the
    // host's process table for anyone with a `ps` to read.
    void theBytesGoOverStdinAndNeverInArgv()
    {
        installFakeSsh();
        RemoteFile file;
        file.setHost(QStringLiteral("filly"), m_socket);
        FileStat saved;
        QString failure;
        file.onSaved = [&](const FileStat &s) { saved = s; };
        file.onFailed = [&](const QString &message, Conflict, const FileStat &) { failure = message; };

        answerWith("10:1758153600:644\n127.0.0.1 localhost\n", 0);
        file.fetch(QStringLiteral("/etc/hosts"));
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));

        answerWith("21:1758153700:644\n", 0);
        file.save(QByteArray("127.0.0.1 localhost\n# a secret\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(QString::fromUtf8(readFile(QDir(m_dir.path()).filePath(QStringLiteral("stdin")))),
                 QStringLiteral("127.0.0.1 localhost\n# a secret\n"));
        QCOMPARE(saved.mtime, 1758153700);
        const QString argv = fakeArgv().join(QLatin1Char(' '));
        QVERIFY(!argv.contains(QStringLiteral("127.0.0.1 localhost")));
        QVERIFY(scriptOf(fakeArgv()).contains(QStringLiteral("want='10:1758153600:644'")));
    }

    void aSaveOverAChangedFileComesBackAsAChoice()
    {
        installFakeSsh();
        RemoteFile file;
        file.setHost(QStringLiteral("filly"), m_socket);
        Conflict conflict = Conflict::None;
        FileStat now;
        QString failure;
        file.onFailed = [&](const QString &message, Conflict c, const FileStat &s) { failure = message; conflict = c; now = s; };

        answerWith("10:1758153600:644\nold\n", 0);
        file.fetch(QStringLiteral("/etc/hosts"));
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));

        // The host found a different file under the lock and wrote nothing.
        answerWith("99:1758159999:644\n", int(ChangedStatus));
        file.save(QByteArray("new\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 10000);
        QCOMPARE(conflict, Conflict::Changed);
        QCOMPARE(now.size, 99);
        QVERIFY(failure.contains(QStringLiteral("changed on filly")));

        // "Overwrite anyway" asks again with nothing to compare, and the host takes it.
        failure.clear();
        answerWith("4:1758160000:644\n", 0);
        FileStat saved;
        file.onSaved = [&](const FileStat &s) { saved = s; };
        file.save(QByteArray("new\n"), true);
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(saved.size, 4);
        QVERIFY(scriptOf(fakeArgv()).contains(QStringLiteral("want=''")));
    }

    void aSaveWithNoConnectionKeepsTheBuffer()
    {
        installFakeSsh();
        RemoteFile file;
        file.setHost(QStringLiteral("filly"), m_socket);
        QString failure;
        file.onFailed = [&](const QString &message, Conflict, const FileStat &) { failure = message; };
        answerWith("10:1758153600:644\nold\n", 0);
        file.fetch(QStringLiteral("/etc/hosts"));
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);

        // The user quit ssh while the pane was still open: the socket is gone.
        file.setHost(QStringLiteral("filly"), QDir(m_dir.path()).filePath(QStringLiteral("gone")));
        QVERIFY(!file.live());
        file.save(QByteArray("new\n"));
        QVERIFY(failure.contains(QStringLiteral("gone")));
        QVERIFY(failure.contains(QStringLiteral("still here")));

        // They log in again, and the same pane saves over the new login.
        announceLogin(QStringLiteral("filly"), m_socket);
        QVERIFY(file.live());
        failure.clear();
        answerWith("4:1758160000:644\n", 0);
        FileStat saved;
        file.onSaved = [&](const FileStat &s) { saved = s; };
        file.save(QByteArray("new\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!file.busy(), 10000);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QCOMPARE(saved.size, 4);
    }

    void theProbeAsksOncePerPathAndCaches()
    {
        installFakeSsh(QStringLiteral("echo call >> \"$RELAY_FAKE/calls\"\nprintf 'd\\nf\\nm\\n'\nexit 0\n"));
        PathProbe probe;
        probe.setHost(QStringLiteral("filly"), m_socket);
        QCOMPARE(probe.lookup(QStringLiteral("/etc")), Entry::Unknown);
        QCOMPARE(probe.lookup(QStringLiteral("/etc/hosts")), Entry::Unknown);
        QCOMPARE(probe.lookup(QStringLiteral("/nope")), Entry::Unknown);
        QCOMPARE(probe.lookup(QStringLiteral("/etc")), Entry::Unknown);   // asked once, not twice
        QCOMPARE(probe.queuedCount(), 3);
        bool answered = false;
        probe.onAnswers = [&] { answered = true; };
        QTRY_VERIFY_WITH_TIMEOUT(answered, 10000);
        QCOMPARE(probe.lookup(QStringLiteral("/etc")), Entry::Directory);
        QCOMPARE(probe.lookup(QStringLiteral("/etc/hosts")), Entry::File);
        QCOMPARE(probe.lookup(QStringLiteral("/nope")), Entry::Missing);
        QCOMPARE(probe.cachedCount(), 3);
        // One batch, three paths.
        QCOMPARE(readFile(QDir(m_dir.path()).filePath(QStringLiteral("calls"))).count('\n'), 1);
        // The login ends: everything it said goes with it.
        probe.setHost(QString());
        QCOMPARE(probe.cachedCount(), 0);
        QCOMPARE(probe.lookup(QStringLiteral("/etc")), Entry::Unknown);
    }

private:
    QByteArray readFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    // One remote script, run here instead: `sh -c '<script>'`, exactly as ssh would run it.
    int runScript(const QString &script, const QByteArray &input, QByteArray *output)
    {
        QProcess sh;
        sh.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("sh -c ") + shellQuote(script)});
        if (!sh.waitForStarted(5000)) return -1;
        sh.write(input);
        sh.closeWriteChannel();
        if (!sh.waitForFinished(10000)) return -1;
        if (output) *output = sh.readAllStandardOutput();
        return sh.exitCode();
    }

    QString replyPath() const { return QDir(m_dir.path()).filePath(QStringLiteral("reply")); }
    QString codePath() const { return QDir(m_dir.path()).filePath(QStringLiteral("code")); }
    // What the next remote command answers: the bytes on stdout and the exit status.
    void answerWith(const QByteArray &reply, int code)
    {
        QVERIFY(writeFile(replyPath(), reply));
        QVERIFY(writeFile(codePath(), QByteArray::number(code)));
    }
    QStringList fakeArgv()
    {
        QStringList argv;
        // NUL-separated: a script argument has newlines of its own.
        const QList<QByteArray> parts = readFile(QDir(m_dir.path()).filePath(QStringLiteral("argv"))).split('\0');
        for (const QByteArray &part : parts) argv << QString::fromUtf8(part);
        if (!argv.isEmpty() && argv.last().isEmpty()) argv.removeLast();
        return argv;
    }

    // A fake `ssh` first of all on PATH: it writes its arguments down one per line and then does
    // whatever the test told it to.
    // The usual one: keep whatever came over stdin, answer with `reply`, exit with `code`.
    void installFakeSsh()
    {
        installFakeSsh(QStringLiteral("cat > \"$RELAY_FAKE/stdin\"\n"
                                      "cat \"$RELAY_FAKE/reply\"\n"
                                      "exit \"$(cat \"$RELAY_FAKE/code\")\"\n"));
    }

    void installFakeSsh(const QString &body)
    {
        const QString bin = QDir(m_dir.path()).filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        const QString script = QDir(bin).filePath(QStringLiteral("ssh"));
        QVERIFY(writeFile(script, (QStringLiteral("#!/bin/sh\n"
                                                  "RELAY_FAKE=%1\n"
                                                  "export RELAY_FAKE\n"
                                                  "rm -f \"$RELAY_FAKE/argv\"\n"
                                                  "for a in \"$@\"; do printf '%s\\0' \"$a\" >> \"$RELAY_FAKE/argv\"; done\n")
                                       .arg(m_dir.path())
                                   + body)
                                      .toUtf8()));
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        if (m_savedPath.isNull()) m_savedPath = qgetenv("PATH");
        qputenv("PATH", bin.toLocal8Bit() + ':' + m_savedPath);
    }

    QTemporaryDir m_dir;
    QString m_socket;
    QByteArray m_savedPath;
};

QTEST_MAIN(RemoteFilesTest)
#include "remotefiles_test.moc"
