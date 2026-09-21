// SPDX-License-Identifier: AGPL-3.0-or-later
// Native Windows 10 1809+ pseudoconsole. Keep both pipes draining independently:
// ClosePseudoConsole may emit a final frame and block until the reader consumes it.
// https://learn.microsoft.com/windows/console/creating-a-pseudoconsole-session
#include "Pty.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <QDir>
#include <QProcessEnvironment>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace relay {
namespace {
void closeHandle(HANDLE &handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = nullptr;
}

// Windows argv quoting: double backslashes preceding a quote or the closing quote.
QString quoteArg(const QString &arg)
{
    QString result = QStringLiteral("\"");
    int slashes = 0;
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('\\')) { ++slashes; continue; }
        if (ch == QLatin1Char('"')) {
            result += QString(slashes * 2 + 1, QLatin1Char('\\'));
        } else {
            result += QString(slashes, QLatin1Char('\\'));
        }
        slashes = 0;
        result += ch;
    }
    result += QString(slashes * 2, QLatin1Char('\\'));
    return result + QLatin1Char('"');
}

class ConPty final : public Pty {
public:
    ~ConPty() override { terminate(); }

    bool start(const StartOptions &options) override
    {
        terminate();
        std::lock_guard<std::mutex> lifecycle(m_lifecycle);
        m_error.clear();
        HANDLE inRead = nullptr, outWrite = nullptr;
        auto fail = [&](const QString &operation, DWORD error) {
            m_error = QStringLiteral("%1 failed (Windows error %2)").arg(operation).arg(error);
            closeHandle(inRead);
            closeHandle(outWrite);
            if (m_process) { TerminateProcess(m_process, 1); WaitForSingleObject(m_process, INFINITE); }
            if (m_console) { ClosePseudoConsole(m_console); m_console = nullptr; }
            closeHandle(m_process);
            closeHandle(m_job);
            closeHandle(m_input);
            closeHandle(m_output);
            return false;
        };
        if (!CreatePipe(&inRead, &m_input, nullptr, 0) ||
            !CreatePipe(&m_output, &outWrite, nullptr, 0))
            return fail(QStringLiteral("CreatePipe"), GetLastError());
        const COORD size{SHORT(std::clamp(options.cols, 1, 32767)),
                         SHORT(std::clamp(options.rows, 1, 32767))};
        const HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &m_console);
        if (FAILED(hr)) return fail(QStringLiteral("CreatePseudoConsole"), DWORD(hr));

        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto *attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes))
            return fail(QStringLiteral("InitializeProcThreadAttributeList"), GetLastError());
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       m_console, sizeof(m_console), nullptr, nullptr)) {
            const DWORD error = GetLastError();
            DeleteProcThreadAttributeList(attributes);
            return fail(QStringLiteral("UpdateProcThreadAttribute"), error);
        }
        QString program = options.program;
        if (program.isEmpty()) program = qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
        QString command = quoteArg(QDir::toNativeSeparators(program));
        for (const auto &arg : options.arguments) command += QLatin1Char(' ') + quoteArg(arg);
        std::wstring commandLine = command.toStdWString();
        const std::wstring directory = QDir::toNativeSeparators(options.workingDirectory).toStdWString();
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const auto &name : options.unsetEnvironment) env.remove(name);
        for (const auto &entry : options.environment) {
            const int equal = entry.indexOf(QLatin1Char('='));
            if (equal > 0) env.insert(entry.left(equal), entry.mid(equal + 1));
        }
        QStringList entries = env.toStringList();
        std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        std::wstring environment;
        for (const auto &entry : entries) { environment += entry.toStdWString(); environment += L'\0'; }
        environment += L'\0';
        if (entries.isEmpty()) environment += L'\0';
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            environment.data(), directory.empty() ? nullptr : directory.c_str(),
            &startup.StartupInfo, &process);
        const DWORD error = GetLastError();
        DeleteProcThreadAttributeList(attributes);
        closeHandle(inRead);
        closeHandle(outWrite);
        if (!created) return fail(QStringLiteral("CreateProcess"), error);
        m_process = process.hProcess;
        m_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!m_job || !SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(m_job, m_process)) {
            const DWORD jobError = GetLastError();
            closeHandle(process.hThread);
            return fail(QStringLiteral("Create/assign process job"), jobError);
        }
        m_pid = process.dwProcessId;
        m_stopping = false;
        m_running = true;
        m_reader = std::thread([this] {
            char buffer[65536];
            DWORD read = 0;
            while (ReadFile(m_output, buffer, sizeof(buffer), &read, nullptr) && read) {
                if (onOutput) onOutput(buffer, size_t(read));
            }
            WaitForSingleObject(m_process, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(m_process, &code);
            m_running = false;
            if (onFinished) onFinished(int(code));
        });
        m_writer = std::thread([this] {
            for (;;) {
                QByteArray next;
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    m_ready.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
                    if (m_stopping) return;
                    next = std::move(m_queue.front());
                    m_queue.pop_front();
                }
                DWORD offset = 0;
                while (offset < DWORD(next.size()) && !m_stopping) {
                    DWORD written = 0;
                    if (!WriteFile(m_input, next.constData() + offset, DWORD(next.size()) - offset, &written, nullptr) || !written)
                        return;
                    offset += written;
                }
            }
        });
        m_watcher = std::thread([this] {
            WaitForSingleObject(m_process, INFINITE);
            m_stopping = true;
            m_ready.notify_all();
            CancelSynchronousIo(m_writer.native_handle());
            // The reader remains live throughout shutdown, including its final frame.
            std::lock_guard<std::mutex> lock(m_consoleMutex);
            if (m_console) { ClosePseudoConsole(m_console); m_console = nullptr; }
        });
        if (ResumeThread(process.hThread) == DWORD(-1)) TerminateJobObject(m_job, 1);
        closeHandle(process.hThread);
        return true;
    }

    QString errorString() const override { return m_error; }
    void write(const char *data, size_t len) override
    {
        if (!len || !m_running || m_stopping) return;
        std::lock_guard<std::mutex> lock(m_queueMutex);
        // QByteArray takes an int on Qt5; split very large writes at that boundary.
        while (len) {
            const size_t chunk = std::min(len, size_t(1024 * 1024));
            m_queue.emplace_back(data, int(chunk));
            data += chunk; len -= chunk;
        }
        m_ready.notify_one();
    }
    void resize(int rows, int cols, int, int) override
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        if (m_console) ResizePseudoConsole(m_console,
            {SHORT(std::clamp(cols, 1, 32767)), SHORT(std::clamp(rows, 1, 32767))});
    }
    qint64 childPid() const override { return m_pid; }
    qint64 foregroundPid() const override { return -1; }
    TermiosFlags termiosFlags() const override { return {}; }
    bool isRunning() const override { return m_running; }
    void terminate() override
    {
        std::lock_guard<std::mutex> lifecycle(m_lifecycle);
        m_stopping = true;
        if (m_job) TerminateJobObject(m_job, 1);
        m_ready.notify_all();
        // Closing the pseudoconsole also closes the far end of a blocked writer's pipe.
        if (m_watcher.joinable()) m_watcher.join();
        if (m_writer.joinable()) { CancelSynchronousIo(m_writer.native_handle()); m_writer.join(); }
        if (m_reader.joinable()) m_reader.join();
        closeHandle(m_process); closeHandle(m_job); closeHandle(m_input); closeHandle(m_output);
        m_running = false; m_pid = -1;
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.clear();
    }
private:
    QString m_error;
    HANDLE m_process = nullptr, m_job = nullptr, m_input = nullptr, m_output = nullptr;
    HPCON m_console = nullptr;
    std::atomic<qint64> m_pid{-1};
    std::atomic<bool> m_running{false}, m_stopping{true};
    std::mutex m_lifecycle, m_consoleMutex, m_queueMutex;
    std::condition_variable m_ready;
    std::deque<QByteArray> m_queue;
    std::thread m_reader, m_writer, m_watcher;
};
}
std::unique_ptr<Pty> Pty::create() { return std::make_unique<ConPty>(); }
} // namespace relay
