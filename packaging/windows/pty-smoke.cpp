// SPDX-License-Identifier: AGPL-3.0-or-later
#include "../../engine/pty/Pty.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QThread>
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
    int cursorRequests = 0;
    bool finished = false;
    int exitCode = -1;
    pty->onOutput = [&](const char *data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        output.append(data, int(size));
        // PSReadLine asks for the cursor position. A real terminal engine answers this;
        // this transport-only harness uses a fixed screen location.
        const int requests = output.count("\x1b[6n");
        if (requests > cursorRequests) {
            cursorRequests = requests;
            pty->write("\x1b[1;1R");
        }
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
    finished = false; output.clear(); cursorRequests = 0;
    options.program = QStringLiteral("cmd.exe"); options.arguments = {QStringLiteral("/Q")};
    if (!pty->start(options)) return 4;
    pty->write("echo interactive-input-ok\r\n");
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!ready.wait_for(lock, std::chrono::seconds(20), [&] { return output.contains("interactive-input-ok"); })) return 5;
    }
    pty->terminate();
    if (pty->isRunning() || !finished) return 6;
    // The actual composer handshake must load multiline UTF8 without executing it until
    // Relay receives the byte hash and sends Enter.
    finished = false; output.clear(); cursorRequests = 0;
    options.program = qEnvironmentVariable("RELAY_POWERSHELL", "pwsh.exe");
    options.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
        QStringLiteral("-NoExit"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), QStringLiteral(RELAY_TEST_SOURCE_DIR "/shell/integration.ps1")};
    options.environment = {QStringLiteral("RELAY_RUNTIME_DIR=") + directory.path(),
        QStringLiteral("RELAY_START_DIR=") + directory.path(),
        QStringLiteral("RELAY_SESSION_TOKEN=smoke-token"), QStringLiteral("RELAY_CLEAN_SHELL=1")};
    if (!pty->start(options)) { std::cerr << pty->errorString().toStdString(); return 8; }
    auto waitState = [&](const QString &stage, const QString &hash = QString()) {
        QElapsedTimer timer; timer.start();
        QByteArray lastState;
        while (timer.elapsed() < 20000) {
            QFile stateFile(directory.filePath("state.json"));
            if (stateFile.open(QIODevice::ReadOnly)) {
                lastState = stateFile.readAll();
                stateFile.close(); // Do not deny a Windows atomic replacement during the sleep.
                auto state = QJsonDocument::fromJson(lastState).object();
                if (state.value("token").toString() == "smoke-token"
                    && state.value("event").toString() == stage
                    && (hash.isEmpty() || state.value("input_sha256").toString() == hash)) return state;
            }
            QThread::msleep(25);
        }
        std::lock_guard<std::mutex> lock(mutex);
        std::cerr << "State timeout " << stage.toStdString() << " expected_hash=" << hash.toStdString()
                  << " state=" << lastState.toStdString() << " output=" << output.toStdString();
        return QJsonObject();
    };
    if (waitState("ready").isEmpty()) return 9;
    const QByteArray command = QString::fromUtf8(
        "[IO.File]::WriteAllText('composer-result.txt', 'héllo 世界')\n"
        "Write-Output 'composer-handshake-ok'\n& $env:ComSpec /c exit 7").toUtf8();
    QFile input(directory.filePath("input.txt"));
    if (!input.open(QIODevice::WriteOnly) || input.write(command) != command.size()) return 10;
    input.close();
    pty->write(QByteArray("\x18\x12", 2));
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(command, QCryptographicHash::Sha256).toHex());
    if (waitState("loaded", hash).isEmpty()) return 11;
    if (QFile::exists(directory.filePath("composer-result.txt"))) return 12;
    pty->write("\r");
    const auto completed = waitState("ready");
    if (completed.isEmpty() || completed.value("status").toInt() != 7) return 13;
    QFile result(directory.filePath("composer-result.txt"));
    if (!result.open(QIODevice::ReadOnly) || result.readAll() != QString::fromUtf8("héllo 世界").toUtf8()) return 14;
    // Redraw preserves the prompt and does not execute the previous input a second time.
    pty->write(QByteArray("\x18\x10", 2));
    pty->terminate();
    options.program = QStringLiteral("C:/relay-does-not-exist.exe");
    if (pty->start(options) || pty->errorString().isEmpty()) return 7;
    std::cout << "PASS native ConPTY: output, environment, exit, resize, input, reuse, stop, failed start, PowerShell composer handshake\n";
    return 0;
}
