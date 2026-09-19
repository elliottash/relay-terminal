// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Claude IDE bridge, the GUI's end (issue GT7X, protocol 26.5).
//
// Relay plays the *editor* side of Claude Code's IDE integration, so a `claude` the user starts in
// a pane gets Relay's diff view instead of VS Code's. The editor is one process for the whole GUI
// run — the sidecar `backend/relay_core/guest_bridge.py`, which owns the loopback WebSocket, the
// JSON-RPC surface and the `~/.claude/ide/<port>.lock` file a claude reads to find it. This class
// is everything the GUI has to do about it, and it is deliberately small:
//
//   * **Start it, once.** The first pane whose shell is about to start asks for the port
//     (`portFor`). The sidecar binds an ephemeral loopback port, writes its lock, sweeps locks
//     whose IDE is gone, and prints one ready line on stdout; that line is where the port comes
//     from. Nothing is started until a pane needs it, and nothing is retried after a failure: a
//     pane works fine without the bridge, and a GUI that retried every 80 ms poll would not.
//   * **Say where the panes are.** The sidecar routes a request to a pane by the longest
//     workspace/cwd prefix of the paths it names, so it needs each claude pane's token, runtime
//     dir, workspace and cwd (`registerPane`). A registration is a small JSON file in the run's
//     own state directory under /tmp, written atomically; the sidecar polls that directory. A pane
//     that closes, or whose claude exits, is unregistered.
//   * **Answer a diff.** `openDiff` blocks inside the guest until the user decides. The pane shows
//     the diff and calls `answerDiff` with FILE_SAVED or DIFF_REJECTED; on FILE_SAVED the *sidecar*
//     writes the file, so the GUI never writes a user file from a bridge event (26.5). The reply
//     path is checked to be inside this run's replies directory before anything is written to it.
//
// The environment that makes claude look for us — `CLAUDE_CODE_SSE_PORT` and
// `ENABLE_IDE_INTEGRATION`, i.e. `guest.bridge_env("claude", port)` — is injected by the pane at
// the `startTerminal` qputenv block (26.2), because it has to be in the shell's environment before
// a claude could start in it. The bridge is off unless Options › Guests turns it on
// (`guests/claude_bridge`), which is the hooks phase's sibling key (`guests/project_install`).
//
// The state directory is a `QTemporaryDir` named `relay-XXXXXX` and marked with
// relay::runtimedirs' owner file, so a GUI that dies without saying goodbye leaves a directory the
// next Relay's startup sweep removes (issue 9JYK) rather than one that accumulates in /tmp.

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "Logging.h"
#include "RuntimeDirs.h"

#ifndef RELAY_VERSION
#define RELAY_VERSION "0.0.0-dev"
#endif

namespace relay::guestbridge {

// One bridge per GUI run, and off by default (26.3's settings rules: the guest integration is
// opt-in, and this is the bridge's own switch).
inline bool enabled() {
    return QSettings().value(QStringLiteral("guests/claude_bridge"), false).toBool();
}

// The two answers a waiting openDiff can get (26.5). Upstream's own strings — claude compares them
// — and the vocabulary lives here so the pane and the sidecar cannot drift apart.
inline QString fileSaved() { return QStringLiteral("FILE_SAVED"); }
inline QString diffRejected() { return QStringLiteral("DIFF_REJECTED"); }

// `guest.bridge_env(guest, port)` in C++ (backend/relay_core/guest.py): the environment a pane's
// shell needs for a guest to find Relay's bridge. Empty for anything but claude — codex has no IDE
// bridge, and an unknown guest is not a guess — and empty for a port outside 1..65535, which is
// what "the bridge is off, or never started" looks like to a caller. One rule in two languages:
// change bridge_env with this.
inline QJsonObject bridgeEnv(const QString &guest, int port) {
    if (guest != QStringLiteral("claude") || port < 1 || port > 65535) return {};
    return QJsonObject{{QStringLiteral("CLAUDE_CODE_SSE_PORT"), QString::number(port)},
                       {QStringLiteral("ENABLE_IDE_INTEGRATION"), QStringLiteral("true")}};
}

// Every variable `bridgeEnv` can set, whether or not this call sets it. The pane's environment is
// the *GUI process's* environment (`qputenv`), which outlives the pane that wrote it: iterating
// over what `bridgeEnv` returned removed nothing when it returned nothing, so a run whose sidecar
// had died — or whose bridge the user had just switched off — went on handing every later shell the
// port of a socket nobody was listening on, and every claude started in one hung looking for it.
// Clear from this list; set from `bridgeEnv`.
inline QStringList bridgeEnvKeys() {
    return {QStringLiteral("CLAUDE_CODE_SSE_PORT"), QStringLiteral("ENABLE_IDE_INTEGRATION")};
}

// May this reply path be written? The one rule `answerDiff` enforces: inside the replies
// directory it names, and that is all. The reply file does not exist yet — it is what the call
// writes — so its own path is absolute-and-cleaned, not canonical (`canonicalFilePath()` is empty
// for a file that is not there, which once made every answer a refusal); the clean also resolves
// a `..`, so a name cannot climb back out of the directory.
inline bool replyAllowed(const QString &repliesDir, const QString &replyPath) {
    const QString inside = QFileInfo(repliesDir).canonicalFilePath();
    const QString reply = QDir::cleanPath(QFileInfo(replyPath).absoluteFilePath());
    return !inside.isEmpty() && !reply.isEmpty() && reply.startsWith(inside + QLatin1Char('/'));
}

class Bridge {
public:
    static Bridge &instance() {
        static Bridge bridge;
        return bridge;
    }
    // Whether a pane has ever asked the bridge for a port. A pane closing on a run that never
    // used it must not bring its machinery (a state directory, a QProcess) into being just to say
    // goodbye — which is why this is *not* set by `instance()`: constructing the singleton is
    // exactly the thing the flag exists to avoid, so a `started()` that `instance()` had already
    // made true was a guard that could never fire.
    static bool started() { return s_started; }

    // Where Relay's data files are (backend/, shell/), so the sidecar can be run from the source
    // tree or an install. Set by the pane before it asks for a port.
    void setDataRoot(const QString &data) { m_data = data; }

    // The loopback port a pane's shell should be told about, or 0 for "inject nothing". The first
    // call starts the sidecar and waits for its ready line; every later call is a field read.
    int portFor(const QString &python) {
        if (!enabled()) return 0;
        s_started = true;   // asked for: from here on a closing pane has something to say goodbye to
        if (running()) return m_port;
        if (m_failed) return 0;
        start(python);
        return m_port;
    }

    bool running() const { return m_process.state() == QProcess::Running && m_port > 0; }

    // One pane, as the sidecar's router sees it. Written whenever the token, runtime dir,
    // workspace or cwd it names changes; the sidecar reads the directory, so a rewrite is what
    // tells it a pane moved — and a write that would say exactly what the last one said is
    // skipped, so a caller may ask on every cwd change. False means "not registered" (the bridge
    // is off, or failed), which the caller retries rather than remembering.
    bool registerPane(const QString &token, const QString &runtimeDir, const QString &workspace,
                      const QString &cwd, const QString &python, const QString &guest) {
        if (!running() || token.isEmpty() || runtimeDir.isEmpty()) return false;
        const QJsonObject payload{
            {QStringLiteral("token"), token},
            {QStringLiteral("runtime_dir"), runtimeDir},
            // The hooks phase's helper, which becomes the channel's only writer once it exists;
            // the sidecar checks for the file itself (26.5).
            {QStringLiteral("helper"), m_data + QStringLiteral("/shell/guest-event.py")},
            {QStringLiteral("python"), python},
            {QStringLiteral("workspace"), workspace},
            {QStringLiteral("cwd"), cwd},
            // Which guest is in this pane's foreground right now, or "". Two panes open on one
            // project are ordinary — every pane registers, because the port is in its shell's
            // environment before a claude could be started there — and the one with a claude in it
            // is the one a request from a claude belongs to (26.5).
            {QStringLiteral("guest"), guest}};
        const QByteArray bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        if (m_written.value(token) == bytes) return true;
        const QString path = m_stateDir + QStringLiteral("/panes/") + token + QStringLiteral(".json");
        if (!writeBytes(path, bytes)) {
            relay::log::error(QStringLiteral("guest_bridge_pane_write_failed pane=%1")
                                  .arg(token.left(8)));
            return false;
        }
        m_written.insert(token, bytes);
        return true;
    }

    void unregisterPane(const QString &token) {
        if (token.isEmpty() || m_stateDir.isEmpty()) return;
        QFile::remove(m_stateDir + QStringLiteral("/panes/") + token + QStringLiteral(".json"));
        m_written.remove(token);
    }

    // The pane's answer to a blocking openDiff. The sidecar unlinks the reply as it reads it, so a
    // duplicate is harmless; anything outside this run's replies directory is refused, so a bridge
    // event can never name a file for Relay to overwrite.
    bool answerDiff(const QString &replyPath, const QString &outcome) {
        if (replyPath.isEmpty()) return false;
        if (!replyAllowed(m_repliesDir, replyPath)) {
            relay::log::error(QStringLiteral("guest_bridge_reply_refused path=%1").arg(replyPath));
            return false;
        }
        return writeBytes(replyPath, QJsonDocument(QJsonObject{{QStringLiteral("outcome"), outcome}})
                                    .toJson(QJsonDocument::Compact));
    }

    // Where the sidecar keeps this run's pane registrations and diff replies.
    QString stateDir() const { return m_stateDir; }

private:
    Bridge() {
        QObject::connect(&m_process, &QProcess::readyReadStandardOutput, &m_process, [this] {
            m_stdout += m_process.readAllStandardOutput();
            for (int newline = m_stdout.indexOf('\n'); newline >= 0; newline = m_stdout.indexOf('\n')) {
                const QByteArray line = m_stdout.left(newline);
                m_stdout.remove(0, newline + 1);
                handleLine(line);
            }
        });
        QObject::connect(&m_process, &QProcess::readyReadStandardError, &m_process, [this] {
            // The sidecar's own log, one line each (tool names, routing misses, openDiff outcomes):
            // Relay's log keeps it, nothing shows it. No file contents ever reach it.
            for (const QByteArray &row : m_process.readAllStandardError().split('\n')) {
                const QString line = QString::fromUtf8(row).trimmed();
                if (!line.isEmpty()) relay::log::debug(QStringLiteral("guest_bridge %1").arg(line));
            }
        });
        QObject::connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &m_process,
                         [this](int code, QProcess::ExitStatus status) {
            relay::log::info(QStringLiteral("guest_bridge_exit code=%1 crashed=%2")
                                 .arg(code).arg(status == QProcess::CrashExit ? 1 : 0));
            m_port = 0;
        });
        QObject::connect(&m_process, &QProcess::errorOccurred, &m_process, [this](QProcess::ProcessError) {
            relay::log::error(QStringLiteral("guest_bridge_failed error=%1").arg(m_process.errorString()));
            m_port = 0;
            m_failed = true;   // a pane works without the bridge; a retry per poll would not
        });
    }

    ~Bridge() {
        if (m_process.state() == QProcess::NotRunning) return;
        // SIGTERM is the sidecar's own quit path: it removes its lock file and exits (26.5).
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }

    void start(const QString &python) {
        const QString interpreter = python.isEmpty()
                                        ? QStandardPaths::findExecutable(QStringLiteral("python3"))
                                        : python;
        if (interpreter.isEmpty() || m_data.isEmpty()) {
            relay::log::error(QStringLiteral("guest_bridge_unavailable python=%1 data=%2")
                                  .arg(interpreter, m_data));
            m_failed = true;
            return;
        }
        m_stateDir = m_runtime.path();
        m_repliesDir = m_stateDir + QStringLiteral("/replies");
        QDir().mkpath(m_repliesDir);
        QDir().mkpath(m_stateDir + QStringLiteral("/panes"));
        // Mark the directory as this process's, so the next Relay's startup sweep can tell it from
        // a live one's and remove it if we are killed rather than quit (#9JYK).
        relay::runtimedirs::markOwned(m_stateDir);
        const QStringList arguments{
            QStringLiteral("-u"), QStringLiteral("-m"), QStringLiteral("relay_core.guest_bridge"),
            QStringLiteral("serve"), QStringLiteral("--state-dir"), m_stateDir,
            QStringLiteral("--relay-version"), QString::fromLatin1(RELAY_VERSION)};
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        // The sidecar is `relay_core.guest_bridge`, which lives in the tree's backend/ (or the
        // install's copy of it); the pane's own RELAY_DATA_DIR is what names that root.
        environment.insert(QStringLiteral("PYTHONPATH"),
                           m_data + QStringLiteral("/backend")
                               + (environment.value(QStringLiteral("PYTHONPATH")).isEmpty()
                                      ? QString()
                                      : QLatin1Char(':') + environment.value(QStringLiteral("PYTHONPATH"))));
        environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
        m_process.setProcessEnvironment(environment);
        m_process.setProgram(interpreter);
        m_process.setArguments(arguments);
        relay::log::info(QStringLiteral("guest_bridge_start python=%1 state=%2").arg(interpreter, m_stateDir));
        m_process.start();
        if (!m_process.waitForStarted(5000)) {
            relay::log::error(QStringLiteral("guest_bridge_start_failed error=%1").arg(m_process.errorString()));
            m_failed = true;
            return;
        }
        // The ready line is one short line, printed as soon as the port is bound, and this runs on
        // the first pane's startup path: bounded, because a hung interpreter must not stall the GUI.
        QElapsedTimer clock;
        clock.start();
        while (m_port <= 0 && m_process.state() == QProcess::Running && clock.elapsed() < 5000) {
            const qint64 left = 5000 - clock.elapsed();
            if (!m_process.waitForReadyRead(int(left > 0 ? left : 1))) break;
        }
        if (m_port <= 0) {
            relay::log::error(QStringLiteral("guest_bridge_no_ready_line state=%1").arg(m_stateDir));
            m_failed = true;
            m_process.kill();
        }
    }

    // The one line the sidecar writes on stdout: `{"ready": true, "port": N, "lock": path}`.
    void handleLine(const QByteArray &line) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return;
        const QJsonObject payload = document.object();
        const QString lock = payload.value(QStringLiteral("lock")).toString();
        if (!payload.value(QStringLiteral("ready")).toBool()) {
            relay::log::error(QStringLiteral("guest_bridge_not_ready error=%1")
                                  .arg(payload.value(QStringLiteral("error")).toString()));
            m_failed = true;
            return;
        }
        m_port = payload.value(QStringLiteral("port")).toInt();
        relay::log::info(QStringLiteral("guest_bridge_ready port=%1 lock=%2").arg(m_port).arg(lock));
    }

    // Mode 0600, atomically (temp file + rename): the sidecar never reads half a registration, and
    // no other user reads either file — a registration names the pane's runtime dir, a reply names
    // nothing at all.
    static bool writeBytes(const QString &path, const QByteArray &bytes) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        return file.write(bytes) == bytes.size() && file.commit();
    }

    // The run's private directory, marked so a Relay that starts after a crash sweeps it (#9JYK).
    QTemporaryDir m_runtime{QDir::tempPath() + QStringLiteral("/relay-XXXXXX")};
    QProcess m_process;
    QByteArray m_stdout;
    QString m_data, m_stateDir, m_repliesDir;
    QHash<QString, QByteArray> m_written;   // pane token -> the registration file's own bytes
    int m_port = 0;
    bool m_failed = false;
    static inline bool s_started = false;
};

}   // namespace relay::guestbridge
