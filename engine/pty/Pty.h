// SPDX-License-Identifier: GPL-3.0-or-later
// relay::Pty: pseudo-terminal with its own I/O thread.
//
// Unix: forkpty + a reader/writer thread (PtyUnix.cpp).
// Windows: ConPTY (PtyWin.cpp, not implemented yet; see the TODO there).
//
// Threading contract:
// - onOutput and onFinished are called on the Pty's I/O thread, never on the
//   GUI thread. Set them before start().
// - write(), resize(), foregroundPid() and terminate() are thread-safe.
// - write() never blocks: bytes are queued and flushed by the I/O thread
//   (a full kernel buffer cannot freeze the caller).
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace relay {

class Pty {
public:
    struct StartOptions {
        QString program;              // empty = $SHELL, then /bin/sh
        QStringList arguments;        // argv[1..]
        QString workingDirectory;     // empty = inherit
        QStringList environment;      // "KEY=VALUE" entries added to / overriding the inherited environment
        QStringList unsetEnvironment; // names removed from the inherited environment
        int rows = 24;
        int cols = 80;
        int pixelWidth = 0;
        int pixelHeight = 0;
    };

    virtual ~Pty() = default;

    virtual bool start(const StartOptions &options) = 0;
    virtual QString errorString() const = 0;
    virtual void write(const char *data, size_t len) = 0;
    void write(const QByteArray &bytes) { write(bytes.constData(), size_t(bytes.size())); }
    virtual void resize(int rows, int cols, int pixelWidth = 0, int pixelHeight = 0) = 0;
    // Process id of the spawned child (usually the shell), -1 if none.
    virtual qint64 childPid() const = 0;
    // Process group owning the terminal (tcgetpgrp on the master), -1 if unknown.
    virtual qint64 foregroundPid() const = 0;
    virtual bool isRunning() const = 0;
    // Hang up: SIGHUP to the child's session, as closing a terminal window does.
    virtual void terminate() = 0;

    std::function<void(const char *data, size_t len)> onOutput; // I/O thread
    std::function<void(int exitCode)> onFinished;               // I/O thread, once; 128+N for signal N

    static std::unique_ptr<Pty> create(); // platform implementation
};

} // namespace relay
