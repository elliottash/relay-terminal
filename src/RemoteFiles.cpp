// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RemoteFiles.h"

#include <QDateTime>
#include <QFileInfo>
#include <algorithm>
#include <QLocale>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace relay::remote {

namespace {

// The live logins, by host as the login names it. A pane announces one; a preview pane looks it
// up. GUI thread only, like everything else here.
QHash<QString, QString> &logins()
{
    static QHash<QString, QString> map;
    return map;
}

QString humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

// `stat -c` is GNU; a BSD or macOS host spells the same three fields `-f %z:%m:%Lp`. Both are
// tried, in that order, so one script covers Linux and the BSDs without asking first.
QString statInto(const QString &variable, const QString &quotedPath)
{
    return QStringLiteral("%1=$(stat -c %s:%Y:%a -- %2 2>/dev/null) || "
                          "%1=$(stat -f %z:%m:%Lp -- %2 2>/dev/null)")
        .arg(variable, quotedPath);
}

}  // namespace

// ----- shell quoting -----------------------------------------------------------------------

QString shellQuote(const QString &text)
{
    if (text.isEmpty()) return QStringLiteral("''");
    QString out;
    out.reserve(text.size() + 2);
    out += QLatin1Char('\'');
    for (const QChar c : text) {
        // The one character single quotes cannot hold: close, escape it, open again.
        if (c == QLatin1Char('\'')) out += QStringLiteral("'\\''");
        else out += c;
    }
    out += QLatin1Char('\'');
    return out;
}

QString shellQuote(const QStringList &words)
{
    QStringList quoted;
    quoted.reserve(words.size());
    for (const QString &word : words) quoted << shellQuote(word);
    return quoted.join(QLatin1Char(' '));
}

// ----- what the host says about a file -------------------------------------------------------

QString FileStat::line() const
{
    if (!ok) return {};
    return QStringLiteral("%1:%2:%3").arg(size).arg(mtime).arg(mode);
}

FileStat parseStat(const QByteArray &line)
{
    FileStat out;
    const QList<QByteArray> parts = line.trimmed().split(':');
    if (parts.size() < 3) return out;
    bool sizeOk = false, mtimeOk = false;
    const qint64 size = parts.at(0).toLongLong(&sizeOk);
    const qint64 mtime = parts.at(1).toLongLong(&mtimeOk);
    const QString mode = QString::fromLatin1(parts.at(2).trimmed());
    if (!sizeOk || !mtimeOk || size < 0 || mode.isEmpty()) return out;
    for (const QChar c : mode)
        if (c < QLatin1Char('0') || c > QLatin1Char('7')) return out;
    out.size = size;
    out.mtime = mtime;
    out.mode = mode;
    out.ok = true;
    return out;
}

Conflict conflictOf(const FileStat &fetched, const FileStat &now)
{
    if (!fetched.ok) return Conflict::None;        // nothing was fetched: nothing to be surprised by
    if (!now.ok) return Conflict::Vanished;
    return fetched == now ? Conflict::None : Conflict::Changed;
}

QString conflictMessage(const QString &host, const QString &path, const FileStat &fetched, const FileStat &now)
{
    const QString name = QFileInfo(path).fileName();
    if (conflictOf(fetched, now) == Conflict::Vanished)
        return QStringLiteral("%1 is no longer on %2 · saving would create it again.").arg(name, host);
    QStringList what;
    if (fetched.size != now.size) what << QStringLiteral("%1 → %2").arg(humanSize(fetched.size), humanSize(now.size));
    if (fetched.mtime != now.mtime && now.mtime > 0)
        what << QStringLiteral("written %1").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(now.mtime), QLocale::ShortFormat));
    if (fetched.mode != now.mode) what << QStringLiteral("mode %1 → %2").arg(fetched.mode, now.mode);
    return QStringLiteral("%1 changed on %2 since you opened it%3").arg(name, host,
        what.isEmpty() ? QStringLiteral(".") : QStringLiteral(" (%1).").arg(what.join(QStringLiteral(", "))));
}

bool looksBinary(const QByteArray &head)
{
    return head.left(kBinarySniffBytes).contains('\0');
}

// ----- the remote scripts ---------------------------------------------------------------------

QString fetchScript(const QString &path, qint64 maxBytes)
{
    const QString p = shellQuote(path);
    // Printed first: the stat line. Everything after the first newline is the file, byte for byte.
    return QStringLiteral(
               "p=%1\n"
               "if [ -d \"$p\" ]; then exit %2; fi\n"
               "if [ ! -e \"$p\" ]; then exit %3; fi\n"
               "if [ ! -r \"$p\" ]; then exit %4; fi\n"
               "%5 || exit %6\n"
               "echo \"$s\"\n"
               "n=${s%%:*}\n"
               "if [ \"$n\" -gt %7 ]; then exit %8; fi\n"
               "cat -- \"$p\"\n")
        .arg(p)
        .arg(int(DirectoryStatus))
        .arg(int(MissingStatus))
        .arg(int(UnreadableStatus))
        .arg(statInto(QStringLiteral("s"), QStringLiteral("\"$p\"")))
        .arg(int(NoStatStatus))
        .arg(maxBytes)
        .arg(int(TooLargeStatus));
}

QString saveScript(const QString &path, const FileStat &expected)
{
    const QString p = shellQuote(path);
    // The check and the `mv` happen on the host, in that order, with nothing of ours in between:
    // that is as close to atomic as a plain ssh session gets. The bytes arrive on stdin, so they
    // are in no argument list and no process table.
    return QStringLiteral(
               "p=%1\n"
               "d=$(dirname -- \"$p\") || exit %2\n"
               "s=\n"
               "if [ -e \"$p\" ]; then %3 || exit %4; fi\n"
               "want=%5\n"
               "if [ -n \"$want\" ] && [ \"$s\" != \"$want\" ]; then echo \"$s\"; exit %6; fi\n"
               "t=$(mktemp -- \"$d/.relay-save.XXXXXX\") || exit %2\n"
               "cat > \"$t\" || { rm -f -- \"$t\"; exit %7; }\n"
               "if [ -e \"$p\" ]; then chmod --reference=\"$p\" -- \"$t\" 2>/dev/null || "
               "chmod \"${s##*:}\" -- \"$t\" 2>/dev/null; fi\n"
               "mv -f -- \"$t\" \"$p\" || { rm -f -- \"$t\"; exit %8; }\n"
               "%9 || exit %4\n"
               "echo \"$s\"\n")
        .arg(p)
        .arg(int(NoTempStatus))
        .arg(statInto(QStringLiteral("s"), QStringLiteral("\"$p\"")))
        .arg(int(NoStatStatus))
        .arg(shellQuote(expected.line()))
        .arg(int(ChangedStatus))
        .arg(int(WriteFailedStatus))
        .arg(int(MoveFailedStatus))
        .arg(statInto(QStringLiteral("s"), QStringLiteral("\"$p\"")));
}

QString statScript(const QString &path)
{
    return QStringLiteral(
               "p=%1\n"
               "if [ ! -e \"$p\" ]; then exit %2; fi\n"
               "%3 || exit %4\n"
               "echo \"$s\"\n")
        .arg(shellQuote(path))
        .arg(int(MissingStatus))
        .arg(statInto(QStringLiteral("s"), QStringLiteral("\"$p\"")))
        .arg(int(NoStatStatus));
}

QString probeScript(const QStringList &paths)
{
    // One line of output per path, in order: a folder, a file (anything else that exists), or
    // nothing there.
    return QStringLiteral("for p in %1; do if [ -d \"$p\" ]; then echo d; elif [ -e \"$p\" ]; then echo f;"
                          " else echo m; fi; done\n")
        .arg(shellQuote(paths));
}

QString listScript(const QString &path)
{
    // One `stat` for the whole folder rather than one per entry: a folder with a thousand files
    // in it is a thousand forks on someone else's machine otherwise. An unmatched glob and a
    // broken symlink each make `stat` complain about that one word on stderr and carry on with
    // the rest, which is why the output is taken whatever the exit status was.
    return QStringLiteral(
               "cd -- %1 2>/dev/null || exit %2\n"
               "set -- * .[!.]* ..?*\n"
               "out=$(stat -L -c '%F|%s|%Y|%n' -- \"$@\" 2>/dev/null)\n"
               "[ -n \"$out\" ] || out=$(stat -L -f '%HT|%z|%m|%N' -- \"$@\" 2>/dev/null)\n"
               "[ -z \"$out\" ] || printf '%s\\n' \"$out\"\n")
        .arg(shellQuote(path))
        .arg(int(MissingStatus));
}

QVector<DirEntry> parseListing(const QByteArray &output, int maxEntries, bool *truncated)
{
    QVector<DirEntry> entries;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty()) continue;
        // type|size|mtime|name — the name is last because it is the one field that may hold a
        // separator of its own.
        const QString text = QString::fromUtf8(line);
        const int first = text.indexOf(QLatin1Char('|'));
        const int second = first < 0 ? -1 : text.indexOf(QLatin1Char('|'), first + 1);
        const int third = second < 0 ? -1 : text.indexOf(QLatin1Char('|'), second + 1);
        if (third < 0) continue;   // a name with a newline in it: skip the pieces, keep the rest
        DirEntry entry;
        entry.directory = text.left(first).contains(QStringLiteral("dir"), Qt::CaseInsensitive);
        entry.size = text.mid(first + 1, second - first - 1).toLongLong();
        entry.mtime = text.mid(second + 1, third - second - 1).toLongLong();
        entry.name = text.mid(third + 1);
        // `stat` prints the name it was given; for a listing that is the bare name already.
        if (entry.name.isEmpty() || entry.name == QStringLiteral(".") || entry.name == QStringLiteral("..")) continue;
        entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const DirEntry &a, const DirEntry &b) {
        if (a.directory != b.directory) return a.directory;   // folders first, as QFileSystemModel does
        const int by = a.name.compare(b.name, Qt::CaseInsensitive);
        return by == 0 ? a.name < b.name : by < 0;
    });
    if (truncated) *truncated = entries.size() > maxEntries;
    if (entries.size() > maxEntries) entries.resize(maxEntries);
    return entries;
}

QStringList sshArguments(const QString &host, const QString &controlPath)
{
    return {QStringLiteral("-S"), controlPath,
            QStringLiteral("-o"), QStringLiteral("ControlMaster=no"),
            QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
            QStringLiteral("-o"), QStringLiteral("ProxyCommand=false"),
            QStringLiteral("-o"), QStringLiteral("ConnectTimeout=10"),
            QStringLiteral("-T"), host, QStringLiteral("--")};
}

QStringList sshCommand(const QString &host, const QString &controlPath, const QString &script)
{
    QStringList out = sshArguments(host, controlPath);
    // The login shell on the host may be anything; `sh -c` makes the script POSIX either way.
    out << QStringLiteral("sh -c ") + shellQuote(script);
    return out;
}

QVector<Entry> parseProbe(const QByteArray &output, int expected)
{
    QVector<Entry> out;
    out.reserve(expected);
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed == "d") out << Entry::Directory;
        else if (trimmed == "f") out << Entry::File;
        else if (trimmed == "m") out << Entry::Missing;
        else out << Entry::Unknown;
        if (out.size() == expected) break;
    }
    // A batch that was cut short (the connection went, the host was killed) leaves the rest
    // unknown rather than claiming they are missing.
    while (out.size() < expected) out << Entry::Unknown;
    return out;
}

QString statusMessage(int exitStatus, const QString &host, const QString &path, const QByteArray &errorOutput)
{
    const QString name = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
    const QString said = QString::fromUtf8(errorOutput).trimmed().split(QLatin1Char('\n')).last().trimmed();
    const QString tail = said.isEmpty() ? QString() : QStringLiteral(" · %1").arg(said);
    switch (exitStatus) {
    case MissingStatus:    return QStringLiteral("%1 is not on %2 any more.").arg(name, host);
    case UnreadableStatus: return QStringLiteral("%1 cannot be read on %2 — you do not have permission.").arg(name, host);
    case DirectoryStatus:  return QStringLiteral("%1 is a folder on %2, not a file.").arg(name, host);
    case TooLargeStatus:   return QStringLiteral("%1 is larger than %2 · Relay does not open it over ssh.")
                                      .arg(name, humanSize(kMaxFileBytes));
    case NoStatStatus:     return QStringLiteral("%1 could not be examined on %2%3").arg(name, host, tail);
    case NoTempStatus:     return QStringLiteral("%1 could not be saved: Relay could not put a temporary file in its folder on %2, "
                                                 "so the folder is not writable by you%3 · your edits are still in this pane — copy them out, "
                                                 "or fix the folder on %2 and save again.").arg(name, host, tail);
    case WriteFailedStatus: return QStringLiteral("%1 could not be written on %2 — the disk may be full or read-only%3").arg(name, host, tail);
    case MoveFailedStatus:  return QStringLiteral("%1 could not be replaced on %2 — a read-only file system, or you do not have permission%3")
                                       .arg(name, host, tail);
    case 255:              return QStringLiteral("The connection to %1 answered nothing%2").arg(host, tail);
    case 127:              return QStringLiteral("%1 has no POSIX shell to run this in%2").arg(host, tail);
    default: break;
    }
    return QStringLiteral("%1 on %2: the host exited %3%4").arg(name, host).arg(exitStatus).arg(tail);
}

// ----- `ssh://host/path` -----------------------------------------------------------------------

QString fileUrl(const QString &host, const QString &path)
{
    if (host.isEmpty() || !path.startsWith(QLatin1Char('/'))) return {};
    // Written out by hand rather than through QUrl, which lower-cases a host: "Filly" is what the
    // user typed and what the ssh config matched, and a title that quietly renames their host
    // reads as a different machine. The path is percent-encoded so a space or a `#` in it survives.
    return QStringLiteral("ssh://%1%2").arg(host, QString::fromUtf8(QUrl::toPercentEncoding(path, "/")));
}

QString folderUrl(const QString &host, const QString &path)
{
    const QString url = fileUrl(host, path);
    if (url.isEmpty() || url.endsWith(QLatin1Char('/'))) return url;
    return url + QLatin1Char('/');
}

bool isFileUrl(const QString &target)
{
    return parseFileUrl(target).ok;
}

FileRef parseFileUrl(const QString &target)
{
    FileRef out;
    if (!target.startsWith(QStringLiteral("ssh://"))) return out;
    const QString rest = target.mid(6);
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash <= 0) return out;
    out.host = rest.left(slash);
    out.path = QUrl::fromPercentEncoding(rest.mid(slash).toUtf8());
    out.directory = out.path.endsWith(QLatin1Char('/'));
    if (out.directory && out.path.size() > 1) out.path.chop(1);
    out.ok = !out.host.isEmpty() && out.path.startsWith(QLatin1Char('/'));
    return out;
}

QString parentPath(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) return {};
    return slash == 0 ? QStringLiteral("/") : path.left(slash);
}

QString childPath(const QString &path, const QString &name)
{
    if (name.isEmpty()) return path;
    return path.endsWith(QLatin1Char('/')) ? path + name : path + QLatin1Char('/') + name;
}

QString displayName(const QString &host, const QString &path)
{
    if (host.isEmpty()) return path;
    return host + QLatin1Char(':') + path;
}

// ----- live logins ----------------------------------------------------------------------------

void announceLogin(const QString &host, const QString &controlPath)
{
    if (host.isEmpty()) return;
    if (controlPath.isEmpty()) { forgetLogin(host); return; }
    logins().insert(host, controlPath);
}

void forgetLogin(const QString &host)
{
    logins().remove(host);
}

QString loginControlPath(const QString &host)
{
    const QString path = logins().value(host);
    // The record is only half the answer: a control master that died takes its socket with it.
    if (path.isEmpty() || !QFileInfo::exists(path)) return {};
    return path;
}

bool loginLive(const QString &host)
{
    return !loginControlPath(host).isEmpty();
}

// ----- RemoteFile --------------------------------------------------------------------------------

RemoteFile::RemoteFile(QObject *parent) : QObject(parent) {}

RemoteFile::~RemoteFile()
{
    cancel();
}

void RemoteFile::setHost(const QString &host, const QString &controlPath)
{
    m_host = host;
    m_explicitControlPath = controlPath;
    if (!controlPath.isEmpty()) announceLogin(host, controlPath);
}

QString RemoteFile::controlPath() const
{
    if (!m_explicitControlPath.isEmpty() && QFileInfo::exists(m_explicitControlPath)) return m_explicitControlPath;
    // The socket this pane was opened with is gone, but the user may have logged in again: the
    // live record is what answers then, which is how a save survives a dropped connection.
    return loginControlPath(m_host);
}

void RemoteFile::fetch(const QString &path)
{
    if (path != m_path) m_fetched = {};
    m_path = path;
    m_checking = false;
    run(fetchScript(path), {}, false, false);
}

void RemoteFile::check()
{
    m_checking = true;
    if (m_path.isEmpty()) { fail(QStringLiteral("There is no file to check.")); return; }
    run(statScript(m_path), {}, false, false);
}

void RemoteFile::save(const QByteArray &content, bool force)
{
    m_checking = false;
    if (m_path.isEmpty()) { fail(QStringLiteral("There is no file to save.")); return; }
    m_pending = content;
    run(saveScript(m_path, force ? FileStat() : m_fetched), content, true, force);
}

void RemoteFile::cancel()
{
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    if (!m_process) return;
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect();
    process->kill();
    process->deleteLater();
}

void RemoteFile::run(const QString &script, const QByteArray &input, bool saving, bool force)
{
    cancel();
    m_saving = saving;
    m_forced = force;
    const QString socket = controlPath();
    if (m_host.isEmpty() || socket.isEmpty()) {
        fail(saving
                 ? QStringLiteral("The connection to %1 is gone · your edits are still here; log in to %1 again and save.").arg(m_host)
                 : QStringLiteral("There is no connection to %1 · log in to it in a terminal pane, then reload this pane.").arg(m_host));
        return;
    }
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus status) {
        finish(code, status == QProcess::CrashExit);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_process) return;
        if (error == QProcess::FailedToStart) {
            const QString host = m_host;
            cancel();
            fail(QStringLiteral("ssh could not be started, so %1 cannot be reached.").arg(host));
        }
    });
    // 30 s: long enough for a slow link and eight megabytes, short enough that a wedged
    // connection does not leave a pane waiting for ever.
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        const QString host = m_host;
        cancel();
        fail(QStringLiteral("%1 did not answer in time.").arg(host));
    });
    m_timeout->start(30000);
    m_process->start(QStringLiteral("ssh"), sshCommand(m_host, socket, script));
    if (saving) {
        m_process->write(input);
        m_process->closeWriteChannel();
    } else {
        m_process->closeWriteChannel();
    }
}

void RemoteFile::finish(int code, bool crashed)
{
    if (!m_process) return;
    QProcess *process = m_process;
    m_process = nullptr;
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    const QByteArray out = process->readAllStandardOutput();
    const QByteArray err = process->readAllStandardError();
    process->deleteLater();
    if (crashed) { fail(QStringLiteral("The connection to %1 ended before the file did.").arg(m_host)); return; }

    if (m_checking) {
        if (code == MissingStatus) { if (onChecked) onChecked(FileStat(), QString()); return; }
        const FileStat now = parseStat(out.trimmed());
        if (code != OkStatus || !now.ok) { fail(statusMessage(code == OkStatus ? int(NoStatStatus) : code, m_host, m_path, err)); return; }
        if (onChecked) onChecked(now, QString());
        return;
    }
    if (code == ChangedStatus) {
        const FileStat now = parseStat(out.trimmed());
        fail(conflictMessage(m_host, m_path, m_fetched, now), conflictOf(m_fetched, now), now);
        return;
    }
    if (code != OkStatus) { fail(statusMessage(code, m_host, m_path, err)); return; }

    const int newline = out.indexOf('\n');
    const FileStat stat = parseStat(newline < 0 ? out : out.left(newline));
    if (!stat.ok) { fail(statusMessage(NoStatStatus, m_host, m_path, err)); return; }
    if (m_saving) {
        m_fetched = stat;
        m_pending.clear();
        if (onSaved) onSaved(stat);
        return;
    }
    // The bytes as they are. Whether binary is a problem depends on what asked for them: an
    // image and a PDF are binary and are meant to be, and only the text viewer refuses one.
    const QByteArray content = newline < 0 ? QByteArray() : out.mid(newline + 1);
    m_fetched = stat;
    if (onFetched) onFetched(content, stat);
}

void RemoteFile::fail(const QString &message, Conflict conflict, const FileStat &now)
{
    if (m_checking) {
        if (onChecked) onChecked(FileStat(), message.isEmpty() ? QStringLiteral("failed") : message);
        return;
    }
    if (onFailed) onFailed(message, conflict, now);
}

// ----- RemoteDir ----------------------------------------------------------------------------------

RemoteDir::RemoteDir(QObject *parent) : QObject(parent) {}

RemoteDir::~RemoteDir()
{
    cancel();
}

void RemoteDir::setHost(const QString &host, const QString &controlPath)
{
    m_host = host;
    m_explicitControlPath = controlPath;
    if (!controlPath.isEmpty()) announceLogin(host, controlPath);
}

QString RemoteDir::controlPath() const
{
    if (!m_explicitControlPath.isEmpty() && QFileInfo::exists(m_explicitControlPath)) return m_explicitControlPath;
    return loginControlPath(m_host);
}

void RemoteDir::cancel()
{
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    if (!m_process) return;
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect();
    process->kill();
    process->deleteLater();
}

void RemoteDir::list(const QString &path)
{
    cancel();
    m_path = path;
    const QString socket = controlPath();
    if (m_host.isEmpty() || socket.isEmpty()) {
        if (onFailed)
            onFailed(QStringLiteral("There is no connection to %1 · log in to it in a terminal pane, then reload this pane.").arg(m_host));
        return;
    }
    m_process = new QProcess(this);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) { finish(code, status == QProcess::CrashExit); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_process && error == QProcess::FailedToStart) {
            const QString host = m_host;
            cancel();
            if (onFailed) onFailed(QStringLiteral("ssh could not be started, so %1 cannot be reached.").arg(host));
        }
    });
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        const QString host = m_host;
        cancel();
        if (onFailed) onFailed(QStringLiteral("%1 did not answer in time.").arg(host));
    });
    m_timeout->start(20000);
    m_process->start(QStringLiteral("ssh"), sshCommand(m_host, socket, listScript(path)));
    m_process->closeWriteChannel();
}

void RemoteDir::finish(int code, bool crashed)
{
    if (!m_process) return;
    QProcess *process = m_process;
    m_process = nullptr;
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    const QByteArray out = process->readAllStandardOutput();
    const QByteArray err = process->readAllStandardError();
    process->deleteLater();
    if (crashed) {
        if (onFailed) onFailed(QStringLiteral("The connection to %1 ended before the folder did.").arg(m_host));
        return;
    }
    if (code != OkStatus) {
        if (onFailed) onFailed(code == MissingStatus
                                   ? QStringLiteral("%1 is not a folder you can open on %2.").arg(m_path, m_host)
                                   : statusMessage(code, m_host, m_path, err));
        return;
    }
    bool truncated = false;
    const QVector<DirEntry> entries = parseListing(out, kMaxDirEntries, &truncated);
    if (onListed) onListed(m_path, entries, truncated);
}

// ----- PathProbe ----------------------------------------------------------------------------------

PathProbe::PathProbe(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { startBatch(); });
}

PathProbe::~PathProbe()
{
    if (m_process) { m_process->disconnect(); m_process->kill(); }
}

void PathProbe::setHost(const QString &host, const QString &controlPath)
{
    if (host == m_host && controlPath == m_explicitControlPath) return;
    m_host = host;
    m_explicitControlPath = controlPath;
    clear();
}

void PathProbe::clear()
{
    m_cache.clear();
    m_queue.clear();
    if (m_timer) m_timer->stop();
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect();
        process->kill();
        process->deleteLater();
    }
}

Entry PathProbe::lookup(const QString &path)
{
    if (m_host.isEmpty() || path.isEmpty()) return Entry::Unknown;
    const auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd()) return it.value();
    if (!m_queue.contains(path) && m_queue.size() < kMaxQueued) {
        m_queue.append(path);
        schedule();
    }
    return Entry::Unknown;
}

void PathProbe::schedule()
{
    if (m_process || m_queue.isEmpty() || !m_timer || m_timer->isActive()) return;
    m_timer->start(kDelayMs);
}

void PathProbe::startBatch()
{
    if (m_process || m_queue.isEmpty()) return;
    const QString socket = m_explicitControlPath.isEmpty() || !QFileInfo::exists(m_explicitControlPath)
                               ? loginControlPath(m_host) : m_explicitControlPath;
    if (socket.isEmpty()) { m_queue.clear(); return; }
    const QStringList asked = m_queue.mid(0, kBatch);
    m_queue = m_queue.mid(asked.size());
    m_process = new QProcess(this);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, asked](int, QProcess::ExitStatus) { finishBatch(asked); });
    connect(m_process, &QProcess::errorOccurred, this, [this, asked](QProcess::ProcessError error) {
        if (m_process && error == QProcess::FailedToStart) finishBatch(asked);
    });
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this, asked] {
        if (m_process) m_process->kill();
        finishBatch(asked);
    });
    m_timeout->start(kTimeoutMs);
    m_process->start(QStringLiteral("ssh"), sshCommand(m_host, socket, probeScript(asked)));
    m_process->closeWriteChannel();
}

void PathProbe::finishBatch(const QStringList &asked)
{
    if (m_timeout) { m_timeout->stop(); m_timeout->deleteLater(); m_timeout = nullptr; }
    QByteArray out;
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect();
        out = process->readAllStandardOutput();
        process->deleteLater();
    }
    const QVector<Entry> answers = parseProbe(out, int(asked.size()));
    bool learned = false;
    for (int i = 0; i < asked.size(); ++i) {
        if (answers.at(i) == Entry::Unknown) continue;   // ask again another time
        if (m_cache.size() >= kMaxCached) m_cache.clear();
        m_cache.insert(asked.at(i), answers.at(i));
        learned = true;
    }
    if (learned && onAnswers) onAnswers();
    // The next batch waits a moment: a hover over a wall of output must not become a stream of
    // ssh calls on the user's own connection (sshd counts sessions, Warp's #1957).
    if (!m_queue.isEmpty() && m_timer) m_timer->start(kQuietMs);
}

}  // namespace relay::remote
