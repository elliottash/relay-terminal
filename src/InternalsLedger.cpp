// SPDX-License-Identifier: AGPL-3.0-or-later
#include "InternalsLedger.h"

namespace relay::internals {

HiddenTurn &Ledger::turnFor(const QString &turnId) {
    if (!m_turns.isEmpty() && m_turns.last().turnId == turnId) return m_turns.last();
    // A turn that already has a block and then comes back (an event out of order, or a second
    // block of the same turn after another turn's row) keeps its own block: the reprint is grouped
    // per turn, and two blocks for one turn under two rules would read as two turns.
    for (int at = m_turns.size() - 1; at >= 0; --at)
        if (m_turns.at(at).turnId == turnId) return m_turns[at];
    m_turns.append(HiddenTurn{turnId, QString(), {}});
    // The bound is on turns, not rows: the worker keeps the last 50 turns' detail, so a row from
    // an older one would reprint with a fold nothing can fill.
    while (m_turns.size() > kMaxTurns) { m_turns.removeFirst(); ++m_dropped; }
    return m_turns.last();
}

void Ledger::setRequest(const QString &turnId, const QString &request) {
    if (request.trimmed().isEmpty()) return;
    HiddenTurn &turn = turnFor(turnId);
    if (turn.request.isEmpty()) turn.request = request.trimmed();
}

void Ledger::add(const QString &turnId, const HiddenRow &row, bool replaceLast) {
    HiddenTurn &turn = turnFor(turnId);
    // Only the last row of the turn that is still being written can be rewritten: a rewrite that
    // arrives after another turn has started, or after a reasoning row, appends instead of eating
    // a row that is not the one the terminal would have had its cursor on.
    const bool rewritable = replaceLast && !turn.rows.isEmpty() && m_turns.last().turnId == turnId
                            && turn.rows.last().kind == HiddenRow::Kind::Call;
    if (rewritable) turn.rows.last() = row;
    else turn.rows.append(row);
}

QVector<HiddenTurn> Ledger::take(int *droppedTurns) {
    QVector<HiddenTurn> out;
    out.swap(m_turns);
    if (droppedTurns) *droppedTurns = m_dropped;
    m_dropped = 0;
    // Turns that were only ever a request line (the pane was opened between turns, say) carry no
    // rows and would reprint as a rule with nothing under it.
    for (int at = int(out.size()) - 1; at >= 0; --at)
        if (out.at(at).rows.isEmpty()) out.removeAt(at);
    return out;
}

void Ledger::clear() {
    m_turns.clear();
    m_dropped = 0;
}

int Ledger::rowCount() const {
    int rows = 0;
    for (const HiddenTurn &turn : m_turns) rows += int(turn.rows.size());
    return rows;
}

}  // namespace relay::internals
