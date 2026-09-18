// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubagentsPanel.h"
#include "Theme.h"
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace relay {

namespace {
QString str(const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString(); }
// "T3 · write the docs" for a subagent working on todo T3 (card #QHR1).
QString withTodo(const QString &todoId, const QString &description) {
    return todoId.isEmpty() ? description : todoId + QStringLiteral(" · ") + description;
}

// Read at paint time so a theme switch recolours the list (issue 0JA7).
inline const QColor &kDone() { return theme::Success; }
inline const QColor &kFailed() { return theme::SyntaxUnknown; }
}  // namespace

SubagentModel::SubagentModel() {
    clock = [] {
        static QElapsedTimer timer;
        if (!timer.isValid()) timer.start();
        return timer.elapsed();
    };
}

SubagentRow *SubagentModel::find(const QString &id) {
    for (auto &row : m_rows) if (row.id == id) return &row;
    return nullptr;
}

const SubagentRow *SubagentModel::row(const QString &id) const {
    for (const auto &row : m_rows) if (row.id == id) return &row;
    return nullptr;
}

int SubagentModel::liveCount() const {
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const SubagentRow &r) { return r.live(); }));
}

bool SubagentModel::hasLiveForeground() const {
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [](const SubagentRow &r) { return r.live() && !r.background; });
}

// ----- the orchestrator waiting on its subagents (card #V7QD) ---------------------------------
QString SubagentModel::waitingLine(int liveSubagents, bool blockedOnWait, bool liveForeground, bool mainBusy, int phase) {
    if (liveSubagents <= 0 || !(blockedOnWait || liveForeground || !mainBusy)) return {};
    // ". . ." grown one character at a time and padded back out, so nothing beside it shifts.
    static const QString dots = QStringLiteral(". . .");
    const int shown = phase < 0 ? dots.size() : QList<int>{0, 1, 3, 5}.value(phase % 4);
    return QStringLiteral("waiting for %1 subagent%2 %3")
        .arg(liveSubagents)
        .arg(liveSubagents == 1 ? QString() : QStringLiteral("s"), dots.left(shown).leftJustified(dots.size()));
}

QStringList SubagentModel::waitingLines(int liveSubagents, bool blockedOnWait, bool liveForeground, bool mainBusy, int phase) {
    const QString full = waitingLine(liveSubagents, blockedOnWait, liveForeground, mainBusy, phase);
    if (full.isEmpty()) return {};
    static const QString dots = QStringLiteral(". . .");
    const int shown = phase < 0 ? dots.size() : QList<int>{0, 1, 3, 5}.value(phase % 4);
    const QString tail = dots.left(shown).leftJustified(dots.size());
    // A narrow pane drops the words rather than wrapping: "waiting for 3 . . ." then "3 . . .".
    return {full, QStringLiteral("waiting for %1 %2").arg(liveSubagents).arg(tail),
            QStringLiteral("%1 %2").arg(liveSubagents).arg(tail)};
}

qint64 SubagentModel::elapsedNow(const SubagentRow &row) const {
    if (!row.live() || row.reportedAt <= 0) return row.elapsedMs;
    return row.elapsedMs + std::max<qint64>(0, clock() - row.reportedAt);
}

qint64 SubagentModel::agentTokens() const {
    qint64 total = 0;
    for (const auto &row : m_rows) total += row.tokens;
    return total;
}

bool SubagentModel::agentTokensEstimated() const {
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [](const SubagentRow &r) { return r.tokensEstimated; });
}

void SubagentModel::dismiss(const QString &id) {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id == id && !m_rows[i].live()) { m_rows.removeAt(i); changed(); return; }
}

void SubagentModel::clearFinished() {
    const int before = m_rows.size();
    m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [](const SubagentRow &r) { return !r.live(); }), m_rows.end());
    if (m_rows.size() != before) changed();
    if (onFinishedCleared) onFinishedCleared();
}

void SubagentModel::clear() {
    const bool had = !m_rows.isEmpty();
    m_rows.clear(); m_mainBusy = false; m_mainTokens = -1;
    if (had) changed();
}

QString SubagentModel::tokenSplit() const {
    const QString main = m_mainTokens >= 0 ? QStringLiteral("main ctx %1").arg(formatTokens(m_mainTokens, false)) : QStringLiteral("main");
    return QStringLiteral("%1 · agents %2 tok").arg(main, formatTokens(agentTokens(), agentTokensEstimated()));
}

QString SubagentModel::formatElapsed(qint64 ms) {
    const qint64 seconds = std::max<qint64>(0, ms) / 1000;
    if (seconds >= 3600) return QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString SubagentModel::formatTokens(qint64 tokens, bool estimated) {
    QString text;
    if (tokens < 1000) text = QString::number(std::max<qint64>(0, tokens));
    else if (tokens < 100000) text = QString::number(tokens / 1000.0, 'f', 1) + QLatin1Char('k');
    else text = QString::number(tokens / 1000) + QLatin1Char('k');
    text.replace(QStringLiteral(".0k"), QStringLiteral("k"));
    return estimated ? QLatin1Char('~') + text : text;
}

QString SubagentModel::statusIcon(const QString &status) {
    if (status == QStringLiteral("running")) return QStringLiteral("●");
    if (status == QStringLiteral("done")) return QStringLiteral("✓");
    if (status == QStringLiteral("failed")) return QStringLiteral("✗");
    if (status == QStringLiteral("stopped")) return QStringLiteral("■");
    return QStringLiteral("○");   // waiting
}

QString SubagentModel::handoffText(const QString &handoff, int wakeups, int maxAutoTurns) {
    if (handoff == QStringLiteral("wake")) return QStringLiteral("background agent finished → main agent continues");
    if (handoff == QStringLiteral("next_model_call")) return QStringLiteral("result goes to the main agent's next step");
    if (handoff == QStringLiteral("pending"))
        return QStringLiteral("result pending: automatic-turn limit reached (%1/%2); it goes with your next prompt").arg(wakeups).arg(maxAutoTurns);
    if (handoff == QStringLiteral("returned")) return QStringLiteral("result returned to the main agent");
    if (handoff == QStringLiteral("discarded")) return QStringLiteral("result discarded (new conversation)");
    return {};
}

bool SubagentModel::handle(const QJsonObject &event) {
    const QString type = str(event, "event");
    const QString id = str(event, "id");
    const qint64 now = clock();
    auto metrics = [&](SubagentRow &row) {
        if (event.contains(QStringLiteral("tools"))) row.tools = event.value(QStringLiteral("tools")).toInt();
        if (event.contains(QStringLiteral("tokens"))) row.tokens = qint64(event.value(QStringLiteral("tokens")).toDouble());
        if (event.contains(QStringLiteral("tokens_estimated"))) row.tokensEstimated = event.value(QStringLiteral("tokens_estimated")).toBool();
        if (event.contains(QStringLiteral("elapsed_ms"))) { row.elapsedMs = qint64(event.value(QStringLiteral("elapsed_ms")).toDouble()); row.reportedAt = now; }
    };
    auto ensure = [&](const QString &rowId) -> SubagentRow & {
        if (SubagentRow *existing = find(rowId)) return *existing;
        SubagentRow row; row.id = rowId; row.reportedAt = now;
        m_rows.append(row);
        return m_rows.last();
    };
    if (type == QStringLiteral("subagent_started")) {
        if (id.isEmpty()) return true;
        SubagentRow &row = ensure(id);
        row.type = str(event, "type"); row.todoId = str(event, "todo_id");
        row.description = withTodo(row.todoId, str(event, "description"));
        row.background = event.value(QStringLiteral("background")).toBool();
        row.model = str(event, "model"); row.effort = str(event, "effort");
        row.resumed = event.value(QStringLiteral("resumed")).toBool();
        row.warnings.clear();
        for (const auto &w : event.value(QStringLiteral("warnings")).toArray()) row.warnings << w.toString();
        row.status = QStringLiteral("waiting"); row.summary.clear(); row.handoff.clear();
        row.lastActivity = row.resumed ? QStringLiteral("resuming") : QStringLiteral("starting");
        if (!row.resumed) { row.elapsedMs = 0; row.reportedAt = now; }
        if (onInline) {
            QString line = QStringLiteral("✦ %1 %2 %3 · %4").arg(row.type, row.id,
                row.resumed ? QStringLiteral("resumed") : row.background ? QStringLiteral("started in the background") : QStringLiteral("started"),
                row.description);
            if (!row.warnings.isEmpty()) line += QStringLiteral(" (") + row.warnings.join(QStringLiteral("; ")) + QLatin1Char(')');
            onInline(line, row.id);
        }
        changed();
        return true;
    }
    if (type == QStringLiteral("subagent_progress")) {
        if (id.isEmpty()) return true;
        SubagentRow &row = ensure(id);
        const QString status = str(event, "status");
        if (!status.isEmpty()) row.status = status;
        if (event.contains(QStringLiteral("last_activity"))) row.lastActivity = str(event, "last_activity");
        metrics(row);
        changed();
        if (onTranscript) onTranscript(id, event);
        return true;
    }
    if (type == QStringLiteral("subagent_finished")) {
        if (id.isEmpty()) return true;
        SubagentRow &row = ensure(id);
        if (row.type.isEmpty()) row.type = str(event, "type");
        row.status = str(event, "outcome").isEmpty() ? QStringLiteral("done") : str(event, "outcome");
        row.summary = str(event, "summary"); row.handoff = str(event, "handoff");
        row.lastActivity = row.status;
        metrics(row);
        const SubagentRow copy = row;
        if (onInline) {
            QString line = QStringLiteral("✦ %1 %2 %3 · %4 · %5 tool%6 · %7 tok").arg(copy.type, copy.id, copy.status,
                formatElapsed(copy.elapsedMs)).arg(copy.tools).arg(copy.tools == 1 ? QString() : QStringLiteral("s"))
                .arg(formatTokens(copy.tokens, copy.tokensEstimated));
            const QString handoff = handoffText(copy.handoff, event.value(QStringLiteral("wakeups")).toInt(), event.value(QStringLiteral("max_auto_turns")).toInt());
            if (!handoff.isEmpty()) line += QStringLiteral(" · ") + handoff;
            onInline(line, copy.id);
        }
        changed();
        if (onTranscript) onTranscript(id, event);
        if (onFinished) onFinished(copy);
        return true;
    }
    if (type == QStringLiteral("subagent_handoff")) {
        const QString handoff = str(event, "handoff");
        if (SubagentRow *row = find(id)) row->handoff = handoff;
        if (onInline && (handoff == QStringLiteral("wake") || handoff == QStringLiteral("pending"))) {
            onInline(handoff == QStringLiteral("wake")
                ? QStringLiteral("✦ %1 result → main agent continues").arg(id)
                : QStringLiteral("✦ %1 %2").arg(id, handoffText(handoff, event.value(QStringLiteral("wakeups")).toInt(), event.value(QStringLiteral("max_auto_turns")).toInt())),
                id);
        }
        changed();
        return true;
    }
    if (type == QStringLiteral("subagent_model")) {
        SubagentRow *row = find(id);
        if (!row) return true;
        row->model = str(event, "model");
        QStringList warnings;
        for (const auto &w : event.value(QStringLiteral("warnings")).toArray()) warnings << w.toString();
        if (onStatus) onStatus(QStringLiteral("%1 → %2%3%4").arg(id, row->model,
            str(event, "applies") == QStringLiteral("next_step") ? QStringLiteral(" from its next step") : QString(),
            warnings.isEmpty() ? QString() : QStringLiteral(" (") + warnings.join(QStringLiteral("; ")) + QLatin1Char(')')));
        changed();
        return true;
    }
    if (type == QStringLiteral("subagent_transcript") || type == QStringLiteral("subagent_event")) {
        if (onTranscript) onTranscript(id, event);
        return true;
    }
    if (type == QStringLiteral("agents_status")) {
        for (const auto &value : event.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            SubagentRow &row = ensure(str(item, "id"));
            row.type = str(item, "type"); row.todoId = str(item, "todo_id");
            row.description = withTodo(row.todoId, str(item, "description"));
            row.background = item.value(QStringLiteral("background")).toBool(); row.model = str(item, "model");
            row.status = str(item, "status"); row.lastActivity = str(item, "last_activity");
            row.tools = item.value(QStringLiteral("tools")).toInt(); row.tokens = qint64(item.value(QStringLiteral("tokens")).toDouble());
            row.elapsedMs = qint64(item.value(QStringLiteral("elapsed_ms")).toDouble()); row.reportedAt = now;
        }
        changed();
        return true;
    }
    if (type == QStringLiteral("agents")) {
        m_definitions = event.value(QStringLiteral("items")).toArray();
        m_definitionWarnings.clear();
        for (const auto &value : event.value(QStringLiteral("skipped")).toArray())
            m_definitionWarnings << QStringLiteral("%1: %2").arg(str(value.toObject(), "path"), str(value.toObject(), "reason"));
        for (const auto &value : event.value(QStringLiteral("duplicates")).toArray())
            m_definitionWarnings << QStringLiteral("%1: duplicate name %2").arg(str(value.toObject(), "source"), str(value.toObject(), "name"));
        return true;
    }
    if (type == QStringLiteral("agent_stopped")) {
        const QJsonArray ids = event.value(QStringLiteral("ids")).toArray();
        if (onStatus) onStatus(ids.isEmpty() ? QStringLiteral("No running agents to stop.")
                                             : QStringLiteral("Stopping %1 agent(s)…").arg(ids.size()));
        return true;
    }
    if (type == QStringLiteral("agent_message_delivered")) {
        if (onStatus) onStatus(str(event, "delivered") == QStringLiteral("resumed")
                                   ? QStringLiteral("Message sent · %1 resumed in the background").arg(id)
                                   : QStringLiteral("Message sent · %1 reads it before its next step").arg(id));
        return true;
    }
    if (type == QStringLiteral("agent_options")) {
        if (onStatus) onStatus(QStringLiteral("Automatic agent turns: %1 used of %2").arg(event.value(QStringLiteral("wakeups")).toInt())
                                   .arg(event.value(QStringLiteral("max_auto_turns")).toInt() == 0 ? QStringLiteral("unlimited")
                                        : QString::number(event.value(QStringLiteral("max_auto_turns")).toInt()))
                                   // Turn limits and request audit (protocol 12.1), present once an agent is configured.
                                   + (event.contains(QStringLiteral("max_steps"))
                                      ? QStringLiteral(" · %1 steps / %2 tool calls per turn · request audit %3").arg(event.value(QStringLiteral("max_steps")).toInt())
                                            .arg(event.value(QStringLiteral("max_tool_calls")).toInt())
                                            .arg(event.value(QStringLiteral("audit_requests")).toBool() ? QStringLiteral("on") : QStringLiteral("off"))
                                      : QString()));
        return true;
    }
    // Observed, not consumed.
    if (type == QStringLiteral("agent_started")) { m_mainBusy = true; changed(); }
    else if (type == QStringLiteral("agent_finished")) { m_mainBusy = false; changed(); }
    else if (type == QStringLiteral("context")) { m_mainTokens = qint64(event.value(QStringLiteral("used_tokens")).toDouble()); changed(); }
    else if (type == QStringLiteral("reset")) clearFinished();
    else if (type == QStringLiteral("ready")) clear();
    return false;
}

// ----- panel ---------------------------------------------------------------------------------

SubagentsPanel::SubagentsPanel(SubagentModel *model, QWidget *parent) : QWidget(parent), m_model(model) {
    setObjectName(QStringLiteral("subagentsPanel"));
    setFocusPolicy(Qt::ClickFocus);
    setAccessibleName(QStringLiteral("Running agents"));
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setMouseTracking(false);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
    hide();
}

int SubagentsPanel::rowHeight() const { return fontMetrics().height() + 6; }

int SubagentsPanel::visibleCount() const { return std::min<int>(kMaxVisible, m_model->rows().size()); }

int SubagentsPanel::firstVisible() const {
    const int n = m_model->rows().size();
    if (n <= kMaxVisible) return 0;
    const int index = std::max(0, m_selected - 1);
    return std::clamp(index - kMaxVisible + 1, 0, n - kMaxVisible);
}

QSize SubagentsPanel::sizeHint() const {
    if (m_folded) return {200, rowHeight() + 6};
    const int n = m_model->rows().size();
    const int lines = 1 + visibleCount() + (n > kMaxVisible ? 1 : 0);
    return {200, lines * rowHeight() + 6};
}

QString SubagentsPanel::selectedId() const {
    const int index = m_selected - 1;
    return index >= 0 && index < m_model->rows().size() ? m_model->rows().at(index).id : QString();
}

void SubagentsPanel::refresh() {
    const bool show = m_allowed && !m_model->isEmpty();
    m_selected = std::clamp(m_selected, 0, int(m_model->rows().size()));
    if (show != isVisible()) {
        if (!show && hasFocus() && onExit) onExit();
        setVisible(show);
    }
    if (show && m_model->liveCount() > 0) { if (!m_tick.isActive()) m_tick.start(); }
    else m_tick.stop();
    updateGeometry();
    update();
}

void SubagentsPanel::setFolded(bool folded, const QString &keys) {
    if (m_folded == folded && m_foldKeys == keys) return;
    m_folded = folded; m_foldKeys = keys;
    refresh();
}

QString SubagentsPanel::foldedText() const {
    const int n = int(m_model->rows().size());
    const int live = m_model->liveCount();
    QString text = live > 0 ? QStringLiteral("%1 subagent%2 running").arg(live).arg(live == 1 ? QString() : QStringLiteral("s"))
                            : QStringLiteral("%1 subagent%2 finished").arg(n).arg(n == 1 ? QString() : QStringLiteral("s"));
    if (live > 0 && n > live) text += QStringLiteral(" · %1 finished").arg(n - live);
    return text + QStringLiteral(" · %1 to open").arg(m_foldKeys.isEmpty() ? QStringLiteral("Enter") : m_foldKeys);
}

void SubagentsPanel::enter() {
    if (m_model->isEmpty()) return;
    m_selected = 1;   // always start at the first subagent
    setFocus(Qt::TabFocusReason);
    update();
}

int SubagentsPanel::rowAt(const QPoint &pos) const {
    const int line = (pos.y() - 2) / rowHeight();
    if (line <= 0) return 0;
    if (line > visibleCount()) return -1;
    return firstVisible() + line;
}

void SubagentsPanel::act(bool stop) {
    const QString id = selectedId();
    const SubagentRow *row = m_model->row(id);
    if (!row) return;
    if (row->live()) { if (stop && onStop) onStop(id); }
    else m_model->dismiss(id);
    refresh();
}

void SubagentsPanel::pickModel(int row) {
    if (row <= 0 || !onPickModel) return;
    m_selected = row;
    const QString id = selectedId();
    if (id.isEmpty()) return;
    QRect chip = m_modelChips.value(row);
    if (chip.isNull()) chip = QRect(0, 2 + (row - firstVisible()) * rowHeight(), width(), rowHeight());
    update();
    onPickModel(id, mapToGlobal(chip.bottomLeft()));
}

void SubagentsPanel::keyPressEvent(QKeyEvent *event) {
    const auto mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const int n = m_model->rows().size();
    if (mods != Qt::NoModifier) { QWidget::keyPressEvent(event); return; }
    if (m_folded) {
        // One line: Enter goes to the open subagent pane, Down to the list beneath, Up/Esc back.
        switch (event->key()) {
        case Qt::Key_Return: case Qt::Key_Enter: if (onOpenPane) onOpenPane(); return;
        case Qt::Key_Down: if (onBelow) onBelow(); return;
        case Qt::Key_Up: case Qt::Key_Escape: if (onExit) onExit(); return;
        default: QWidget::keyPressEvent(event); return;
        }
    }
    switch (event->key()) {
    case Qt::Key_Up:
        if (m_selected <= 0) { if (onExit) onExit(); }
        else { --m_selected; update(); }
        return;
    case Qt::Key_Down:
        if (m_selected < n) { ++m_selected; update(); }
        else if (onBelow) onBelow();
        return;
    case Qt::Key_Home: m_selected = 0; update(); return;
    case Qt::Key_End: m_selected = n; update(); return;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (m_selected == 0) { if (onExit) onExit(); }
        else if (onOpen) onOpen(selectedId());
        return;
    case Qt::Key_X: case Qt::Key_Delete: case Qt::Key_Backspace:
        act(true);
        return;
    case Qt::Key_M:
        pickModel(m_selected);
        return;
    case Qt::Key_Escape:
        if (onExit) onExit();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void SubagentsPanel::mousePressEvent(QMouseEvent *event) {
    if (m_folded) {
        if (event->button() == Qt::LeftButton && onOpenPane) { onOpenPane(); if (onMouseOpen) onMouseOpen(); }
        return;
    }
    const int row = rowAt(event->pos());
    if (row < 0) return;
    m_selected = row;
    setFocus(Qt::MouseFocusReason);
    // The × at the right edge stops or dismisses; the model chip opens the model picker; anywhere
    // else on a subagent row opens its tab in the subagent pane (owner, 2026-09-18: a click).
    if (row > 0 && event->pos().x() >= width() - 24) act(true);
    else if (row > 0 && m_modelChips.value(row).contains(event->pos())) pickModel(row);
    else if (row > 0 && event->button() == Qt::LeftButton && onOpen) {
        const QString id = selectedId();
        update();
        onOpen(id);
        if (onMouseOpen) onMouseOpen();
        return;
    }
    update();
}

// A second click of a double-click lands on a row the first click already opened.
void SubagentsPanel::mouseDoubleClickEvent(QMouseEvent *) {}

void SubagentsPanel::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); update(); }
void SubagentsPanel::focusOutEvent(QFocusEvent *event) { QWidget::focusOutEvent(event); update(); }

void SubagentsPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QFontMetrics fm = fontMetrics();
    const int h = rowHeight();
    const bool focused = hasFocus();
    // The list sits beneath the prompt box and draws its own card.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(theme::Border);
    p.setBrush(theme::Surface);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, false);
    if (m_folded) {
        const QRect r(0, 2, width(), h);
        if (focused) p.fillRect(r.adjusted(4, 0, -4, 0), theme::SurfaceRaised.lighter(135));
        const bool live = m_model->liveCount() > 0;
        p.setPen(live ? theme::Accent : theme::TextMuted);
        p.drawText(QRect(10, r.top(), 16, h), Qt::AlignCenter, live ? QStringLiteral("●") : QStringLiteral("✓"));
        p.setPen(theme::Text);
        p.drawText(QRect(30, r.top(), width() - 40, h), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(foldedText(), Qt::ElideRight, width() - 40));
        return;
    }
    const int nameWidth = std::max(fm.horizontalAdvance(QStringLiteral("general a00")) + 8, 96);

    m_modelChips.clear();
    auto drawRow = [&](int line, bool selected, const QString &icon, const QColor &iconColor, const QString &name,
                       const QString &description, const QString &metrics, const QString &badge, bool closable,
                       const QString &model = QString(), int rowIndex = 0) {
        const QRect r(0, 2 + line * h, width(), h);
        if (selected) p.fillRect(r.adjusted(4, 0, -4, 0), focused ? theme::SurfaceRaised.lighter(135) : theme::SurfaceRaised);
        int x = 10;
        p.setPen(iconColor);
        p.drawText(QRect(x, r.top(), 16, h), Qt::AlignCenter, icon);
        x += 20;
        p.setPen(theme::Text);
        QFont bold = font(); bold.setBold(true); p.setFont(bold);
        p.drawText(QRect(x, r.top(), nameWidth, h), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(name, Qt::ElideRight, nameWidth - 4));
        p.setFont(font());
        x += nameWidth;
        int right = width() - (closable ? 28 : 10);
        if (!badge.isEmpty()) {
            const int bw = fm.horizontalAdvance(badge) + 10;
            const QRect br(right - bw, r.top() + 3, bw, h - 6);
            p.setPen(theme::Border); p.setBrush(theme::SurfaceRaised);
            p.drawRoundedRect(br, 4, 4);
            p.setPen(theme::TextMuted);
            p.drawText(br, Qt::AlignCenter, badge);
            p.setBrush(Qt::NoBrush);
            right -= bw + 8;
        }
        if (rowIndex > 0) {
            // The model chip: click it, or press m on the row, to pick another model.
            const QString label = fm.elidedText(model.isEmpty() ? QStringLiteral("model") : model, Qt::ElideMiddle, 150)
                                  + QStringLiteral(" ▾");
            const int cw = fm.horizontalAdvance(label) + 12;
            const QRect cr(right - cw, r.top() + 3, cw, h - 6);
            p.setPen(theme::Border); p.setBrush(theme::SurfaceRaised);
            p.drawRoundedRect(cr, 4, 4);
            p.setPen(theme::Text);
            p.drawText(cr, Qt::AlignCenter, label);
            p.setBrush(Qt::NoBrush);
            m_modelChips.insert(rowIndex, cr);
            right -= cw + 8;
        }
        const int mw = std::min(fm.horizontalAdvance(metrics) + 8, std::max(0, (right - x) / 2));
        p.setPen(theme::TextMuted);
        p.drawText(QRect(right - mw, r.top(), mw, h), Qt::AlignVCenter | Qt::AlignRight, fm.elidedText(metrics, Qt::ElideLeft, mw));
        p.setPen(theme::Text);
        const int dw = std::max(0, right - mw - 12 - x);
        p.drawText(QRect(x, r.top(), dw, h), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(description, Qt::ElideRight, dw));
        if (closable) {
            p.setPen(theme::TextMuted);
            p.drawText(QRect(width() - 24, r.top(), 16, h), Qt::AlignCenter, QStringLiteral("×"));
        }
    };

    const QString hint = focused ? QStringLiteral("↑↓ select · Enter open tab · m model · x stop/dismiss · Esc back")
                                 : QStringLiteral("↓ from the prompt to select");
    drawRow(0, focused && m_selected == 0, m_model->mainBusy() ? QStringLiteral("●") : QStringLiteral("○"),
            m_model->mainBusy() ? theme::Accent : theme::TextMuted, QStringLiteral("main"),
            (m_model->mainBusy() ? QStringLiteral("working") : QStringLiteral("idle")) + QStringLiteral("   ") + hint,
            m_model->tokenSplit(), QString(), false);
    const auto &rows = m_model->rows();
    const int first = firstVisible();
    for (int i = 0; i < visibleCount(); ++i) {
        const SubagentRow &row = rows.at(first + i);
        QColor color = theme::TextMuted;
        if (row.status == QStringLiteral("running")) color = theme::Accent;
        else if (row.status == QStringLiteral("done")) color = kDone();
        else if (row.status == QStringLiteral("failed")) color = kFailed();
        QStringList parts;
        if (!row.live()) parts << row.status;
        else if (row.status == QStringLiteral("waiting")) parts << QStringLiteral("waiting");
        parts << SubagentModel::formatElapsed(m_model->elapsedNow(row));
        parts << QStringLiteral("%1 tool%2").arg(row.tools).arg(row.tools == 1 ? QString() : QStringLiteral("s"));
        parts << SubagentModel::formatTokens(row.tokens, row.tokensEstimated) + QStringLiteral(" tok");
        QString description = row.description;
        if (row.live() && !row.lastActivity.isEmpty() && row.lastActivity != QStringLiteral("starting"))
            description += QStringLiteral("  ·  ") + row.lastActivity;
        drawRow(i + 1, m_selected == first + i + 1 && focused, SubagentModel::statusIcon(row.status), color,
                QStringLiteral("%1 %2").arg(row.type, row.id), description, parts.join(QStringLiteral(" · ")),
                row.background ? QStringLiteral("bg") : QString(), true, row.model, first + i + 1);
    }
    if (rows.size() > kMaxVisible) {
        const int hidden = rows.size() - kMaxVisible;
        p.setPen(theme::TextMuted);
        p.drawText(QRect(30, 2 + (visibleCount() + 1) * h, width() - 40, h), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("+%1 more · ↑↓ scroll · Agents… in the palette").arg(hidden));
    }
}

}  // namespace relay
