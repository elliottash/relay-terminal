// SPDX-License-Identifier: GPL-3.0-or-later
// relay::screen: "is the foreground program waiting for me to type something?", decided from the
// last rows of the terminal screen.
//
// Relay's own engine can read the screen (relay::TerminalBackend::ScreenText); KonsolePart on
// KF5 cannot, so panes without that capability keep the old /proc-only behaviour and say so.
// The /proc signals Relay already has (termios line discipline, a process blocked in read())
// are one input here, not the whole answer: `sudo` runs its child in its own pseudo-terminal,
// so the real reader is invisible, and the screen text is the only evidence left.
//
// Everything here is a pure function over text plus a small signal struct, so the rules can be
// tested against recorded screens without a terminal. Fixtures: tests/fixtures/screen/.
// Cards: issues/features/2026-09-17-screen-text-input-detection.md (#YR21) and
// issues/features/2026-09-17-agent-delegate-and-take-over.md (#C1HH).
#pragma once

#include "InputPolicy.h"

#include <QString>
#include <QStringList>

namespace relay::screen {

// How many rows from the bottom of the screen the classifier looks at. A prompt never needs
// more: the question is on the cursor row, at most with its heading on the row above.
inline constexpr int kInspectRows = 8;
// Longest question kept for the banner; longer lines are elided from the left.
inline constexpr int kQuestionChars = 160;
// A detection at or above this confidence may be acted on (answered from the prompt box, or
// typed into by the agent). Below it the pane only keeps its previous behaviour.
inline constexpr double kActConfidence = 0.60;

enum class Kind {
    None,        // nothing is being asked: output, a finished command, an empty screen
    ShellPrompt, // the shell's own prompt: the command ended, the next line is a command
    YesNo,       // "[Y/n]", "(yes/no)", "(yes/no/[fingerprint])"
    Choice,      // a numbered menu: "type selection number:"
    Password,    // "Password:", "[sudo] password for …", "Enter passphrase for key …"
    PressKey,    // "Press ENTER to continue", "--More--", "(END)"
    FreeText,    // any other line ending in a prompt: `read -p`, python input(), ">>> "
};

const char *kindName(Kind kind);

// The /proc signals Relay already collects (src/InputPolicy.h), plus whether the pane's engine
// can read the screen at all. `screenReadable` false means `rows` is empty and only those
// signals are available — the KonsolePart case.
struct Signals {
    relay::input::TerminalMode mode = relay::input::TerminalMode::Unknown;
    bool programRunning = false;  // a foreground process group other than the pane's shell
    bool programReading = false;  // a process of it is blocked in read() on the terminal
    bool altScreen = false;       // a full-screen program owns the screen
    bool screenReadable = true;   // the engine reports TerminalBackend::ScreenText
};

struct Detection {
    Kind kind = Kind::None;
    QString question;       // the line being asked, trimmed and capped at kQuestionChars
    QString options;        // the option token as written: "[Y/n]", "(yes/no/[fingerprint])"
    QString defaultAnswer;  // "y" / "n" / "" — the capitalized option, when there is one
    double confidence = 0.0;
    bool masked = false;    // a password: never echo it, never let the agent type it

    // A program is waiting for a line and Relay is sure enough to act on it.
    bool waiting() const { return kind != Kind::None && kind != Kind::ShellPrompt; }
    bool actionable() const { return waiting() && confidence >= kActConfidence; }
};

// The last `count` rows of a '\n'-joined screen snapshot (relay::TerminalBackend::screenText).
QStringList lastRows(const QString &screen, int count = kInspectRows);

// The classifier. `rows` is the bottom of the screen, oldest first; only the last kInspectRows
// are read. An empty `rows` (or screenReadable false) falls back to the signals alone.
Detection detect(const QStringList &rows, const Signals &sig);

// The shell's own prompt: a trailing "$", "#", "%" or arrow with something path- or host-shaped
// before it. The classifier decides a whole screen; this answers for one line, which is what a
// caller with a cursor position already knows to ask about (card #S5SH, a shell inside a remote
// tmux: the last row of the screen is tmux's status bar, the cursor's row is the prompt).
bool isShellPrompt(const QString &line);

// "apt is asking: Do you want to continue? [Y/n]" — the take-control banner's label.
// Empty when nothing is being asked. A password is named, never quoted back with its line.
QString bannerText(const QString &program, const Detection &detection);

// "apt is waiting for input · Enter sends your line to it" — the composer row's line.
QString waitingLine(const QString &program, const Detection &detection);

}  // namespace relay::screen
