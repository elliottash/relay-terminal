// SPDX-License-Identifier: AGPL-3.0-or-later
#include "JobsPanel.h"
#include "SubagentsPanel.h"   // SubagentModel::formatElapsed: one clock format under the prompt box
#include "Theme.h"
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace relay {

QString JobRow::state(qint64 elapsedNow) const {
    const QString clock = SubagentModel::formatElapsed(elapsedNow);
    if (running) return QStringLiteral("running · ") + clock;
    if (stopped) return QStringLiteral("stopped · ") + clock;
    return QStringLiteral("exit %1 · %2").arg(hasExitCode ? QString::number(exitCode) : QStringLiteral("?"), clock);
}

JobsModel::JobsModel() {
    clock = [] {
        static QElapsedTimer timer;
        if (!timer.isValid()) timer.start();
        return timer.elapsed();
    };
}

const JobRow *JobsModel::row(const QString &id) const {
    for (const auto &row : m_rows) if (row.id == id) return &row;
    return nullptr;
}

int JobsModel::runningCount() const {
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const JobRow &r) { return r.running; }));
}

qint64 JobsModel::elapsedNow(const JobRow &row) const {
    return row.running ? row.elapsedMs + std::max<qint64>(0, clock() - row.reportedAt) : row.elapsedMs;
}

bool JobsModel::handle(const QJsonObject &event) {
    const QString type = event.value(QStringLiteral("type")).toString(event.value(QStringLiteral("event")).toString());
    if (type == QStringLiteral("jobs")) {
        const qint64 now = clock();
        QList<JobRow> rows;
        for (const auto &value : event.value(QStringLiteral("jobs")).toArray()) {
            const QJsonObject item = value.toObject();
            JobRow row;
            row.id = item.value(QStringLiteral("job_id")).toString();
            if (row.id.isEmpty() || m_dismissed.contains(row.id)) continue;
            row.command = item.value(QStringLiteral("command")).toString().simplified();
            row.running = item.value(QStringLiteral("running")).toBool();
            row.stopped = item.value(QStringLiteral("stopped")).toBool();
            row.hasExitCode = item.value(QStringLiteral("exit_code")).isDouble();
            row.exitCode = item.value(QStringLiteral("exit_code")).toInt();
            row.elapsedMs = qint64(item.value(QStringLiteral("elapsed_ms")).toDouble());
            row.reportedAt = now;
            const JobRow *before = this->row(row.id);
            if (before && before->running && !row.running && !row.stopped && onFinished) onFinished(row);
            rows << row;
        }
        m_rows = rows;
        if (onChanged) onChanged();
        return true;
    }
    if (type == QStringLiteral("reset") || type == QStringLiteral("ready")) clear();
    return false;
}

void JobsModel::dismiss(const QString &id) {
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id != id || m_rows.at(i).running) continue;
        m_dismissed.insert(id);
        m_rows.removeAt(i);
        if (onChanged) onChanged();
        return;
    }
}

void JobsModel::clearFinished() {
    const auto before = m_rows.size();
    for (int i = m_rows.size() - 1; i >= 0; --i)
        if (!m_rows.at(i).running) { m_dismissed.insert(m_rows.at(i).id); m_rows.removeAt(i); }
    if (m_rows.size() != before && onChanged) onChanged();
}

void JobsModel::clear() {
    const bool had = !m_rows.isEmpty();
    m_rows.clear();
    m_dismissed.clear();
    if (had && onChanged) onChanged();
}

// ----- panel ---------------------------------------------------------------------------------

JobsPanel::JobsPanel(JobsModel *model, QWidget *parent) : QWidget(parent), m_model(model) {
    setObjectName(QStringLiteral("jobsPanel"));
    setFocusPolicy(Qt::ClickFocus);
    setAccessibleName(QStringLiteral("Commands the agent left running"));
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
    hide();
}

int JobsPanel::rowHeight() const { return fontMetrics().height() + 6; }

int JobsPanel::visibleCount() const { return std::min<int>(kMaxVisible, m_model->rows().size()); }

int JobsPanel::firstVisible() const {
    const int n = m_model->rows().size();
    if (n <= kMaxVisible) return 0;
    return std::clamp(m_selected - kMaxVisible + 1, 0, n - kMaxVisible);
}

QSize JobsPanel::sizeHint() const {
    const int n = m_model->rows().size();
    const int lines = 1 + visibleCount() + (n > kMaxVisible ? 1 : 0);
    return {200, lines * rowHeight() + 6};
}

QString JobsPanel::selectedId() const {
    return m_selected >= 0 && m_selected < m_model->rows().size() ? m_model->rows().at(m_selected).id : QString();
}

void JobsPanel::refresh() {
    const bool show = m_allowed && !m_model->isEmpty();
    m_selected = std::clamp(m_selected, 0, std::max(0, int(m_model->rows().size()) - 1));
    if (show != isVisible()) {
        if (!show && hasFocus() && onExit) onExit();
        setVisible(show);
    }
    if (show && m_model->runningCount() > 0) { if (!m_tick.isActive()) m_tick.start(); }
    else m_tick.stop();
    updateGeometry();
    update();
}

void JobsPanel::enter() {
    if (m_model->isEmpty()) return;
    m_selected = 0;
    setFocus(Qt::TabFocusReason);
    update();
}

int JobsPanel::rowAt(const QPoint &pos) const {
    const int line = (pos.y() - 2) / rowHeight();
    if (line <= 0 || line > visibleCount()) return -1;
    return firstVisible() + line - 1;
}

void JobsPanel::act(bool mouse) {
    const QString id = selectedId();
    const JobRow *row = m_model->row(id);
    if (!row) return;
    if (row->running) { if (onStop) onStop(id, mouse); }
    else m_model->dismiss(id);
    refresh();
}

void JobsPanel::keyPressEvent(QKeyEvent *event) {
    const auto mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const int n = m_model->rows().size();
    if (mods != Qt::NoModifier) { QWidget::keyPressEvent(event); return; }
    switch (event->key()) {
    case Qt::Key_Up:
        if (m_selected <= 0) { if (onExitUp) onExitUp(); }
        else { --m_selected; update(); }
        return;
    case Qt::Key_Down:
        if (m_selected < n - 1) { ++m_selected; update(); }
        return;
    case Qt::Key_Home: m_selected = 0; update(); return;
    case Qt::Key_End: m_selected = std::max(0, n - 1); update(); return;
    case Qt::Key_Return: case Qt::Key_Enter:
        if (onOpen && !selectedId().isEmpty()) onOpen(selectedId());
        return;
    case Qt::Key_X: case Qt::Key_Delete: case Qt::Key_Backspace:
        act(false);
        return;
    case Qt::Key_Escape:
        if (onExit) onExit();
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void JobsPanel::mousePressEvent(QMouseEvent *event) {
    const int row = rowAt(event->pos());
    if (row < 0) return;
    m_selected = row;
    setFocus(Qt::MouseFocusReason);
    if (event->pos().x() >= width() - 24) act(true);   // the × stops or dismisses
    update();
}

void JobsPanel::mouseDoubleClickEvent(QMouseEvent *event) {
    const int row = rowAt(event->pos());
    if (row >= 0 && event->pos().x() < width() - 24 && onOpen) { m_selected = row; onOpen(selectedId()); }
}

void JobsPanel::focusInEvent(QFocusEvent *event) { QWidget::focusInEvent(event); update(); }
void JobsPanel::focusOutEvent(QFocusEvent *event) { QWidget::focusOutEvent(event); update(); }

void JobsPanel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const QFontMetrics fm = fontMetrics();
    const int h = rowHeight();
    const bool focused = hasFocus();
    // Drawn like the running-agents list above it: its own card beneath the prompt box.
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(theme::Border);
    p.setBrush(theme::Surface);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int running = m_model->runningCount();
    const QString title = running > 0
        ? QStringLiteral("%1 command%2 running").arg(running).arg(running == 1 ? QString() : QStringLiteral("s"))
        : QStringLiteral("Commands");
    const QString keys = focused ? QStringLiteral("↑↓ select · Enter output · x stop/dismiss · Esc back")
                                 : QStringLiteral("↓ from the prompt to select");
    p.setPen(theme::TextMuted);
    p.drawText(QRect(10, 2, width() - 20, h), Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(title + QStringLiteral("   ") + keys, Qt::ElideRight, width() - 20));

    const int nameWidth = std::max(fm.horizontalAdvance(QStringLiteral("job-00")) + 12, 60);
    const auto &rows = m_model->rows();
    const int first = firstVisible();
    for (int i = 0; i < visibleCount(); ++i) {
        const JobRow &row = rows.at(first + i);
        const QRect r(0, 2 + (i + 1) * h, width(), h);
        if (focused && m_selected == first + i) p.fillRect(r.adjusted(4, 0, -4, 0), theme::SurfaceRaised.lighter(135));
        int x = 10;
        const QColor color = row.running ? theme::Accent
                             : (!row.stopped && row.hasExitCode && row.exitCode == 0) ? theme::Success : theme::TextMuted;
        p.setPen(color);
        p.drawText(QRect(x, r.top(), 16, h), Qt::AlignCenter, row.running ? QStringLiteral("●") : QStringLiteral("○"));
        x += 20;
        QFont bold = font(); bold.setBold(true); p.setFont(bold);
        p.setPen(theme::Text);
        p.drawText(QRect(x, r.top(), nameWidth, h), Qt::AlignVCenter | Qt::AlignLeft, row.id);
        p.setFont(font());
        x += nameWidth;
        const int right = width() - 28;
        const QString state = row.state(m_model->elapsedNow(row));
        const int sw = std::min(fm.horizontalAdvance(state) + 8, std::max(0, (right - x) / 2));
        p.setPen(theme::TextMuted);
        p.drawText(QRect(right - sw, r.top(), sw, h), Qt::AlignVCenter | Qt::AlignRight, fm.elidedText(state, Qt::ElideLeft, sw));
        p.setPen(theme::Text);
        const int cw = std::max(0, right - sw - 12 - x);
        p.drawText(QRect(x, r.top(), cw, h), Qt::AlignVCenter | Qt::AlignLeft,
                   fm.elidedText(QStringLiteral("$ ") + row.command, Qt::ElideRight, cw));
        p.setPen(theme::TextMuted);
        p.drawText(QRect(width() - 24, r.top(), 16, h), Qt::AlignCenter, QStringLiteral("×"));
    }
    if (rows.size() > kMaxVisible) {
        p.setPen(theme::TextMuted);
        p.drawText(QRect(30, 2 + (visibleCount() + 1) * h, width() - 40, h), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("+%1 more · ↑↓ scroll").arg(rows.size() - kMaxVisible));
    }
}

}  // namespace relay
