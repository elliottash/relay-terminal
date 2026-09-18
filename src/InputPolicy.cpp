// SPDX-License-Identifier: GPL-3.0-or-later
#include "InputPolicy.h"

#include <QRegularExpression>

#include <algorithm>

namespace relay::input {

bool secretPrompt(const State &state) {
    return state.programRunning && !state.altScreen && state.mode == TerminalMode::Secret;
}

bool lineRequested(const State &state) {
    // Canonical input with echo means the terminal is collecting a line for somebody. Who that
    // is, is either proved by /proc (a process of the command blocked in read() on the tty) or
    // shown on the screen ("Do you want to continue? [Y/n]"). `sudo` runs its child in its own
    // pseudo-terminal, so for the case this feature exists for only the screen can tell.
    return state.programRunning && !state.altScreen && state.mode == TerminalMode::Echoing
        && (state.programReading || state.screenAsking);
}

TypeRefusal agentTypeRefusal(const State &state, bool delegated) {
    if (state.native) return TypeRefusal::UserInControl;
    // A masked prompt outranks every other reason, so the agent is always told the true one:
    // Relay drops the delegation the moment a password prompt appears, and "not asked" would
    // hide why. Either way nothing is typed.
    if (state.screenMasked || secretPrompt(state)) return TypeRefusal::Password;
    // "Nothing is running" outranks "you were not asked": a program that exited while the agent
    // was thinking should not be reported as a permission problem.
    if (!state.programRunning) return TypeRefusal::NoProgram;
    if (!delegated) return TypeRefusal::NotAsked;
    return TypeRefusal::None;
}

QString typeRefusalText(TypeRefusal refusal, const QString &program) {
    const QString who = program.isEmpty() ? QStringLiteral("the program") : program;
    switch (refusal) {
    case TypeRefusal::None:
        return {};
    case TypeRefusal::UserInControl:
        return QStringLiteral("The user took control of the terminal; nothing was typed.");
    case TypeRefusal::NotAsked:
        return QStringLiteral("The user has not asked you to drive the program in the terminal pane; "
                              "nothing was typed. Tell them what to type, or ask them to delegate it.");
    case TypeRefusal::NoProgram:
        return QStringLiteral("No program is running in the user's terminal pane; nothing was typed.");
    case TypeRefusal::Password:
        return QStringLiteral("%1 is asking for a password. Relay never types into a password prompt; "
                              "nothing was typed. Ask the user to type it.").arg(who);
    }
    return {};
}

QString typedLine(const QString &text) {
    QString shown = text;
    shown.replace(QLatin1Char('\n'), QStringLiteral("⏎"));
    shown.replace(QLatin1Char('\t'), QStringLiteral("⇥"));
    shown = shown.simplified();
    if (shown.size() > 120) shown = shown.left(119) + QStringLiteral("…");
    return shown.isEmpty() ? QStringLiteral("✦ typed: ⏎") : QStringLiteral("✦ typed: %1").arg(shown);
}

LineTarget targetFor(const State &state, const QString &mode) {
    if (mode == QStringLiteral("agent")) return LineTarget::Agent;
    // Native input means the user types into the terminal directly; nothing is submitted here.
    if (state.native) return LineTarget::Shell;
    if (secretPrompt(state) || lineRequested(state)) return LineTarget::Program;
    return LineTarget::Shell;
}

bool offerTakeControl(const State &state, bool remoteSession) {
    return !state.native && state.programRunning && (state.altScreen || remoteSession);
}

bool retainable(LineTarget target, bool secret) {
    return !secret && target != LineTarget::Program;
}

QString sentToProgram(const QString &program) {
    return QStringLiteral("Sent to %1").arg(program.isEmpty() ? QStringLiteral("the program") : program);
}

QString passwordChip(const QString &program) {
    return QStringLiteral("password for %1").arg(program.isEmpty() ? QStringLiteral("the program") : program);
}

void zero(QString &text) {
    if (text.isEmpty()) return;
    // data() detaches first, so the zeroes land in the buffer this QString owns.
    QChar *raw = text.data();
    std::fill(raw, raw + text.size(), QChar(u'\0'));
}

void wipe(QString &text) {
    zero(text);
    text.clear();
}

void Secret::set(const QString &text) {
    wipe();
    m_text = QString(text.constData(), text.size());   // a deep copy this object owns alone
}

QString Secret::take() {
    QString line(m_text.constData(), m_text.size());
    line.append(QLatin1Char('\n'));
    wipe();
    return line;
}

void Secret::wipe() { relay::input::wipe(m_text); }

bool commandMatchesPrompt(const QString &command, const QString &prompt) {
    if (command.trimmed().isEmpty() || prompt.trimmed().isEmpty()) return false;
    // A leading `cd <dir>` joined by && or ;, where <dir> may be quoted with spaces.
    static const QRegularExpression cdPrefix(
        QStringLiteral(R"(^cd\s+(?:"[^"]*"|'[^']*'|\S+)\s*(?:&&|;)\s*)"));
    // A trailing redirect of stderr into stdout, which agents add to see errors.
    static const QRegularExpression stderrSuffix(QStringLiteral(R"(\s+2>&1\s*$)"));
    QString run = command.simplified();
    run.remove(cdPrefix);
    run.remove(stderrSuffix);
    QString typed = prompt.simplified();
    typed.remove(stderrSuffix);
    return run == typed;
}

}  // namespace relay::input
