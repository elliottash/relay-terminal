// SPDX-License-Identifier: GPL-3.0-or-later
#include "InputPolicy.h"

#include <algorithm>

namespace relay::input {

bool secretPrompt(const State &state) {
    return state.programRunning && !state.altScreen && state.mode == TerminalMode::Secret;
}

bool lineRequested(const State &state) {
    return state.programRunning && !state.altScreen && state.programReading
        && state.mode == TerminalMode::Echoing;
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

}  // namespace relay::input
