// SPDX-License-Identifier: AGPL-3.0-or-later
// Unix relay::Pty: forkpty + one I/O thread (poll on the master and a wake pipe).
#include "Pty.h"

#include <QFileInfo>
#include <QStandardPaths>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <poll.h>
#include <pty.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif

extern char **environ;

namespace relay {
namespace {

int exitCodeFromStatus(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

void reapLater(pid_t pid)
{
    // Give the child a moment to handle SIGHUP, then make sure it goes away,
    // without blocking the caller (usually the GUI thread closing a pane).
    std::thread([pid] {
        for (int i = 0; i < 30; ++i) {
            if (::waitpid(pid, nullptr, WNOHANG) != 0)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
    }).detach();
}

class UnixPty final : public Pty {
public:
    ~UnixPty() override
    {
        stopThread();
        if (m_master >= 0)
            ::close(m_master);
        for (int fd : m_wake) {
            if (fd >= 0)
                ::close(fd);
        }
        if (m_pid > 0 && !m_reaped.load()) {
            ::kill(m_pid, SIGHUP);
            reapLater(m_pid);
        }
    }

    bool start(const StartOptions &o) override
    {
        if (m_pid > 0) {
            m_error = QStringLiteral("already started");
            return false;
        }
        QString program = o.program;
        if (program.isEmpty())
            program = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
        const QString path = program.contains(QLatin1Char('/')) ? program : QStandardPaths::findExecutable(program);
        if (path.isEmpty() || !QFileInfo(path).isExecutable()) {
            m_error = QStringLiteral("program not found or not executable: %1").arg(program);
            return false;
        }

        // Everything the child needs is prepared before fork(): after fork()
        // only async-signal-safe calls are allowed (the parent has threads).
        std::vector<QByteArray> argStore;
        argStore.push_back(program.toLocal8Bit());
        for (const QString &a : o.arguments)
            argStore.push_back(a.toLocal8Bit());
        std::vector<char *> argv;
        for (QByteArray &a : argStore)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        // Inherited environment, minus unset names and names given in
        // o.environment; within o.environment the last entry for a name wins
        // (so a caller's TERM=... overrides a default earlier in the list).
        QStringList overrideNames = o.unsetEnvironment;
        for (const QString &kv : o.environment)
            overrideNames << kv.section(QLatin1Char('='), 0, 0);
        std::vector<QByteArray> envStore;
        for (char **e = environ; e && *e; ++e) {
            const QByteArray entry(*e);
            const QString name = QString::fromLocal8Bit(entry.left(entry.indexOf('=')));
            if (!overrideNames.contains(name))
                envStore.push_back(entry);
        }
        for (int i = 0; i < o.environment.size(); ++i) {
            const QString name = o.environment[i].section(QLatin1Char('='), 0, 0);
            bool laterWins = false;
            for (int j = i + 1; j < o.environment.size() && !laterWins; ++j)
                laterWins = o.environment[j].section(QLatin1Char('='), 0, 0) == name;
            if (!laterWins && !o.unsetEnvironment.contains(name))
                envStore.push_back(o.environment[i].toLocal8Bit());
        }
        std::vector<char *> envp;
        for (QByteArray &e : envStore)
            envp.push_back(e.data());
        envp.push_back(nullptr);

        const QByteArray exe = path.toLocal8Bit();
        const QByteArray cwd = o.workingDirectory.toLocal8Bit();

        // Upper bound for closing inherited descriptors in the child.
        struct rlimit rl {};
        const int maxFd = (::getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
            ? int(std::min<rlim_t>(rl.rlim_cur, 65536))
            : 65536;

        if (m_wake[0] < 0) {
#if defined(__linux__)
            if (::pipe2(m_wake, O_CLOEXEC | O_NONBLOCK) != 0) {
#else
            if (::pipe(m_wake) != 0) {
#endif
                m_error = QString::fromLocal8Bit(std::strerror(errno));
                m_wake[0] = m_wake[1] = -1;
                return false;
            }
            for (int fd : m_wake) {
                ::fcntl(fd, F_SETFD, FD_CLOEXEC);
                ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            }
        }

        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(std::max(1, o.rows));
        ws.ws_col = static_cast<unsigned short>(std::max(1, o.cols));
        ws.ws_xpixel = static_cast<unsigned short>(std::max(0, o.pixelWidth));
        ws.ws_ypixel = static_cast<unsigned short>(std::max(0, o.pixelHeight));

        int master = -1;
        const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
        if (pid < 0) {
            m_error = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        if (pid == 0) {
            if (!cwd.isEmpty() && ::chdir(cwd.constData()) != 0) {
                // keep the inherited directory
            }
            // SIG_IGN and the signal mask survive exec: reset them so Ctrl+C
            // works even when Relay itself was started with SIGINT ignored.
            for (int sig = 1; sig < NSIG; ++sig)
                ::signal(sig, SIG_DFL);
            sigset_t none;
            sigemptyset(&none);
            ::sigprocmask(SIG_SETMASK, &none, nullptr);
#if defined(__linux__) && defined(SYS_close_range)
            if (::syscall(SYS_close_range, 3U, ~0U, 0U) != 0) // ENOSYS before Linux 5.9
                for (int fd = 3; fd < maxFd; ++fd)
                    ::close(fd);
#else
            for (int fd = 3; fd < maxFd; ++fd)
                ::close(fd);
#endif
            ::execve(exe.constData(), argv.data(), envp.data());
            const char msg[] = "relay: exec failed\r\n";
            ssize_t ignored = ::write(2, msg, sizeof msg - 1);
            (void)ignored;
            ::_exit(127);
        }

        m_pid = pid;
        m_master = master;
#if defined(__APPLE__)
        if (m_master >= FD_SETSIZE || m_wake[0] >= FD_SETSIZE) {
            // select() cannot watch these descriptors. TODO(macos): kqueue.
            m_error = QStringLiteral("pty descriptor above FD_SETSIZE");
            ::kill(pid, SIGHUP);
            reapLater(pid);
            ::close(m_master);
            m_master = -1;
            m_pid = -1;
            return false;
        }
#endif
        ::fcntl(m_master, F_SETFD, FD_CLOEXEC);
        ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
        m_running = true;
        m_thread = std::thread([this] { run(); });
        return true;
    }

    QString errorString() const override { return m_error; }

    void write(const char *data, size_t len) override
    {
        if (len == 0 || !m_running.load())
            return;
        std::lock_guard<std::mutex> lock(m_writeMutex);
        size_t done = 0;
        if (m_pendingOffset >= m_pending.size()) {
            while (done < len) {
                const ssize_t n = ::write(m_master, data + done, len - done);
                if (n > 0) {
                    done += size_t(n);
                } else if (n < 0 && errno == EINTR) {
                    continue;
                } else {
                    break; // EAGAIN (queue the rest) or an error (the reader notices EOF)
                }
            }
        }
        if (done < len) {
            m_pending.append(data + done, len - done);
            poke();
        }
    }

    void resize(int rows, int cols, int pixelWidth, int pixelHeight) override
    {
        if (m_master < 0)
            return;
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
        ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
        ws.ws_xpixel = static_cast<unsigned short>(std::max(0, pixelWidth));
        ws.ws_ypixel = static_cast<unsigned short>(std::max(0, pixelHeight));
        ::ioctl(m_master, TIOCSWINSZ, &ws); // the kernel sends SIGWINCH to the foreground group
    }

    qint64 childPid() const override { return m_pid; }

    qint64 foregroundPid() const override
    {
        if (m_master < 0 || !m_running.load())
            return -1;
        const pid_t pg = ::tcgetpgrp(m_master);
        return pg > 0 ? pg : -1;
    }

    // tcgetattr() on the master: allowed from any process (unlike tcgetpgrp()'s
    // controlling-terminal rule for the slave), and it reports the flags the
    // slave sees, because both ends share one line discipline.
    TermiosFlags termiosFlags() const override
    {
        TermiosFlags flags;
        if (m_master < 0 || !m_running.load())
            return flags;
        struct termios state {};
        if (::tcgetattr(m_master, &state) != 0)
            return flags;
        flags.valid = true;
        flags.canonical = (state.c_lflag & ICANON) != 0;
        flags.echo = (state.c_lflag & ECHO) != 0;
        return flags;
    }

    bool isRunning() const override { return m_running.load(); }

    void terminate() override
    {
        if (m_pid > 0 && !m_reaped.load()) {
            ::kill(-m_pid, SIGHUP); // forkpty made the child a session and group leader
            ::kill(m_pid, SIGHUP);
        }
    }

private:
    void poke()
    {
        const char c = 1;
        ssize_t ignored = ::write(m_wake[1], &c, 1);
        (void)ignored;
    }

    void stopThread()
    {
        if (!m_thread.joinable())
            return;
        m_stop = true;
        poke();
        m_thread.join();
    }

    // Returns false on a fatal write error.
    void flushPending()
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        while (m_pendingOffset < m_pending.size()) {
            const ssize_t n = ::write(m_master, m_pending.data() + m_pendingOffset, m_pending.size() - m_pendingOffset);
            if (n > 0) {
                m_pendingOffset += size_t(n);
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else if (n < 0 && errno == EAGAIN) {
                return;
            } else {
                m_pending.clear();
                m_pendingOffset = 0;
                return;
            }
        }
        m_pending.clear();
        m_pendingOffset = 0;
    }

    bool hasPending()
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        return m_pendingOffset < m_pending.size();
    }

    // Wait for readability of the master or the wake pipe (and writability of the
    // master when output is queued). Returns false on stop.
    bool wait(bool *readable, bool *writable)
    {
        const bool wantWrite = hasPending();
        for (;;) {
#if defined(__APPLE__)
            // poll() does not support ptys on macOS.
            fd_set rfds, wfds;
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_SET(m_master, &rfds);
            FD_SET(m_wake[0], &rfds);
            if (wantWrite)
                FD_SET(m_master, &wfds);
            const int r = ::select(std::max(m_master, m_wake[0]) + 1, &rfds, &wfds, nullptr, nullptr);
            if (r < 0 && errno == EINTR)
                continue;
            *readable = FD_ISSET(m_master, &rfds);
            *writable = wantWrite && FD_ISSET(m_master, &wfds);
            const bool woke = FD_ISSET(m_wake[0], &rfds);
#else
            struct pollfd fds[2] = {{m_master, short(POLLIN | (wantWrite ? POLLOUT : 0)), 0}, {m_wake[0], POLLIN, 0}};
            const int r = ::poll(fds, 2, -1);
            if (r < 0 && errno == EINTR)
                continue;
            *readable = (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
            *writable = (fds[0].revents & POLLOUT) != 0;
            const bool woke = (fds[1].revents & POLLIN) != 0;
#endif
            if (woke) {
                char drain[64];
                while (::read(m_wake[0], drain, sizeof drain) > 0) {
                }
            }
            return !m_stop.load();
        }
    }

    void run()
    {
        std::vector<char> buf(65536);
        bool eof = false;
        while (!eof) {
            bool readable = false, writable = false;
            if (!wait(&readable, &writable))
                return;
            if (writable)
                flushPending();
            if (!readable)
                continue;
            // Bounded read burst so queued input (Ctrl+C) interleaves with a flood.
            size_t burst = 0;
            while (burst < (1u << 20)) {
                const ssize_t n = ::read(m_master, buf.data(), buf.size());
                if (n > 0) {
                    burst += size_t(n);
                    if (onOutput)
                        onOutput(buf.data(), size_t(n));
                    if (hasPending())
                        flushPending();
                    if (m_stop.load())
                        return;
                    continue;
                }
                if (n < 0 && errno == EINTR)
                    continue;
                if (n < 0 && errno == EAGAIN)
                    break;
                eof = true; // 0 or EIO: every slave fd is closed
                break;
            }
        }
        m_running = false;
        // EOF means no process holds the slave any more; the child itself can
        // still be alive (it may have redirected its descriptors). Wait for it
        // without blocking a destructor that wants this thread to stop.
        int status = 0;
        int code = -1;
        while (m_pid > 0) {
            const pid_t r = ::waitpid(m_pid, &status, WNOHANG);
            if (r == m_pid) {
                code = exitCodeFromStatus(status);
                m_reaped = true;
                break;
            }
            if (r < 0 && errno != EINTR)
                break;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(m_wake[0], &rfds);
            struct timeval tv {0, 50000};
            ::select(m_wake[0] + 1, &rfds, nullptr, nullptr, &tv);
            if (m_stop.load())
                return; // the destructor hangs up and reaps
        }
        if (onFinished)
            onFinished(code);
    }

    int m_master = -1;
    pid_t m_pid = -1;
    int m_wake[2] = {-1, -1};
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_reaped{false};
    std::mutex m_writeMutex;
    std::string m_pending;
    size_t m_pendingOffset = 0;
    QString m_error;
};

} // namespace

std::unique_ptr<Pty> Pty::create()
{
    return std::make_unique<UnixPty>();
}

} // namespace relay
