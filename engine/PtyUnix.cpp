// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pty.h"

#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

namespace relay {
namespace {

class UnixPty final : public Pty {
public:
    ~UnixPty() override
    {
        delete m_notifier;
        if (m_fd >= 0)
            ::close(m_fd);
        if (m_pid > 0) {
            ::kill(m_pid, SIGHUP);
            ::waitpid(m_pid, nullptr, WNOHANG);
        }
    }

    bool start(const StartOptions &o) override
    {
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(o.rows);
        ws.ws_col = static_cast<unsigned short>(o.cols);

        QByteArray program = o.program.toLocal8Bit();
        if (program.isEmpty())
            program = qgetenv("SHELL").isEmpty() ? QByteArray("/bin/bash") : qgetenv("SHELL");
        QList<QByteArray> args{program};
        for (const QString &a : o.arguments)
            args << a.toLocal8Bit();
        QByteArray cwd = o.workingDirectory.toLocal8Bit();
        QList<QByteArray> env;
        for (const QString &e : o.extraEnvironment)
            env << e.toLocal8Bit();

        // Prepare argv before fork: only async-signal-safe calls in the child.
        std::vector<char *> argv;
        for (QByteArray &a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);

        pid_t pid = ::forkpty(&m_fd, nullptr, nullptr, &ws);
        if (pid < 0)
            return false;
        if (pid == 0) {
            for (QByteArray &e : env)
                ::putenv(e.data());
            if (!cwd.isEmpty() && ::chdir(cwd.constData()) != 0) {
                // keep inherited cwd
            }
            // Reset every signal disposition and the mask: SIG_IGN survives exec,
            // so a Relay started from a script with `&` (SIGINT/SIGQUIT ignored)
            // would otherwise give the shell's children an unkillable Ctrl+C.
            for (int sig = 1; sig < NSIG; ++sig)
                ::signal(sig, SIG_DFL);
            sigset_t none;
            sigemptyset(&none);
            ::sigprocmask(SIG_SETMASK, &none, nullptr);
            ::execvp(argv[0], argv.data());
            ::_exit(127);
        }
        m_pid = pid;
        ::fcntl(m_fd, F_SETFL, ::fcntl(m_fd, F_GETFL) | O_NONBLOCK);
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read);
        QObject::connect(m_notifier, &QSocketNotifier::activated, [this] { readAvailable(); });
        return true;
    }

    qint64 write(const char *data, qint64 len) override
    {
        if (m_fd < 0)
            return -1;
        qint64 done = 0;
        while (done < len) {
            ssize_t n = ::write(m_fd, data + done, static_cast<size_t>(len - done));
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN) {
                    // Spike simplification: brief blocking retry. Production code
                    // needs a write queue driven by a write notifier.
                    ::usleep(1000);
                    continue;
                }
                return done;
            }
            done += n;
        }
        return done;
    }

    void resize(int rows, int cols) override
    {
        if (m_fd < 0)
            return;
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(rows);
        ws.ws_col = static_cast<unsigned short>(cols);
        ::ioctl(m_fd, TIOCSWINSZ, &ws); // kernel delivers SIGWINCH to the foreground group
    }

    qint64 childPid() const override { return m_pid; }
    qint64 foregroundPid() const override { return m_fd >= 0 ? ::tcgetpgrp(m_fd) : -1; }
    bool isRunning() const override { return m_fd >= 0; }

private:
    void readAvailable()
    {
        // Drain with a time budget so a flood (cat of a huge file) cannot starve
        // painting and input. The notifier fires again if data remains.
        char buf[65536];
        QElapsedTimer t;
        t.start();
        for (;;) {
            if (t.elapsed() >= 12) {
                // Budget used: yield one event-loop pass (input, timers, paint)
                // before reading more, so a flood cannot monopolise the GUI thread.
                m_notifier->setEnabled(false);
                QTimer::singleShot(0, m_notifier, [this] {
                    if (m_fd >= 0)
                        m_notifier->setEnabled(true);
                });
                return;
            }
            ssize_t n = ::read(m_fd, buf, sizeof buf);
            if (n > 0) {
                if (onOutput)
                    onOutput(buf, n);
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && errno == EAGAIN)
                return;
            finish(); // EOF / EIO: slave closed
            return;
        }
    }

    void finish()
    {
        m_notifier->setEnabled(false);
        ::close(m_fd);
        m_fd = -1;
        int status = 0;
        int code = -1;
        if (m_pid > 0 && ::waitpid(m_pid, &status, 0) == m_pid && WIFEXITED(status))
            code = WEXITSTATUS(status);
        m_pid = -1;
        if (onFinished)
            onFinished(code);
    }

    int m_fd = -1;
    pid_t m_pid = -1;
    QSocketNotifier *m_notifier = nullptr;
};

} // namespace

std::unique_ptr<Pty> Pty::create()
{
    return std::make_unique<UnixPty>();
}

} // namespace relay
