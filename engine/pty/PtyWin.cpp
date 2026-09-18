// SPDX-License-Identifier: GPL-3.0-or-later
// Windows relay::Pty over ConPTY. NOT IMPLEMENTED YET: start() fails with an
// explanatory error so the rest of the engine can be compiled and tested on
// Windows.
//
// TODO(windows): implement with ConPTY (Windows 10 1809+):
//  - CreatePipe x2 (input, output); CreatePseudoConsole(size, inRead, outWrite, 0, &hpc).
//  - STARTUPINFOEXW with PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE; CreateProcessW with
//    EXTENDED_STARTUPINFO_PRESENT, a UTF-16 environment block and the working directory.
//  - Reader thread: ReadFile on the output pipe -> onOutput (pipes have no
//    readiness notification, so the thread blocks in ReadFile). Writer: WriteFile
//    on the input pipe from a queue, like PtyUnix.
//  - resize(): ResizePseudoConsole. terminate(): ClosePseudoConsole, then
//    TerminateProcess after a timeout. Exit code: WaitForSingleObject +
//    GetExitCodeProcess -> onFinished.
//  - foregroundPid(): no ConPTY equivalent; return -1 and rely on OSC 7/133 from
//    shell integration (or a job object to enumerate the process tree).
//  - ConPTY re-renders output from its own screen buffer; expect differences in
//    wrapping and alternate-screen handling compared with Unix ptys.
//  References (MIT): Qt Creator's patched ptyqt copy (src/libs/3rdparty/libptyqt),
//  wezterm's portable-pty (pty/src/win). Do not depend on upstream Pty-Qt.
#include "Pty.h"

namespace relay {
namespace {

class ConPty final : public Pty {
public:
    bool start(const StartOptions &) override
    {
        m_error = QStringLiteral("ConPTY backend not implemented yet (engine/pty/PtyWin.cpp)");
        return false;
    }
    QString errorString() const override { return m_error; }
    void write(const char *, size_t) override {}
    void resize(int, int, int, int) override {}
    qint64 childPid() const override { return -1; }
    qint64 foregroundPid() const override { return -1; }
    TermiosFlags termiosFlags() const override { return {}; } // ConPTY has no line discipline to read
    bool isRunning() const override { return false; }
    void terminate() override {}

private:
    QString m_error;
};

} // namespace

std::unique_ptr<Pty> Pty::create()
{
    return std::make_unique<ConPty>();
}

} // namespace relay
