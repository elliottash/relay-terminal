// SPDX-License-Identifier: GPL-3.0-or-later
#include "RequestsPanel.h"
#include "Theme.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

namespace relay {

namespace {
constexpr int kIdRole = Qt::UserRole;          // todo id on task rows
constexpr int kGroupRole = Qt::UserRole + 1;   // "#earlier" on the group row
const QColor kDone{126, 200, 140};
const QColor kWarn{229, 192, 123};
const QColor kFailed{224, 108, 117};
const QColor kCancelled{150, 150, 160};

// Model text is untrusted; drop control characters except newline and tab.
QString clean(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if (u == '\n' || u == '\t' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) out += c;
    }
    return out;
}

QColor statusColor(const QString &status) {
    if (status == QStringLiteral("done") || status == QStringLiteral("completed")) return kDone;
    if (status == QStringLiteral("in_progress")) return theme::Accent;
    if (status == QStringLiteral("blocked")) return kFailed;
    if (status == QStringLiteral("deferred")) return kWarn;
    if (status.startsWith(QStringLiteral("cancelled"))) return kCancelled;
    return theme::Text;
}

QString rowText(const LedgerTodo &todo) {
    QString line = QStringLiteral("%1  %2  %3").arg(RequestLedgerModel::todoGlyph(todo.status), todo.id, clean(todo.text).simplified());
    if (!todo.note.isEmpty()) line += QStringLiteral(" — ") + clean(todo.note).simplified();
    return line;
}
}  // namespace

RequestsPanel::RequestsPanel(RequestLedgerModel *model, QWidget *parent) : QWidget(parent), m_model(model) {
    setObjectName(QStringLiteral("requestsPanel"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral("QWidget#requestsPanel { background: %1; border: 1px solid %2; border-radius: 8px; }")
                      .arg(theme::Background.name(), theme::Border.name()));
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(10, 8, 8, 8); layout->setSpacing(6);
    auto *header = new QHBoxLayout;
    m_title = new QLabel; m_title->setTextFormat(Qt::PlainText);
    QFont bold = m_title->font(); bold.setBold(true); m_title->setFont(bold);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(m_title, 1);
    auto *close = new QToolButton; close->setText(QStringLiteral("×")); close->setAutoRaise(true); close->setFocusPolicy(Qt::NoFocus);
    close->setToolTip(QStringLiteral("Close (Esc)"));
    connect(close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
    header->addWidget(close);
    layout->addLayout(header);
    m_keys = new QLabel(QStringLiteral("↑↓ select · Enter fold Earlier · Esc close"));
    QPalette muted = m_keys->palette(); muted.setColor(QPalette::WindowText, theme::TextMuted); m_keys->setPalette(muted);
    m_keys->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_keys);
    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("requestsList"));
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setTextElideMode(Qt::ElideRight);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->installEventFilter(this);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { if (!m_rebuilding) updateDetail(); });
    layout->addWidget(m_tree, 3);
    m_detail = new QLabel;
    m_detail->setObjectName(QStringLiteral("requestDetail"));
    m_detail->setTextFormat(Qt::PlainText);
    m_detail->setWordWrap(true);
    m_detail->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_detail);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll, 2);
    refresh();
}

namespace {
void forEachRow(QTreeWidgetItem *item, const std::function<void(QTreeWidgetItem *)> &fn) {
    fn(item);
    for (int i = 0; i < item->childCount(); ++i) forEachRow(item->child(i), fn);
}
}  // namespace

QString RequestsPanel::selectedId() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

QTreeWidgetItem *RequestsPanel::rowFor(const QString &todoId) const {
    QTreeWidgetItem *found = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount() && !found; ++i)
        forEachRow(m_tree->topLevelItem(i), [&](QTreeWidgetItem *row) { if (!found && row->data(0, kIdRole).toString() == todoId) found = row; });
    return found;
}

void RequestsPanel::select(const QString &todoId) {
    QTreeWidgetItem *row = rowFor(todoId);
    if (!row) return;
    if (row->parent() && !row->parent()->isExpanded()) row->parent()->setExpanded(true);
    m_tree->setCurrentItem(row);
}

void RequestsPanel::refresh() {
    const QString selected = selectedId();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        forEachRow(m_tree->topLevelItem(i), [this](QTreeWidgetItem *row) {
            const QString key = row->data(0, kGroupRole).toString();
            if (!key.isEmpty() && row->childCount() > 0) m_expanded.insert(key, row->isExpanded());
        });
    m_rebuilding = true;
    m_tree->clear();

    // Tasks are the model's todos; the batch says which list they belong to.
    QHash<QString, int> batch;
    for (const auto &task : m_model->tasks()) batch.insert(task.key, task.batch);
    const int current = m_model->currentBatch();
    auto addTodo = [&](QTreeWidgetItem *parent, const LedgerTodo &todo) {
        auto *row = parent ? new QTreeWidgetItem(parent, {rowText(todo)}) : new QTreeWidgetItem(m_tree, {rowText(todo)});
        row->setData(0, kIdRole, todo.id);
        row->setForeground(0, statusColor(todo.status));
        QString tip = QStringLiteral("%1 · %2\n\n%3").arg(todo.id, RequestLedgerModel::statusLabel(todo.status), clean(todo.text));
        if (!todo.note.isEmpty()) tip += QStringLiteral("\n\n") + clean(todo.note);
        row->setToolTip(0, tip);
    };

    const QList<LedgerTodo> todos = m_model->allTodos();
    for (const auto &todo : todos)
        if (batch.value(todo.id, current) == current) addTodo(nullptr, todo);
    QTreeWidgetItem *earlier = nullptr;
    for (const auto &todo : todos) {
        if (batch.value(todo.id, current) >= current) continue;
        if (!earlier) {
            const TaskSummary sum = m_model->earlierSummary();
            earlier = new QTreeWidgetItem(m_tree, {sum.total > 0 ? QStringLiteral("Earlier · %1").arg(sum.progress(true)) : QStringLiteral("Earlier")});
            earlier->setData(0, kGroupRole, QStringLiteral("#earlier"));
            earlier->setForeground(0, theme::TextMuted);
            earlier->setToolTip(0, QStringLiteral("Task lists finished before the current one"));
        }
        addTodo(earlier, todo);
    }
    if (earlier) earlier->setExpanded(m_expanded.value(QStringLiteral("#earlier"), false));

    m_rebuilding = false;
    if (!selected.isEmpty()) select(selected);
    if (!m_tree->currentItem() && m_tree->topLevelItemCount() > 0) m_tree->setCurrentItem(m_tree->topLevelItem(0));
    const TaskSummary sum = m_model->summary();
    m_title->setText(sum.total > 0 ? QStringLiteral("Tasks · %1").arg(sum.progress(true)) : QStringLiteral("Tasks"));
    m_title->setToolTip(m_model->chipToolTip());
    updateDetail();
}

void RequestsPanel::enter() {
    QString target;
    for (const auto &task : m_model->tasks())
        if (task.batch == m_model->currentBatch() && task.outcome == TaskOutcome::Active) { target = task.key; break; }
    if (target.isEmpty()) {
        for (const auto &task : m_model->tasks())
            if (task.batch == m_model->currentBatch()) target = task.key;
    }
    if (!target.isEmpty()) select(target);
    m_tree->setFocus(Qt::OtherFocusReason);
}

void RequestsPanel::updateDetail() {
    const QString id = selectedId();
    if (id.isEmpty()) {
        m_detail->setText(m_model->allTodos().isEmpty()
                              ? QStringLiteral("No task list for this session yet.\n\nThe agent writes one when a message has "
                                               "several asks or the work takes several steps; a single simple ask runs without one.")
                              : QString());
        return;
    }
    for (const auto &todo : m_model->allTodos()) {
        if (todo.id != id) continue;
        QStringList lines;
        lines << QStringLiteral("%1 · %2").arg(todo.id, RequestLedgerModel::statusLabel(todo.status));
        if (!todo.note.isEmpty()) lines << QStringLiteral("Note: ") + todo.note;
        lines << QString() << todo.text;
        m_detail->setText(clean(lines.join('\n')));
        return;
    }
    m_detail->clear();
}

bool RequestsPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched != m_tree || event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    const auto *key = static_cast<QKeyEvent *>(event);
    const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (mods) return false;
    switch (key->key()) {
    case Qt::Key_Escape: if (onClose) onClose(); return true;
    case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_Space: {
        QTreeWidgetItem *item = m_tree->currentItem();
        while (item && item->childCount() == 0 && item->parent()) item = item->parent();
        if (item && item->childCount() > 0) { item->setExpanded(!item->isExpanded()); m_tree->setCurrentItem(item); }
        return true;
    }
    default: return false;
    }
}

}  // namespace relay
