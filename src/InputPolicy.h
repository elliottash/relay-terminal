// SPDX-License-Identifier: GPL-3.0-or-later
// relay::input: the rules behind "the prompt box is the only keyboard input" (Warp-style).
//
// The Pane owns the state (what the terminal's line discipline says, whether a foreground
// program is running and whether it is blocked reading the terminal); these functions turn
// that state into a decision, so both terminal engines behave the same and the rules can be
// tested without a shell. See issues/changes/needs_qa_llm/2026-09-17-terminal-not-directly-typable.md.
#pragma once

#include <QString>

namespace relay::input {

// What the terminal's line discipline says about who is reading the pane's tty.
enum class TerminalMode {
    Unknown,    // no terminal, or tcgetattr failed
    Raw,        // ICANON off: Readline, or a full-screen program such as vim
    Echoing,    // canonical input with echo: a shell prompt, or a program reading a line
    Secret,     // canonical input with echo off: a password prompt
};

// Where a line submitted from the prompt box goes.
enum class LineTarget {
    Shell,     // run it in the shell now, or queue it (the existing behaviour)
    Program,   // write it to the foreground program's stdin: it is reading a line right now
    Agent,     // the pane's agent
};

struct State {
    TerminalMode mode = TerminalMode::Unknown;
    bool programRunning = false;   // a foreground process group other than the pane's shell
    bool programReading = false;   // a process of it is blocked in read() on the terminal
    bool altScreen = false;        // full-screen program: only native input types into it
    bool native = false;           // the user took control; the prompt box is hidden
    // What the screen-text classifier (src/ScreenPrompt.h) makes of the last rows. Both are
    // false on engines that cannot read the screen, which leaves the /proc-only rules below
    // exactly as they were.
    bool screenAsking = false;     // a program is visibly asking for a line ("[Y/n]", "Continue? ")
    bool screenMasked = false;     // ... and it is a password prompt
};

// Why the agent may not type into the foreground program right now.
enum class TypeRefusal {
    None,           // it may
    UserInControl,  // the user took the keyboard (Ctrl+H, the button, or typing)
    NotAsked,       // the user has not handed this program to the agent for this turn
    NoProgram,      // nothing is running in the pane's terminal
    Password,       // a masked prompt: the agent never types a password
};

// A password prompt: the program kept canonical (line) input but turned echo off.
// Full-screen programs and Readline turn canonical input off, so they never match.
bool secretPrompt(const State &state);

// An ordinary program waiting for a line (`apt`'s `[Y/n]`): canonical input with echo on,
// and a process of the command blocked reading the terminal.
bool lineRequested(const State &state);

// The rule for a line submitted from the prompt box. `mode` is the pane's routing mode
// ("auto", "shell" or "agent"); an agent submission is never diverted to a program.
LineTarget targetFor(const State &state, const QString &mode);

// May the agent type into the foreground program? `delegated` is the user's consent for this
// turn (the delegate action, the banner button, or "let the agent answer this"); it is never
// inferred. Checked again in the Pane immediately before every write, so a take-over that
// lands mid-turn stops the next keystroke.
TypeRefusal agentTypeRefusal(const State &state, bool delegated);
// The sentence the agent (and the pane) is told when a write is refused.
QString typeRefusalText(TypeRefusal refusal, const QString &program);
// "✦ typed: y" — the inline line printed in the pane for every write the agent makes.
QString typedLine(const QString &text);

// True when a full-screen (or remote) program owns the terminal and the prompt box still has
// the keyboard: the pane offers "Take control" instead of switching by itself.
bool offerTakeControl(const State &state, bool remoteSession);

// May a submitted line be kept anywhere (prompt history, the queue, the request ledger, the
// session file, logs, route assist, suggestions, model prompts)? Lines written to a running
// program are answers to that program, not commands; a password is never kept at all.
bool retainable(LineTarget target, bool secret);

// "sent to apt" — the status line shown after a line went to the running program.
QString sentToProgram(const QString &program);
// "password for sudo" — the chip beside the masked prompt box.
QString passwordChip(const QString &program);

// Overwrite a string's characters in place, keeping its length. Detaches first, so the zeroes
// land in the buffer this QString owns; a copy taken earlier keeps its own characters.
void zero(QString &text);
// zero(), then empty the string.
void wipe(QString &text);

// A line typed at a password prompt. It only ever travels from the masked prompt box to the
// program's stdin: take() hands it over once and wipes the stored copy.
class Secret {
public:
    Secret() = default;
    Secret(const Secret &) = delete;
    Secret &operator=(const Secret &) = delete;
    ~Secret() { wipe(); }

    // Keeps a private deep copy, so wiping cannot leave a shared one behind.
    void set(const QString &text);
    bool isEmpty() const { return m_text.isEmpty(); }
    int size() const { return int(m_text.size()); }
    // The line plus the newline the program is waiting for; the stored copy is wiped.
    QString take();
    void wipe();

private:
    QString m_text;
};

// Wrong-mode hints: whether a run_command the agent issued is the line the user submitted in
// agent mode. The agent tends to wrap a user's command — `cd <dir> && `, `cd "<dir>"; `, or a
// trailing `2>&1` — so those are stripped before comparing, with whitespace collapsed.
bool commandMatchesPrompt(const QString &command, const QString &prompt);

}  // namespace relay::input
