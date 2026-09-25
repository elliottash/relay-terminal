// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Files that live on the host a pane is logged into (card #S5SH, docs/SSH-AND-MOSH.md § 9).
//
// A path printed by `ssh filly` names a file on filly, not one here. Clicking it used to say so
// and stop. This is the other half: fetch that file over the login the user already has open, show
// it in an ordinary preview pane and save the edited bytes back — owner, 2026-09-18, "editing
// allowed so it's equal to local text editing".
//
// Everything runs over the user's own authenticated connection, exactly as `run_command` with a
// `host` does (docs/SSH-AND-MOSH.md § 7): `ssh -S <control path> -o ControlMaster=no
// -o BatchMode=yes -o ConnectTimeout=10 -T <host> -- sh -c '<script>'`. No second login, no
// password, nothing installed on the host. The file's bytes go over ssh's *stdin*, never in argv,
// where a `ps` on the host would show them.
//
// Three kinds of thing live here:
//
//  * The pure parts — shell quoting, the scripts, `stat -c %s:%Y:%a` parsing, the "it changed
//    under us" decision, the `ssh://host/path` form a pane hands to a preview. No process, no
//    network, no widget: tests/remotefiles_test.cpp checks all of it directly.
//  * `RemoteFile`: one file, fetched and saved asynchronously with QProcess. Nothing blocks the
//    GUI thread and every failure comes back as a sentence a person can read.
//  * `PathProbe`: the batched `test -e` cache that lets the terminal view underline a path
//    *because it exists on the host*, rather than because a path of the same name exists here.
//
// Live logins are announced here (`announceLogin`) so a preview pane can reach the host without
// holding a pointer back into the pane that opened it — and so that a save after the login ended
// fails with "the connection is gone" instead of writing somewhere unexpected.
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class QProcess;
class QTimer;

namespace relay::remote {

// ----- shell quoting -----------------------------------------------------------------------
// `text` as one POSIX shell word: single quotes, with `'` spelled `'\''`. Safe for spaces,
// quotes, `$`, backticks, newlines, globs and UTF-8 alike, and for the empty string.
QString shellQuote(const QString &text);
QString shellQuote(const QStringList &words);   // joined with spaces, each quoted

// ----- what the host says about a file -------------------------------------------------------

// `stat -c %s:%Y:%a` — size, mtime (seconds), mode. The same three fields a save compares to
// decide whether the file changed on the host since it was fetched, and where the mode it keeps
// comes from.
struct FileStat {
    qint64 size = -1;
    qint64 mtime = -1;
    QString mode;      // "644", "1777"; empty when the host's stat did not give one
    bool ok = false;

    bool operator==(const FileStat &other) const {
        return ok == other.ok && size == other.size && mtime == other.mtime && mode == other.mode;
    }
    bool operator!=(const FileStat &other) const { return !(*this == other); }
    // The form the remote scripts print and compare, so the host itself can do the comparison
    // between the check and the `mv` (nothing else can make that race narrower).
    QString line() const;
};

FileStat parseStat(const QByteArray &line);

// Why a save was refused before it wrote anything.
enum class Conflict {
    None,      // the host's copy is the one that was fetched
    Changed,   // something else wrote it (or chmod'd it) meanwhile
    Vanished,  // it is not there any more
};
Conflict conflictOf(const FileStat &fetched, const FileStat &now);
// "nginx.conf changed on filly since you opened it (…)." — what the pane puts to the user above
// the Overwrite / Reload choice.
QString conflictMessage(const QString &host, const QString &path, const FileStat &fetched, const FileStat &now);

// A file with NUL in its first kilobyte is binary and is not opened as text.
constexpr int kBinarySniffBytes = 1024;
bool looksBinary(const QByteArray &head);

// ----- the remote scripts ---------------------------------------------------------------------
// Each is one POSIX `sh` script, run as a single argument. They print the stat line first and the
// file after it, so one round trip carries both.

constexpr qint64 kMaxFileBytes = 8 * 1024 * 1024;   // a bigger file is refused, with its size

// Exit codes the scripts use. Anything else is ssh's own (255: the connection) or the remote
// shell's (127: no `sh`).
enum Status {
    OkStatus = 0,
    MissingStatus = 10,
    UnreadableStatus = 11,
    DirectoryStatus = 12,
    TooLargeStatus = 13,
    NoStatStatus = 14,
    ChangedStatus = 20,   // save only: the host's copy moved on; the current stat is on stdout
    NoTempStatus = 21,
    WriteFailedStatus = 22,
    MoveFailedStatus = 23,
};

QString fetchScript(const QString &path, qint64 maxBytes = kMaxFileBytes);
// `expected` empty saves whatever is there (the user answered "overwrite anyway"); otherwise the
// host compares it to the file's own stat and refuses with ChangedStatus before writing a byte.
QString saveScript(const QString &path, const FileStat &expected);
QString probeScript(const QStringList &paths);
// One folder: `<type>|<size>|<mtime>|<name>` a line, dot files included, symlinks followed.
QString listScript(const QString &path);

// The ssh arguments that reuse the user's login, up to and including the destination and `--`.
QStringList sshArguments(const QString &host, const QString &controlPath);
// Those arguments plus `sh -c <script>`: the whole argv for QProcess::start("ssh", …).
QStringList sshCommand(const QString &host, const QString &controlPath, const QString &script);

// What a probe batch answers, one per path, in the order they were asked.
enum class Entry { Unknown = -2, Missing = -1, File = 0, Directory = 1 };
QVector<Entry> parseProbe(const QByteArray &output, int expected);

// One row of a folder listing. `name` is the entry's own name, not a path.
struct DirEntry {
    QString name;
    bool directory = false;
    qint64 size = -1;
    qint64 mtime = -1;
};
constexpr int kMaxDirEntries = 2000;   // a listing longer than this is cut, and says so
// Folders first, then files, each by name, case-insensitively — the order QFileSystemModel
// shows a local folder in. `truncated` is set when the host had more than `maxEntries`.
QVector<DirEntry> parseListing(const QByteArray &output, int maxEntries = kMaxDirEntries,
                               bool *truncated = nullptr);

// A failure, as a sentence. `stderr` is the host's own words (`Permission denied`, `No space left
// on device`), which are usually the whole answer.
QString statusMessage(int exitStatus, const QString &host, const QString &path, const QByteArray &errorOutput);

// ----- `ssh://host/path`: how a pane names a remote file to a preview pane ---------------------
// A preview pane keeps a path string, saves it with the window layout and hands it back to
// `open()`. A remote file needs a host as well, so it travels as a URL. The control path is never
// in it: it is looked up from the live logins, so a URL saved with the layout can never point at
// a socket that has since belonged to something else.
QString fileUrl(const QString &host, const QString &path);
// The same, for a folder: it ends in `/`, the way a URL has always told a collection from a
// document. That one character is how `RelayWindow::openPath` knows to open an explorer pane
// rather than a preview without asking the host a second time.
QString folderUrl(const QString &host, const QString &path);
bool isFileUrl(const QString &target);
struct FileRef {
    QString host;
    QString path;          // without the trailing `/` of a folder, except for the root itself
    bool directory = false;
    bool ok = false;
};
FileRef parseFileUrl(const QString &target);
// "filly:/etc/nginx/nginx.conf" — the title of a pane showing it.
QString displayName(const QString &host, const QString &path);
// The folder holding `path`, and the path of `name` inside it. Plain string work on the host's
// spelling: QFileInfo and QDir would answer about this machine's idea of a path.
QString parentPath(const QString &path);
QString childPath(const QString &path, const QString &name);

// ----- live logins ----------------------------------------------------------------------------
// A pane announces its login when it resolves one and forgets it when the login ends. The socket
// is checked as well as the record, so a control master that died without ceremony reads as gone.
void announceLogin(const QString &host, const QString &controlPath);
void forgetLogin(const QString &host);
QString loginControlPath(const QString &host);
bool loginLive(const QString &host);

// ----- one file on the host --------------------------------------------------------------------
class RemoteFile final : public QObject {
public:
    explicit RemoteFile(QObject *parent = nullptr);
    ~RemoteFile() override;

    // The host as the login names it ("filly"); the control path may be left empty, and then the
    // live-login record answers for it at every fetch and save.
    void setHost(const QString &host, const QString &controlPath = QString());
    QString host() const { return m_host; }
    QString path() const { return m_path; }
    // The socket this would use right now, or empty when the login is gone.
    QString controlPath() const;
    bool live() const { return !controlPath().isEmpty(); }
    bool busy() const { return m_process != nullptr; }
    const FileStat &fetched() const { return m_fetched; }

    // The bytes, whatever they are: an image and a PDF are binary and are meant to be. Refusing
    // binary is the *text editor's* rule, not the transport's (`looksBinary`).
    void fetch(const QString &path);
    // `force` is the user's "overwrite anyway" after a Changed refusal.
    void save(const QByteArray &content, bool force = false);
    void cancel();

    std::function<void(const QByteArray &content, const FileStat &)> onFetched;
    std::function<void(const FileStat &)> onSaved;
    // `conflict` is the host's stat when the file changed under us (the caller then offers
    // Overwrite / Reload); `message` is a sentence in every case.
    std::function<void(const QString &message, Conflict conflict, const FileStat &nowOnHost)> onFailed;

private:
    void run(const QString &script, const QByteArray &input, bool saving, bool force);
    void finish(int code, bool crashed);
    void fail(const QString &message, Conflict conflict = Conflict::None, const FileStat &now = {});

    QString m_host, m_explicitControlPath, m_path;
    FileStat m_fetched;
    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    bool m_saving = false, m_forced = false;
    QByteArray m_pending;   // the bytes a save is writing, kept until it lands
};

// ----- one folder on the host -------------------------------------------------------------------
class RemoteDir final : public QObject {
public:
    explicit RemoteDir(QObject *parent = nullptr);
    ~RemoteDir() override;

    void setHost(const QString &host, const QString &controlPath = QString());
    QString host() const { return m_host; }
    QString path() const { return m_path; }
    QString controlPath() const;
    bool live() const { return !controlPath().isEmpty(); }
    bool busy() const { return m_process != nullptr; }

    void list(const QString &path);
    void cancel();

    std::function<void(const QString &path, const QVector<DirEntry> &entries, bool truncated)> onListed;
    std::function<void(const QString &message)> onFailed;

private:
    void finish(int code, bool crashed);

    QString m_host, m_explicitControlPath, m_path;
    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
};

// ----- do these paths exist on the host? -------------------------------------------------------
//
// The terminal view asks a `links::Probe` whether each candidate path exists before it underlines
// it, and that probe answers about *this* machine. Under a login that is the wrong machine: most
// remote paths are not links at all, and the ones that happen to exist here are links for the
// wrong reason (docs/SSH-AND-MOSH.md § 9).
//
// A probe cannot wait for the network — it is called from a mouse-move. So this answers from a
// cache, queues what it does not know, and checks a batch of them over the same socket a moment
// later; the view asks again on the next hover or scan and the answer is there. Nothing is asked
// twice, no more than one batch is in flight, and the whole cache goes when the login ends.
class PathProbe final : public QObject {
public:
    static constexpr int kBatch = 24;        // paths per ssh call
    static constexpr int kDelayMs = 120;     // gather candidates this long before asking
    static constexpr int kQuietMs = 150;     // and leave at least this long between batches
    static constexpr int kMaxCached = 4000;
    static constexpr int kMaxQueued = 200;
    static constexpr int kTimeoutMs = 8000;

    explicit PathProbe(QObject *parent = nullptr);
    ~PathProbe() override;

    // Changing the host (or the socket) drops everything the old one answered.
    void setHost(const QString &host, const QString &controlPath = QString());
    QString host() const { return m_host; }
    void clear();

    // The cached answer, queueing the path when there is none. Never blocks.
    Entry lookup(const QString &path);
    int cachedCount() const { return int(m_cache.size()); }
    int queuedCount() const { return int(m_queue.size()); }

    // A batch came back: what the view drew is out of date, ask it to look again.
    std::function<void()> onAnswers;

private:
    void schedule();
    void startBatch();
    void finishBatch(const QStringList &asked);

    QString m_host, m_explicitControlPath;
    QHash<QString, Entry> m_cache;
    QStringList m_queue;
    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_timeout = nullptr;
};

}  // namespace relay::remote
