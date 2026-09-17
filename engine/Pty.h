// SPDX-License-Identifier: GPL-3.0-or-later
// relay::Pty: minimal pseudo-terminal abstraction for the engine spike.
// Unix implementation (forkpty) lives in PtyUnix.cpp; a ConPTY implementation
// (CreatePseudoConsole + pipes + a reader thread) would implement the same
// interface on Windows.
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
        QString program;              // empty = $SHELL or /bin/bash
        QStringList arguments;
        QString workingDirectory;     // empty = inherit
        QStringList extraEnvironment; // "KEY=VALUE"
        int rows = 24;
        int cols = 80;
    };

    virtual ~Pty() = default;

    virtual bool start(const StartOptions &options) = 0;
    virtual qint64 write(const char *data, qint64 len) = 0;
    virtual void resize(int rows, int cols) = 0;
    // Process id of the child we spawned (the shell).
    virtual qint64 childPid() const = 0;
    // Process-group leader currently owning the terminal (tcgetpgrp); -1 if unknown.
    virtual qint64 foregroundPid() const = 0;
    virtual bool isRunning() const = 0;

    // Called on the GUI thread with raw output bytes.
    std::function<void(const char *data, qint64 len)> onOutput;
    // Called once when the child side closes.
    std::function<void(int exitCode)> onFinished;

    static std::unique_ptr<Pty> create(); // platform factory
};

} // namespace relay
