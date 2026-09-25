// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PaneState.h"

#include <QJsonArray>

#include <algorithm>

namespace relay::panestate {

QString phase(const Inputs &in) {
    if (in.waiting) return QStringLiteral("waiting");
    if (!in.busy) return QStringLiteral("idle");
    return in.toolRunning ? QStringLiteral("tool") : QStringLiteral("thinking");
}

QString clip(const QString &text, int max, bool simplify) {
    const QString simple = simplify ? text.simplified() : text;
    if (max <= 0) return {};
    if (simple.size() <= max) return simple;
    return simple.left(max - 1) + QChar(0x2026);
}

// A queued row, which the up/down actions count, as against one inside the running turn: a steer,
// or a `/model` switch steered into it (card #7QH0: "waiting", "withdrawing", "interrupting").
bool isEntryRow(const Row &row) {
    if (row.kind == QLatin1String("steer")) return false;
    if (row.kind == QLatin1String("model"))
        return row.state == QLatin1String("queued") || row.state == QLatin1String("paused") || row.state == QLatin1String("editing");
    return true;
}

QString rowLabel(const Row &row) {
    if (row.kind == QLatin1String("model")) {
        QString label = isEntryRow(row) ? QString() : QStringLiteral("↪ next tool call  ");
        label += QStringLiteral("↻ ") + row.text.simplified();
        if (row.state == QLatin1String("withdrawing")) label += QStringLiteral("  withdrawing…");
        if (row.state == QLatin1String("interrupting")) label += QStringLiteral("  switching now…");
        return label;
    }
    const bool steer = row.kind == QLatin1String("steer");
    const bool agent = steer || row.kind == QLatin1String("agent");
    QString label = steer ? QStringLiteral("↪ next tool call  ") : QString();
    label += agent ? QStringLiteral("✦ ") : QStringLiteral("$ ");
    label += row.text.simplified();
    if (steer && row.state == QLatin1String("withdrawing")) label += QStringLiteral("  withdrawing…");
    return label;
}

QStringList rowActions(const Row &row, bool busy, int entryIndex, int entryCount) {
    QStringList actions;
    if (row.kind == QLatin1String("steer")) {
        // A steer being withdrawn is already on its way out, and one the desktop has in its prompt
        // box is the desktop user's to finish with.
        if (row.state != QLatin1String("waiting")) return actions;
        actions << QStringLiteral("remove") << QStringLiteral("edit") << QStringLiteral("to_queue");
        if (busy) actions << QStringLiteral("send_now");
        return actions;
    }
    if (row.kind == QLatin1String("model")) {
        // Card #7QH0: a `/model` walks the prompt's ladder — queued, then at the next tool call,
        // then now — and has no text to edit: picking another model replaces it.
        if (!isEntryRow(row)) {
            if (row.state != QLatin1String("waiting")) return actions;
            actions << QStringLiteral("remove");
            if (busy) actions << QStringLiteral("send_now");
            return actions;
        }
        if (row.state == QLatin1String("editing")) return actions;
        actions << QStringLiteral("remove");
        if (busy) actions << QStringLiteral("steer") << QStringLiteral("send_now");
        if (entryIndex > 0) actions << QStringLiteral("up");
        if (entryIndex >= 0 && entryIndex < entryCount - 1) actions << QStringLiteral("down");
        return actions;
    }
    if (row.kind != QLatin1String("agent") && row.kind != QLatin1String("command")) return actions;
    if (row.state == QLatin1String("editing")) return actions;   // being edited on the desktop
    actions << QStringLiteral("remove");
    if (!row.written) actions << QStringLiteral("edit");
    // Only an agent prompt the person wrote, while a turn runs, can be delivered inside it — the
    // same rule as Ctrl+↑ and as dropping a row above the steers (Pane::steerQueuedEntry).
    if (busy && row.kind == QLatin1String("agent") && !row.written) actions << QStringLiteral("steer");
    if (entryIndex > 0) actions << QStringLiteral("up");
    if (entryIndex >= 0 && entryIndex < entryCount - 1) actions << QStringLiteral("down");
    return actions;
}

QStringList actionsFor(const Inputs &in, const QString &rowId) {
    int entryCount = 0;
    for (const Row &row : in.rows) if (isEntryRow(row)) ++entryCount;
    int entryIndex = 0;
    for (const Row &row : in.rows) {
        const bool entry = isEntryRow(row);
        if (row.id == rowId) return rowActions(row, in.busy, entry ? entryIndex : -1, entryCount);
        if (entry) ++entryIndex;
    }
    return {};
}

QString queueHint(bool steerSelected, bool headSteerable, bool rowSelected) {
    if (steerSelected)
        return QStringLiteral("Enter or type to edit · Ctrl+↓ back to the queue · Ctrl+Enter now · Shift+Del withdraw");
    if (headSteerable) return QStringLiteral("Ctrl+↑ next tool call · Ctrl+↓ move · Enter save · ↓ leave · Shift+Del remove");
    if (rowSelected) return QStringLiteral("↑↓ row · Ctrl+↑↓ move · Enter save · ↓ leave · Shift+Del remove");
    return QStringLiteral("↑ select a row · Ctrl+↑↓ move · Shift+Del remove");
}

QString Tokens::idFor(const QString &key) {
    const auto found = m_ids.constFind(key);
    if (found != m_ids.constEnd()) return *found;
    const QString id = QString(m_prefix) + QString::number(++m_next);
    m_ids.insert(key, id);
    m_keys.insert(id, key);
    return id;
}

QJsonObject build(qint64 seq, const Inputs &in, Tokens &choiceTokens, Tokens &sessionTokens) {
    QJsonObject turn{{QStringLiteral("phase"), phase(in)},
                     {QStringLiteral("clock"), in.busy ? clip(in.clock) : QString()},
                     {QStringLiteral("busy"), in.busy}};

    QJsonObject thinking{{QStringLiteral("visible"), in.thinkingVisible},
                         {QStringLiteral("header"), clip(in.thinkingHeader)},
                         {QStringLiteral("tail"), in.thinkingText.right(kTailMax)}};

    QJsonArray rows;
    int entryCount = 0;
    for (const Row &row : in.rows) if (isEntryRow(row)) ++entryCount;
    int entryIndex = 0;
    for (const Row &row : in.rows) {
        const bool entry = isEntryRow(row);
        if (rows.size() < kRowsMax) {
            QJsonArray actions;
            for (const QString &action : rowActions(row, in.busy, entry ? entryIndex : -1, entryCount)) actions.append(action);
            rows.append(QJsonObject{{QStringLiteral("id"), row.id},
                                    {QStringLiteral("kind"), row.kind},
                                    {QStringLiteral("label"), clip(rowLabel(row), kLabelMax, false)},
                                    {QStringLiteral("state"), row.state},
                                    {QStringLiteral("actions"), actions}});
        }
        if (entry) ++entryIndex;
    }
    QJsonObject queue{{QStringLiteral("paused"), in.queuePaused},
                      {QStringLiteral("pause_reason"), in.queuePaused ? clip(in.pauseReason) : QString()},
                      {QStringLiteral("running"), in.running.trimmed().isEmpty()
                                                      ? QJsonValue(QJsonValue::Null)
                                                      : QJsonValue(QJsonObject{{QStringLiteral("label"), clip(in.running)}})},
                      {QStringLiteral("rows"), rows},
                      {QStringLiteral("hint"), rows.isEmpty() ? QString() : clip(in.queueHint)}};

    QJsonArray choices;
    for (const Choice &choice : in.choices) {
        if (choices.size() >= kChoicesMax) break;
        if (choice.key.isEmpty()) continue;
        choices.append(QJsonObject{{QStringLiteral("id"), choiceTokens.idFor(choice.key)},
                                   {QStringLiteral("label"), clip(choice.label)},
                                   {QStringLiteral("current"), choice.current}});
    }
    // The reasoning level and the levels this model takes (section 3): empty `efforts` says the
    // model has none, and a client draws no picker. Labels are the desktop's own words.
    // `effort_fixed` says the level is not the pane's to change — a model with no reasoning knob,
    // or Relay Free, where the gateway picks it for the role — with the reason the desktop's own
    // greyed box shows in its tooltip. The levels stay published either way: Relay Free has two
    // worth showing, and a client draws a chip it cannot pick from rather than no chip at all
    // (card #EFT9). A client that has never heard of the field draws the picker it drew before.
    QJsonArray efforts;
    for (const QString &level : in.efforts) {
        if (efforts.size() >= kEffortsMax) break;
        if (level.simplified().isEmpty()) continue;
        efforts.append(level);
    }
    QJsonObject model{{QStringLiteral("label"), clip(in.modelLabel)}, {QStringLiteral("choices"), choices},
                      {QStringLiteral("effort"), in.efforts.isEmpty() ? QJsonValue() : QJsonValue(in.effort)},
                      {QStringLiteral("efforts"), efforts},
                      {QStringLiteral("effort_fixed"), in.effortFixed},
                      {QStringLiteral("effort_fixed_reason"), in.effortFixed ? clip(in.effortFixedReason) : QString()}};

    QJsonObject composer{{QStringLiteral("mode"), in.mode},
                         {QStringLiteral("placeholder"), clip(in.placeholder)},
                         {QStringLiteral("modes"), QJsonArray{QStringLiteral("auto"), QStringLiteral("shell"), QStringLiteral("agent")}}};

    QJsonObject context{{QStringLiteral("label"), clip(in.contextLabel)},
                        {QStringLiteral("percent_left"), in.percentLeft < 0 ? QJsonValue(QJsonValue::Null)
                                                                            : QJsonValue(std::min(100, in.percentLeft))}};

    // The Relay Free chip, and only while the pane is on the hosted preset with a figure arrived:
    // no object at all otherwise, so a pane that switches to a provider with a key takes the chip
    // off every view. The label and detail are the desktop chip's own words (section 16's rule);
    // warn is read off the same percentage the desktop's chip warns by.
    QJsonObject allowance;
    if (!in.allowanceLabel.simplified().isEmpty()) {
        const int left = in.allowanceLeft < 0 ? -1 : std::min(100, in.allowanceLeft);
        allowance = QJsonObject{{QStringLiteral("label"), clip(in.allowanceLabel)},
                                {QStringLiteral("percent_left"), left < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(left)},
                                {QStringLiteral("warn"), left >= 0 && left <= 10},
                                {QStringLiteral("detail"), clip(in.allowanceDetail)}};
    }

    QJsonArray sessionRows;
    for (const Session &session : in.sessions) {
        if (sessionRows.size() >= kSessionsMax) break;
        if (session.key.isEmpty()) continue;
        sessionRows.append(QJsonObject{{QStringLiteral("id"), sessionTokens.idFor(session.key)},
                                       {QStringLiteral("title"), clip(session.title)},
                                       {QStringLiteral("when"), clip(session.when)},
                                       {QStringLiteral("current"), session.current},
                                       {QStringLiteral("running"), session.running}});
    }
    QJsonObject sessions{{QStringLiteral("rows"), sessionRows}, {QStringLiteral("can_new"), in.canNew},
                         {QStringLiteral("can_open"), in.canOpen}};

    QJsonObject out{{QStringLiteral("t"), QStringLiteral("pane_state")},
                    {QStringLiteral("v"), kVersion},
                    {QStringLiteral("pane"), in.pane},
                    {QStringLiteral("seq"), seq},
                    {QStringLiteral("turn"), turn},
                    {QStringLiteral("thinking"), thinking},
                    {QStringLiteral("queue"), queue},
                    {QStringLiteral("model"), model},
                    {QStringLiteral("composer"), composer},
                    {QStringLiteral("context"), context},
                    {QStringLiteral("sessions"), sessions}};
    if (!allowance.isEmpty()) out.insert(QStringLiteral("allowance"), allowance);
    // The theme the desktop is drawing itself in (owner, 2026-09-19: the phone's terminal follows
    // the desktop's theme). An id, never a file name or a path — the web view has a generated
    // block per shipped theme and keeps the one it is on for an id it does not know — and absent
    // rather than empty when there is none to name.
    if (!in.theme.trimmed().isEmpty()) out.insert(QStringLiteral("theme"), in.theme.trimmed());
    return out;
}

Publisher::Publisher(Gather gather, Sink sink, Wanted wanted, int intervalMs)
    : m_gather(std::move(gather)), m_sink(std::move(sink)), m_wanted(std::move(wanted)), m_interval(intervalMs) {
    m_timer.setSingleShot(true);
    QObject::connect(&m_timer, &QTimer::timeout, [this] { flush(false); });
}

void Publisher::changed() {
    if (!m_wanted || !m_wanted()) return;   // nobody listening: gather nothing, send nothing
    m_dirty = true;
    if (m_timer.isActive()) return;          // one is already due; it will carry this change too
    const qint64 since = m_last.isValid() ? m_last.elapsed() : m_interval;
    m_timer.start(int(std::max<qint64>(0, m_interval - since)));
}

QJsonObject Publisher::publishNow() {
    m_timer.stop();
    m_dirty = true;
    if (!m_wanted || !m_wanted()) { m_dirty = false; return {}; }
    flush(true);
    QJsonObject out = m_lastBody;
    out.insert(QStringLiteral("seq"), m_seq);
    return out;
}

void Publisher::flush(bool force) {
    if (!m_dirty) return;
    m_dirty = false;
    if (!m_gather || !m_sink || !m_wanted || !m_wanted()) return;
    QJsonObject body = build(0, m_gather(), m_choices, m_sessions);
    body.remove(QStringLiteral("seq"));
    if (!force && body == m_lastBody) return;   // nothing a view would draw differently
    m_lastBody = body;
    body.insert(QStringLiteral("seq"), ++m_seq);
    ++m_sent;
    m_last.restart();
    m_sink(body);
}

}   // namespace relay::panestate
