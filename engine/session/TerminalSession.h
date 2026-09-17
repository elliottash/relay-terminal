// SPDX-License-Identifier: GPL-3.0-or-later
// relay::TerminalSession: one terminal = one VtCore + one Pty, thread-safe.
//
// PTY output is parsed on the Pty's I/O thread (not the GUI thread) under the
// session mutex. Core events are queued and delivered as Qt signals on the
// session's thread (the GUI thread), coalesced to at most one delivery per
// event-loop pass. The view never parses; it takes a ViewportFrame snapshot
// under the lock (microseconds) and paints without it.
#pragma once

#include "core/VtCore.h"
#include "pty/Pty.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <vector>

namespace relay {

class TerminalSession : public QObject {
    Q_OBJECT
public:
    // coreName: "ghostty", "libvterm" or empty for the default core.
    explicit TerminalSession(const QString &coreName = QString(), QObject *parent = nullptr);
    ~TerminalSession() override;

    QString coreName() const;

    struct StartOptions {
        QString program;
        QStringList arguments;
        QString workingDirectory;
        QStringList environment; // KEY=VALUE, added to TERM/COLORTERM defaults
    };
    bool start(const StartOptions &options);
    QString errorString() const;
    bool isRunning() const;
    qint64 shellPid() const;
    qint64 foregroundPid() const;
    void terminate();

    // Geometry. Also resizes the PTY (SIGWINCH).
    void resize(int rows, int cols, int cellWidthPx, int cellHeightPx);
    int rows() const;
    int columns() const;

    // Raw bytes to the program (keys already encoded, or text).
    void sendInput(const QByteArray &bytes);
    // Bytes into the emulator as if the program printed them (never reaches
    // the program). Relay's inline agent output uses this. If the program's
    // output stopped in the middle of an escape sequence or UTF-8 character,
    // the bytes wait until the parser is back at ground (at most ~500 ms).
    void writeToDisplay(const QByteArray &bytes);

    // Run `f(core)` with the session lock held. Events raised inside are
    // delivered as signals afterwards. Do not call other session methods from f.
    template<typename F>
    auto withCore(F &&f) -> decltype(f(std::declval<VtCore &>()))
    {
        GuiLock lock(this);
        struct Deliver {
            TerminalSession *s;
            ~Deliver() { s->scheduleDelivery(); }
        } deliver{this};
        return f(*m_core);
    }

    // Convenience (each takes the lock once).
    QString screenText();
    QStringList scrollbackText(int maxLines);
    bool altScreen() const { return m_alt.load(); }
    QString title() const;
    QString currentDirectory() const; // last OSC 7 path (empty if none)
    quint64 bytesReceived() const { return m_bytes.load(); }

    void setScrollbackLines(int lines);
    void setClipboardWriteAllowed(bool allowed);
    // Emit output() with raw PTY bytes (GUI thread, batched). Off by default:
    // copying a 200 MB flood to the GUI thread is not free. If more than 64 MiB
    // accumulate before the GUI thread takes them, the excess is dropped.
    void setOutputSignalEnabled(bool enabled);

signals:
    void contentChanged();
    void titleChanged(const QString &title);
    void cwdChanged(const QString &path, const QString &host);
    void bell();
    void altScreenChanged(bool active);
    void promptMark(relay::PromptMark kind, int row, int exitCode);
    void clipboardWriteRequested(const QString &target, const QByteArray &data);
    void notification(const QString &title, const QString &body);
    void output(const QByteArray &bytes);
    void finished(int exitCode);

private:
    struct GuiLock {
        explicit GuiLock(const TerminalSession *s);
        ~GuiLock();
        const TerminalSession *s;
    };
    struct Event {
        enum Kind { Title, Cwd, Bell, Alt, Mark, Clipboard, Notify, Finished } kind;
        QString a;
        QString b;
        QByteArray bytes;
        int i = 0;
        int j = 0;
    };

    void onPtyOutput(const char *data, size_t len);
    void scheduleDelivery();
    void deliver();

    std::unique_ptr<VtCore> m_core;
    std::unique_ptr<Pty> m_pty;
    mutable std::mutex m_mutex;
    mutable std::atomic<int> m_guiWaiting{0};
    std::atomic<bool> m_deliveryQueued{false};
    std::atomic<bool> m_outputSignal{false};
    std::atomic<bool> m_alt{false};
    std::atomic<quint64> m_bytes{0};
    std::atomic<bool> m_contentDirty{false};
    void pushEvent(Event e);      // m_mutex held
    void flushDisplayQueue(bool force); // m_mutex held

    std::vector<Event> m_events;  // guarded by m_mutex; coalesced and capped
    bool m_bellPending = false;   // guarded by m_mutex
    int m_titleEvent = -1;        // index of the pending title event, guarded by m_mutex
    int m_cwdEvent = -1;          // index of the pending cwd event, guarded by m_mutex
    QByteArray m_pendingOutput;   // guarded by m_mutex; capped at 64 MiB (excess output() data is dropped)
    QByteArray m_pendingDisplay;  // writeToDisplay bytes waiting for the parser to reach ground, guarded by m_mutex
    QElapsedTimer m_pendingDisplaySince; // guarded by m_mutex
    QTimer m_displayRetry;
    QString m_title;              // guarded by m_mutex
    QString m_cwd;                // guarded by m_mutex
    QString m_error;
    int m_rows = 24;
    int m_cols = 80;
};

} // namespace relay

Q_DECLARE_METATYPE(relay::PromptMark)
