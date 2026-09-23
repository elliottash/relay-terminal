// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A background task settles with its request ledger, not with a model turn.
#include "RequestLedger.h"
#include <QSet>

namespace relay::background {

enum class State { Working, NeedsYou, Done, Failed, Interrupted };

struct Facts {
    bool interrupted = false;
    bool asking = false;
    bool lastAsked = false;
    bool agentBusy = false;
    bool guestBusy = false;
    bool otherWork = false;
    int liveSubagents = 0;
    QString lastOutcome;
    // Empty when no card was handed to the pane. "unknown" keeps a card-backed task working
    // until the Board index has seen its verification lane.
    QString cardStatus;
    int minRequestNumber = 1;
};

inline int requestNumber(const QString &id) {
    if (!id.startsWith(QLatin1Char('R'))) return 0;
    bool ok = false;
    const int number = id.mid(1).toInt(&ok);
    return ok ? number : 0;
}

inline State resolve(const Facts &facts, const RequestLedgerModel &ledger) {
    if (facts.interrupted) return State::Interrupted;
    if (facts.asking || (facts.lastAsked && !facts.agentBusy)
        || (facts.lastOutcome == QStringLiteral("cancelled") && !facts.agentBusy)) return State::NeedsYou;
    QSet<QString> owned;
    for (const auto &request : ledger.requests())
        if (requestNumber(request.id) >= facts.minRequestNumber) owned.insert(request.id);
    const auto belongs = [&owned](const LedgerTodo &todo) {
        for (const QString &id : todo.requestIds) if (owned.contains(id)) return true;
        return false;
    };
    for (const auto &todo : ledger.todos()) {
        if (belongs(todo) && (todo.status == QStringLiteral("blocked")
            || todo.status == QStringLiteral("deferred") || todo.status == QStringLiteral("cancelled")))
            return State::NeedsYou;
    }
    for (const auto &request : ledger.requests()) {
        if (owned.contains(request.id) && (request.status == QStringLiteral("blocked")
            || request.status == QStringLiteral("deferred") || request.status == QStringLiteral("cancelled")))
            return State::NeedsYou;
    }
    if (facts.lastOutcome == QStringLiteral("error") && !facts.agentBusy) return State::Failed;
    if (facts.agentBusy || facts.guestBusy || facts.otherWork || facts.liveSubagents > 0) return State::Working;
    if (owned.isEmpty()) return State::Working;
    for (const auto &todo : ledger.todos()) if (belongs(todo) && (todo.open() || todo.subagentRunning)) return State::Working;
    for (const auto &request : ledger.requests())
        if (owned.contains(request.id) && (request.open() || request.status != QStringLiteral("done")))
            return State::Working;
    if (!facts.cardStatus.isEmpty() && facts.cardStatus != QStringLiteral("needs-verification")
        && facts.cardStatus != QStringLiteral("done")) return State::Working;
    return State::Done;
}

inline QString name(State state) {
    switch (state) {
    case State::Working: return QStringLiteral("working");
    case State::NeedsYou: return QStringLiteral("needs-you");
    case State::Done: return QStringLiteral("done");
    case State::Failed: return QStringLiteral("failed");
    case State::Interrupted: return QStringLiteral("interrupted");
    }
    return QStringLiteral("working");
}

}  // namespace relay::background
