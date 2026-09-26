// SPDX-License-Identifier: AGPL-3.0-or-later
// A console's linked shell (card #2FQ9): the pty an agent console runs while it is popped out of
// its artifact into a leaf of its own. `Pane::startTerminal` is not used for it, and is not
// changed for it: that function builds the transcript surface *and* the pty, and a console
// already has the surface — its transcript is on it — so only the pty is started here, on the
// backend the console has had since it was made. The tab worker is untouched: a console's line
// to its agent still goes out through `onWorkerLine`, and no second worker starts.
#include "Pane.h"
#include "AppPaths.h"   // relayBash, relayPowerShell, relay::scratchpaths

bool Pane::setLinkedShell(bool on, const QString &cwd) {
    // A terminal pane has its shell already; only a console is ever linked.
    if (!m_context || m_context == &m_terminalContext) return false;
    if (on == m_linkedShell) return true;
    if (on) {
        if (!m_backend) {
            status(QStringLiteral("This agent has no terminal surface yet · try again in a moment"));
            return false;
        }
        if (!cwd.isEmpty() && QFileInfo(cwd).isDir()) {
            m_cwd = cwd;
            updatePaths();
        }
        // The host may have folded its dock; the leaf this console now stands in is not folded.
        m_collapsedWhenDocked = m_collapsed;
        if (m_collapsed) {
            m_collapsed = false;
            setVisible(true);
        }
        m_linkedShell = true;
        contextChanged();   // hasShell() is true now: routing, chip, header
        // A card page hides a transcript nothing has printed on yet; a shell needs it shown.
        applyTranscriptVisibility();
        // Docked and popped straight out again: the last shell has been asked to go but has not
        // gone, and a session runs one program at a time. The new one starts when it has.
        if (m_linkedShellStopping || m_backend->isRunning()) {
            m_linkedStartPending = true;
            return true;
        }
        if (!startLinkedPty()) {
            m_linkedShell = false;
            contextChanged();
            return false;
        }
        return true;
    }
    m_linkedShell = false;
    m_linkedStartPending = false;
    m_poll.stop();
    m_secretPoll.stop();
    m_programPoll.stop();
    if (m_backend && m_backend->isRunning()) {
        m_linkedShellStopping = true;
        m_backend->stopProgram();
    }
    m_shellReady = false; m_promptReported = false; m_loading = false; m_shellPid = 0;
    if (!m_prefixMode.isEmpty()) clearPrefixMode(true);
    contextChanged();       // back to a console: the line is the agent's, the chip goes
    applyTranscriptVisibility();
    if (m_collapsedWhenDocked) setCollapsed(true);
    m_collapsedWhenDocked = false;
    return true;
}

bool Pane::startLinkedPty() {
    // What `restartShell` resets before a new shell, for the same reason: nothing about a shell
    // that ran before may be read as this one's.
    m_shellStopped = false; m_shellPid = 0; m_shellReady = false; m_promptReported = false;
    m_loading = false; m_seenShell = false; m_shellSequence.clear(); m_stateSeen = false;
    m_atLineStart = true; m_autoHuman = false; m_shellResizeHeld = false;
    if (!m_linkedFinishHooked) {
        // The console's surface was built by `startTerminal`, whose `onFinished` drops the backend
        // (a terminal pane's shell ending ends its terminal). Here the backend is the transcript
        // and outlives any number of linked shells, so their end is taken first.
        m_linkedFinishHooked = true;
        auto previous = m_backend->onFinished;
        m_backend->onFinished = [this, previous](int code) {
            if (m_linkedShell || m_linkedShellStopping) { linkedPtyFinished(code); return; }
            if (previous) previous(code);
        };
    }
    // Per pane, so in the child's own environment rather than Relay's: another pane may have
    // started since this console's surface was built, and Relay's environment is theirs now.
    const QString backendDir = m_data + QStringLiteral("/backend");
    QDir().mkpath(guestEventsDir());
    QDir().mkpath(guestAnswersDir());
    QStringList environment{
        QStringLiteral("RELAY_RUNTIME_DIR=") + m_runtime.path(),
        QStringLiteral("RELAY_SESSION_TOKEN=") + m_token,
        QStringLiteral("RELAY_SHELL_EVENT=") + m_data + QStringLiteral("/shell/event.py"),
        QStringLiteral("RELAY_GUEST_EVENT=") + guestEventsDir(),
        QStringLiteral("RELAY_BACKEND_DIR=") + backendDir,
        QStringLiteral("RELAY_PYTHON=") + m_python,
        QStringLiteral("RELAY_CLEAN_SHELL=") + (m_cleanShell ? QStringLiteral("1") : QStringLiteral("0")),
        QStringLiteral("RELAY_START_DIR=") + m_cwd,
        QStringLiteral("RELAY_PANE_ID=") + scrollbackId(),
    };
    if (qEnvironmentVariableIsEmpty("MPLBACKEND"))
        environment << QStringLiteral("MPLBACKEND=module://relay_mpl_backend");
    const QString parentPythonPath = qEnvironmentVariable("PYTHONPATH");
    const QString scriptsPath = m_data + QStringLiteral("/scripts");
    environment << QStringLiteral("PYTHONPATH=")
                       + (parentPythonPath.isEmpty() ? scriptsPath : parentPythonPath + QDir::listSeparator() + scriptsPath);
    const QString scratchTmp = QDir(relay::scratchpaths::sessionRoot(m_token)).filePath(QStringLiteral("tmp"));
    if (QDir().mkpath(scratchTmp)) environment << QStringLiteral("TMPDIR=") + scratchTmp;
#ifdef Q_OS_WIN
    const QStringList shell{relayPowerShell(), QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
        QStringLiteral("-NoExit"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), m_data + QStringLiteral("/shell/integration.ps1")};
#else
    const QStringList shell{relayBash(), QStringLiteral("--noprofile"),
        QStringLiteral("--rcfile"), m_data + QStringLiteral("/shell/integration.bash"), QStringLiteral("-i")};
#endif
    // No local holder (#87HB): the linked shell lives exactly as long as the link, and a tmux
    // session left behind by one would be re-attached by nothing. Memory isolation is kept, the
    // same scope a pane's shell gets.
    // The shell's first line starts on a line of its own, not after the transcript's last word.
    if (m_backend->cursorPosition().x() != 0) m_backend->writeToDisplay("\r\n");
    m_shellUnit.clear();
    bool started = false;
    if (isolation::enabled() && isolation::available()) {
        isolation::ensureTotalCeiling();
        m_shellUnit = QStringLiteral("relay-pane-%1-shell-%2").arg(m_token.left(8)).arg(++m_shellGeneration);
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
        const QString max = isolation::memory("isolation/shell_memory_max", isolation::shellDefault());
        started = m_backend->startProgram(tool, isolation::wrap(m_shellUnit,
            {QStringLiteral("MemoryMax=") + max,
             QStringLiteral("MemoryHigh=") + isolation::memory("isolation/shell_memory_high", isolation::fractionOf(max, 80)),
             QStringLiteral("MemorySwapMax=") + isolation::memory("isolation/shell_swap_max", isolation::shellSwapDefault()),
             QStringLiteral("KillSignal=SIGHUP"), QStringLiteral("TimeoutStopSec=5"),
             QStringLiteral("OOMPolicy=continue")}, shell), m_cwd, environment);
    } else {
        started = m_backend->startProgram(shell.first(), shell.mid(1), m_cwd, environment);
    }
    if (!started) {
        status(QStringLiteral("The linked shell could not be started"));
        return false;
    }
    m_oomKills = -1;
    registerWithBridge();
    m_secretPoll.start(1000);
    m_poll.start(kPollFastMs);
    return true;
}

void Pane::linkedPtyFinished(int code) {
    Q_UNUSED(code);
    const bool asked = m_linkedShellStopping;
    m_linkedShellStopping = false;
    m_shellReady = false; m_promptReported = false; m_loading = false; m_shellPid = 0;
    m_poll.stop();
    m_secretPoll.stop();
    m_programPoll.stop();
    if (m_linkedStartPending && m_linkedShell) {
        m_linkedStartPending = false;
        if (!startLinkedPty()) setLinkedShell(false);
        return;
    }
    if (asked || !m_linkedShell) return;
    // The shell ended on its own — `exit`, or it died. The console goes home: the window docks it
    // back, which also turns the link off here.
    if (onLinkedShellEnded) QTimer::singleShot(0, this, [this] { if (onLinkedShellEnded) onLinkedShellEnded(); });
    else setLinkedShell(false);
}
