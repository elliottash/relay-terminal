// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../../engine/pty/Pty.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto pty = relay::Pty::create();
    std::mutex mutex;
    std::condition_variable ready;
    QByteArray output;
    bool finished = false;
    int exitCode = -1;
    pty->onOutput = [&](const char *data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        output.append(data, int(size));
        ready.notify_all();
    };
    pty->onFinished = [&](int code) {
        std::lock_guard<std::mutex> lock(mutex);
        exitCode = code; finished = true; ready.notify_all();
    };
    QTemporaryDir directory;
    relay::Pty::StartOptions options;
    options.program = QStringLiteral("powershell.exe");
    options.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
        QStringLiteral("Write-Output $env:RELAY_PTY_TEST; Write-Output (Get-Location).Path; exit 7")};
    options.environment = {QStringLiteral("RELAY_PTY_TEST=native-conpty-ok")};
    options.workingDirectory = directory.path();
    if (!pty->start(options)) { std::cerr << pty->errorString().toStdString(); return 1; }
    pty->resize(40, 120);
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!ready.wait_for(lock, std::chrono::seconds(20), [&] { return finished; })) return 2;
        if (exitCode != 7 || !output.contains("native-conpty-ok")) {
            std::cerr << "exit=" << exitCode << " output=" << output.toStdString(); return 3;
        }
    }
    pty->terminate();
    // Reuse, interactive input, and termination with output still being produced.
    finished = false; output.clear();
    options.program = QStringLiteral("cmd.exe"); options.arguments = {QStringLiteral("/Q")};
    if (!pty->start(options)) return 4;
    pty->write("echo interactive-input-ok\r\n");
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!ready.wait_for(lock, std::chrono::seconds(20), [&] { return output.contains("interactive-input-ok"); })) return 5;
    }
    pty->terminate();
    if (pty->isRunning() || !finished) return 6;
    options.program = QStringLiteral("C:/relay-does-not-exist.exe");
    if (pty->start(options) || pty->errorString().isEmpty()) return 7;
    std::cout << "PASS native ConPTY: output, environment, exit, resize, input, reuse, stop, failed start\n";
    return 0;
}
