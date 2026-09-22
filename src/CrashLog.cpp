// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CrashLog.h"

#include "Logging.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstring>

namespace relay::crashlog {
namespace {
wchar_t g_path[32768]{};
char g_build[256]{};
volatile LONG g_reporting = 0;
volatile LONG g_toFile = 0;

LONG WINAPI exceptionHandler(EXCEPTION_POINTERS *exception) {
    if (InterlockedCompareExchange(&g_reporting, 1, 0)) return EXCEPTION_CONTINUE_SEARCH;
    // Prepare all strings at install time. At a fatal exception only fixed buffers and
    // Kernel32 I/O are used: neither Qt nor heap allocation is safe here.
    char report[512]{};
    size_t used = 0;
    const auto append = [&](const char *text) {
        while (*text && used + 1 < sizeof(report)) report[used++] = *text++;
    };
    const auto hex = [&](ULONG_PTR value) {
        constexpr char digits[] = "0123456789abcdef";
        for (int shift = int(sizeof(value) * 8) - 4; shift >= 0; shift -= 4)
            if (used + 1 < sizeof(report)) report[used++] = digits[(value >> shift) & 15];
    };
    append("ERROR relay.gui gui_crash exception=0x");
    hex(exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionCode : 0);
    append(" address=0x");
    hex(exception && exception->ExceptionRecord
            ? reinterpret_cast<ULONG_PTR>(exception->ExceptionRecord->ExceptionAddress) : 0);
    append(" build="); append(g_build); append("\r\n");
    HANDLE file = INVALID_HANDLE_VALUE;
    if (InterlockedCompareExchange(&g_toFile, 0, 0) && g_path[0])
        file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE outputs[] = {file, GetStdHandle(STD_ERROR_HANDLE)};
    for (HANDLE output : outputs) {
        if (!output || output == INVALID_HANDLE_VALUE) continue;
        DWORD written = 0;
        WriteFile(output, report, DWORD(used), &written, nullptr);
        if (output == file) FlushFileBuffers(output);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    // Preserve Windows Error Reporting and the original exception's termination status.
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void install(const QString &buildId) {
    const QString path = relay::log::filePath();
    if (path.size() < int(sizeof(g_path) / sizeof(g_path[0])) - 1) {
        path.toWCharArray(g_path);
        g_path[path.size()] = 0;
    }
    const QByteArray build = buildId.toUtf8().left(sizeof(g_build) - 1);
    std::memcpy(g_build, build.constData(), size_t(build.size()));
    g_build[build.size()] = 0;
    noteLogEnabled(relay::log::level() != relay::log::Level::Off);
    SetUnhandledExceptionFilter(exceptionHandler);
}
void noteLogEnabled(bool enabled) { InterlockedExchange(&g_toFile, enabled ? 1 : 0); }
QString reportPath() {
    return InterlockedCompareExchange(&g_toFile, 0, 0) ? QString::fromWCharArray(g_path) : QString();
}
} // namespace relay::crashlog
#else
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace relay::crashlog {
namespace {

// Everything the handler is allowed to touch, filled in by install() while it is still safe to
// allocate. `volatile sig_atomic_t` for what the handler writes; plain bytes for what it reads.
constexpr int PathMax = 4096;
constexpr int NoteMax = 256;
constexpr int MaxFrames = 64;

char g_path[PathMax] = {0};        // relay.log, or empty: then stderr alone
char g_note[NoteMax] = {0};        // "build=… version=…", already formatted
time_t g_started = 0;              // to say how long the run lasted
volatile sig_atomic_t g_reporting = 0;   // a crash inside the handler must not loop
volatile sig_atomic_t g_toFile = 0;      // logging is on: write the report into relay.log too
void *g_frames[MaxFrames];         // the backtrace lands here: no allocation at crash time
char g_stack[256 * 1024];          // our own stack, so a stack overflow can still be reported

// write() can return short or be interrupted; a report that gives up on EINTR is a report missing
// its last line.
void put(int fd, const char *text, size_t length) {
    while (length > 0) {
        const ssize_t written = ::write(fd, text, length);
        if (written > 0) { text += written; length -= size_t(written); continue; }
        if (written < 0 && errno == EINTR) continue;
        return;
    }
}

// Integers, right into a caller's buffer. snprintf is not async-signal-safe; this is arithmetic.
// Returns the number of characters written.
int putNumber(char *out, long long value, int width = 0, bool hex = false) {
    char digits[32];
    int count = 0;
    unsigned long long magnitude = value < 0 ? ~static_cast<unsigned long long>(value) + 1
                                             : static_cast<unsigned long long>(value);
    const unsigned long long base = hex ? 16 : 10;
    do {
        const int digit = int(magnitude % base);
        digits[count++] = char(digit < 10 ? '0' + digit : 'a' + digit - 10);
        magnitude /= base;
    } while (magnitude > 0 && count < int(sizeof digits));
    int length = 0;
    if (value < 0) out[length++] = '-';
    for (int pad = count; pad < width; ++pad) out[length++] = '0';
    while (count > 0) out[length++] = digits[--count];
    return length;
}

// The log's own timestamp shape (2026-09-19T20:36:17.000Z) without gmtime_r, which may call
// tzset() and is not async-signal-safe. Days since the epoch to a civil date, by arithmetic.
int putTimestamp(char *out) {
    struct timespec now;
    if (::clock_gettime(CLOCK_REALTIME, &now) != 0) { now.tv_sec = ::time(nullptr); now.tv_nsec = 0; }
    long long days = now.tv_sec / 86400;
    long long seconds = now.tv_sec % 86400;
    if (seconds < 0) { seconds += 86400; --days; }
    // Howard Hinnant's civil_from_days, era-based and exact for any day.
    days += 719468;
    const long long era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned long long dayOfEra = static_cast<unsigned long long>(days - era * 146097);
    const unsigned long long yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    long long year = static_cast<long long>(yearOfEra) + era * 400;
    const unsigned long long dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned long long shifted = (5 * dayOfYear + 2) / 153;
    const unsigned long long day = dayOfYear - (153 * shifted + 2) / 5 + 1;
    const unsigned long long month = shifted < 10 ? shifted + 3 : shifted - 9;
    if (month <= 2) ++year;
    int length = 0;
    length += putNumber(out + length, year, 4);
    out[length++] = '-';
    length += putNumber(out + length, static_cast<long long>(month), 2);
    out[length++] = '-';
    length += putNumber(out + length, static_cast<long long>(day), 2);
    out[length++] = 'T';
    length += putNumber(out + length, seconds / 3600, 2);
    out[length++] = ':';
    length += putNumber(out + length, (seconds / 60) % 60, 2);
    out[length++] = ':';
    length += putNumber(out + length, seconds % 60, 2);
    out[length++] = '.';
    length += putNumber(out + length, now.tv_nsec / 1000000, 3);
    out[length++] = 'Z';
    return length;
}

const char *signalName(int number) {
    switch (number) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
    case SIGSYS: return "SIGSYS";
    default: return "signal";
    }
}

// One line in the log's format: "<ts> ERROR relay.gui <message>". The frames that follow are
// written by backtrace_symbols_fd in its own shape, between two marker lines, because anything
// that reformatted them would have to allocate.
void line(int fd, const char *message) {
    char buffer[512];
    int length = putTimestamp(buffer);
    const char *prefix = " ERROR relay.gui ";
    ::memcpy(buffer + length, prefix, ::strlen(prefix));
    length += int(::strlen(prefix));
    const int room = int(sizeof buffer) - length - 2;
    int used = int(::strlen(message));
    if (used > room) used = room;
    ::memcpy(buffer + length, message, size_t(used));
    length += used;
    buffer[length++] = '\n';
    put(fd, buffer, size_t(length));
}

void handler(int number, siginfo_t *info, void *) {
    // A second crash — in this handler, or on another thread while it runs — must not reopen the
    // file and interleave half a report. The first one through writes; the rest die quietly.
    if (__sync_val_compare_and_swap(&g_reporting, 0, 1) != 0) {
        ::signal(number, SIG_DFL);
        ::raise(number);
        return;
    }

    int fd = -1;
    if (g_toFile && g_path[0]) fd = ::open(g_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    const int fds[2] = {fd, STDERR_FILENO};

    char message[384];
    int length = 0;
    const char *head = "gui_crash signal=";
    ::memcpy(message + length, head, ::strlen(head)); length += int(::strlen(head));
    length += putNumber(message + length, number);
    message[length++] = ' ';
    const char *name = signalName(number);
    ::memcpy(message + length, "name=", 5); length += 5;
    ::memcpy(message + length, name, ::strlen(name)); length += int(::strlen(name));
    const char *codeKey = " code=";
    ::memcpy(message + length, codeKey, ::strlen(codeKey)); length += int(::strlen(codeKey));
    length += putNumber(message + length, info ? info->si_code : 0);
    const char *addrKey = " addr=0x";
    ::memcpy(message + length, addrKey, ::strlen(addrKey)); length += int(::strlen(addrKey));
    length += putNumber(message + length,
                        static_cast<long long>(info ? reinterpret_cast<intptr_t>(info->si_addr) : 0), 0, true);
    const char *pidKey = " pid=";
    ::memcpy(message + length, pidKey, ::strlen(pidKey)); length += int(::strlen(pidKey));
    length += putNumber(message + length, static_cast<long long>(::getpid()));
    const char *upKey = " uptime_s=";
    ::memcpy(message + length, upKey, ::strlen(upKey)); length += int(::strlen(upKey));
    length += putNumber(message + length,
                        g_started ? static_cast<long long>(::time(nullptr) - g_started) : -1);
    if (g_note[0]) {
        message[length++] = ' ';
        const size_t note = ::strlen(g_note);
        ::memcpy(message + length, g_note, note); length += int(note);
    }
    message[length] = '\0';

    const int count = ::backtrace(g_frames, MaxFrames);
    char frames[64];
    int framesLength = 0;
    const char *framesKey = "gui_crash_frames_begin count=";
    ::memcpy(frames + framesLength, framesKey, ::strlen(framesKey)); framesLength += int(::strlen(framesKey));
    framesLength += putNumber(frames + framesLength, count);
    frames[framesLength] = '\0';
    for (const int out : fds) {
        if (out < 0) continue;
        line(out, message);
        line(out, frames);
        // The frames themselves: one line each, "<binary>(<symbol>+0x<offset>) [0x<address>]".
        // Unresolved names are still addresses `addr2line -e build/relay` can turn into lines.
        ::backtrace_symbols_fd(g_frames, count, out);
        line(out, "gui_crash_frames_end · names: addr2line -fCe build/relay <+0x…> · every thread: scripts/relay-debug");
        ::fsync(out);
    }
    if (fd >= 0) ::close(fd);

    // Die of what killed us: a debugger, apport and the exit status all still see the real signal.
    ::signal(number, SIG_DFL);
    ::raise(number);
}

} // namespace

void install(const QString &buildId) {
    const QByteArray path = relay::log::filePath().toUtf8();
    if (!path.isEmpty() && path.size() < PathMax - 1) {
        ::memcpy(g_path, path.constData(), size_t(path.size()));
        g_path[path.size()] = '\0';
    }
    const QByteArray note = QStringLiteral("build=%1").arg(buildId.isEmpty() ? QStringLiteral("unknown") : buildId).toUtf8();
    if (note.size() < NoteMax - 1) {
        ::memcpy(g_note, note.constData(), size_t(note.size()));
        g_note[note.size()] = '\0';
    }
    g_started = ::time(nullptr);
    g_toFile = relay::log::level() == relay::log::Level::Off ? 0 : 1;

    // backtrace() dlopens libgcc's unwinder the first time it is called, which a signal handler
    // must not do. Call it once now, where that is allowed, so the crash-time call is arithmetic.
    void *warm[4];
    (void)::backtrace(warm, 4);

    // Our own signal stack: a stack overflow (the classic runaway recursion) leaves no room to run
    // a handler on the thread's own stack, and the report would be the crash.
    stack_t alternate;
    ::memset(&alternate, 0, sizeof alternate);
    alternate.ss_sp = g_stack;
    alternate.ss_size = sizeof g_stack;
    alternate.ss_flags = 0;
    ::sigaltstack(&alternate, nullptr);

    struct sigaction action;
    ::memset(&action, 0, sizeof action);
    action.sa_sigaction = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (const int number : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGSYS}) {
        struct sigaction current;
        // Started with one ignored (a launcher, a test harness): leave it ignored, as the quit
        // signals do — taking it over would turn a deliberate choice into a crash report.
        if (::sigaction(number, nullptr, &current) == 0 && current.sa_handler == SIG_IGN) continue;
        ::sigaction(number, &action, nullptr);
    }
}

void noteLogEnabled(bool enabled) { g_toFile = enabled ? 1 : 0; }

QString reportPath() { return g_toFile ? QString::fromUtf8(g_path) : QString(); }

} // namespace relay::crashlog

#endif
