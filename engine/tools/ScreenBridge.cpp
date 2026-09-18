// SPDX-License-Identifier: GPL-3.0-or-later
// relay-screen-bridge: one shell in a PTY, driven by Relay's own emulator, streamed as screen
// state on stdout.
//
// This is the P2 "stream state, not bytes" path from docs/REMOTE-PROTOCOL.md section 6.5, in the
// smallest form that can be used before the GUI grows a RemoteHub: the remote host
// (remote/terminal.py) runs one of these per shared pane and forwards what comes out.
//
// It exists so that a phone never sees PTY bytes and never resizes the host session. The host is
// authoritative about size; the phone scales to fit.
//
// Protocol, one JSON object per line each way.
//
//   in   {"t":"input","bytes":"<base64>"}      keys or text for the program
//        {"t":"resize","rows":R,"cols":C}      the *desktop* resizing its own pane
//        {"t":"snapshot"}                      ask for a full frame (a phone reconnected)
//        {"t":"signal","name":"int"}           ^C without guessing an encoding
//        {"t":"quit"}
//
//   out  {"t":"hello","shell_pid":N,"rows":R,"cols":C}
//        {"t":"snapshot","rows":R,"cols":C,"alt":bool,"cursor":{...},"lines":[<row>...]}
//        {"t":"diff","cursor":{...},"lines":[<row>...]}        only rows that changed
//        {"t":"title","text":"..."} {"t":"cwd","path":"..."} {"t":"bell"}
//        {"t":"mark","kind":K,"row":R,"exit":E}                OSC 133
//        {"t":"status","foreground_pid":N,"running":bool}       tcgetpgrp on the master
//        {"t":"out","bytes":"<base64>"}     raw PTY bytes, only with --raw-out
//        {"t":"exit","code":N}
//
// --raw-out exists so the desktop can attach its own terminal to the same shell while a phone
// watches the screen state. The phone never receives these bytes.
//
//   <row> is {"row":N,"segs":[[text,fg,bg,attrs],...]} — runs of identical style, so the client
//   needs no index arithmetic and no second emulator. fg/bg are packed relay::CellColor.
#include "ScreenJson.h"
#include "session/TerminalSession.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <unistd.h>

using namespace relay;

namespace {

constexpr int kDefaultRows = 24;
constexpr int kDefaultCols = 80;
constexpr int kFrameMs = 50;        // 20 fps, matching the protocol's foreground cap
constexpr int kStatusMs = 400;      // who owns the terminal, for the pane status

void writeLine(const QJsonObject &object)
{
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    ::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    ::fflush(stdout);
}

class Bridge : public QObject {
public:
    Bridge(TerminalSession *session, int rows, int cols, bool rawOut)
        : m_session(session), m_rows(rows), m_cols(cols)
    {
        if (rawOut) {
            session->setOutputSignalEnabled(true);
            connect(session, &TerminalSession::output, this, [](const QByteArray &bytes) {
                writeLine({{"t", "out"}, {"bytes", QString::fromLatin1(bytes.toBase64())}});
            });
        }
        connect(session, &TerminalSession::titleChanged, this, [](const QString &title) {
            writeLine({{"t", "title"}, {"text", title}});
        });
        connect(session, &TerminalSession::cwdChanged, this, [](const QString &path, const QString &) {
            writeLine({{"t", "cwd"}, {"path", path}});
        });
        connect(session, &TerminalSession::bell, this, [] { writeLine({{"t", "bell"}}); });
        connect(session, &TerminalSession::altScreenChanged, this, [this](bool active) {
            m_alt = active;
            m_forceSnapshot = true;
        });
        connect(session, &TerminalSession::promptMark, this,
                [](relay::PromptMark kind, int row, int exitCode) {
                    writeLine({{"t", "mark"}, {"kind", int(kind)}, {"row", row}, {"exit", exitCode}});
                });
        connect(session, &TerminalSession::finished, this, [](int code) {
            writeLine({{"t", "exit"}, {"code", code}});
            QCoreApplication::quit();
        });

        m_frames = new QTimer(this);
        connect(m_frames, &QTimer::timeout, this, &Bridge::sendFrame);
        m_frames->start(kFrameMs);

        // Who owns the terminal. The host turns this into a pane status, and into the rule that
        // a program is running so a prompt should be queued rather than typed.
        m_status = new QTimer(this);
        connect(m_status, &QTimer::timeout, this, &Bridge::sendStatus);
        m_status->start(kStatusMs);

        m_stdin = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
        connect(m_stdin, &QSocketNotifier::activated, this, &Bridge::readCommands);
    }

    void sendHello()
    {
        writeLine({{"t", "hello"}, {"shell_pid", double(m_session->shellPid())},
                   {"rows", m_rows}, {"cols", m_cols}});
        m_forceSnapshot = true;
        sendFrame();
    }

private:
    void sendFrame()
    {
        const bool full = m_forceSnapshot;
        bool changed = false;
        m_session->withCore([&](VtCore &core) {
            changed = core.updateFrame(&m_frame, full);
        });
        if (!changed && !full) return;
        m_forceSnapshot = false;

        writeLine(relay::screenjson::frameOf(m_frame, full));
    }

    void sendStatus()
    {
        const qint64 foreground = m_session->foregroundPid();
        const qint64 shell = m_session->shellPid();
        const bool running = foreground > 0 && shell > 0 && foreground != shell;
        if (foreground == m_lastForeground && running == m_lastRunning) return;
        m_lastForeground = foreground;
        m_lastRunning = running;
        writeLine({{"t", "status"}, {"foreground_pid", double(foreground)},
                   {"running", running}});
    }

    void readCommands()
    {
        char buffer[8192];
        const ssize_t got = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (got <= 0) {                       // the host went away; take the shell with us
            QCoreApplication::quit();
            return;
        }
        m_pending.append(buffer, int(got));
        int newline;
        while ((newline = m_pending.indexOf('\n')) >= 0) {
            const QByteArray line = m_pending.left(newline);
            m_pending.remove(0, newline + 1);
            if (!line.trimmed().isEmpty()) handle(line);
        }
    }

    void handle(const QByteArray &line)
    {
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString kind = message.value("t").toString();
        if (kind == QLatin1String("input")) {
            m_session->sendInput(QByteArray::fromBase64(
                message.value("bytes").toString().toLatin1()));
        } else if (kind == QLatin1String("resize")) {
            m_rows = qBound(1, message.value("rows").toInt(m_rows), 500);
            m_cols = qBound(1, message.value("cols").toInt(m_cols), 1000);
            m_session->resize(m_rows, m_cols, 8, 16);
            m_forceSnapshot = true;
        } else if (kind == QLatin1String("snapshot")) {
            m_forceSnapshot = true;
            sendFrame();
        } else if (kind == QLatin1String("signal")) {
            const qint64 pid = m_session->foregroundPid();
            if (pid > 0) ::kill(pid_t(pid), SIGINT);
        } else if (kind == QLatin1String("quit")) {
            m_session->terminate();
            QCoreApplication::quit();
        }
    }

    TerminalSession *m_session;
    ViewportFrame m_frame;
    QTimer *m_frames = nullptr;
    QTimer *m_status = nullptr;
    qint64 m_lastForeground = -2;
    bool m_lastRunning = false;
    QSocketNotifier *m_stdin = nullptr;
    QByteArray m_pending;
    int m_rows;
    int m_cols;
    bool m_alt = false;
    bool m_forceSnapshot = true;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int rows = kDefaultRows, cols = kDefaultCols, scrollback = 2000;
    QString core, program, directory;
    bool rawOut = false;
    const QStringList arguments = QCoreApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &argument = arguments[i];
        const auto next = [&]() { return i + 1 < arguments.size() ? arguments[++i] : QString(); };
        if (argument == QLatin1String("--rows")) rows = next().toInt();
        else if (argument == QLatin1String("--cols")) cols = next().toInt();
        else if (argument == QLatin1String("--core")) core = next();
        else if (argument == QLatin1String("--shell")) program = next();
        else if (argument == QLatin1String("--cwd")) directory = next();
        else if (argument == QLatin1String("--scrollback")) scrollback = next().toInt();
        else if (argument == QLatin1String("--raw-out")) rawOut = true;
    }
    if (rows <= 0) rows = kDefaultRows;
    if (cols <= 0) cols = kDefaultCols;

    TerminalSession session(core);
    session.setScrollbackLines(scrollback);
    session.resize(rows, cols, 8, 16);

    TerminalSession::StartOptions options;
    options.program = program.isEmpty() ? qEnvironmentVariable("SHELL", QStringLiteral("/bin/bash"))
                                        : program;
    options.workingDirectory = directory;
    if (!session.start(options)) {
        writeLine({{"t", "error"}, {"message", session.errorString()}});
        return 1;
    }

    Bridge bridge(&session, rows, cols, rawOut);
    bridge.sendHello();
    return app.exec();
}
