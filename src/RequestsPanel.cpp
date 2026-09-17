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

namespace relay {

namespace {
constexpr int kIdRole = Qt::UserRole;
const QColor kDone{126, 200, 140};
const QColor kWarn{229, 192, 123};
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
    if (status == QStringLiteral("blocked") || status == QStringLiteral("deferred")) return kWarn;
    if (status.startsWith(QStringLiteral("cancelled"))) return kCancelled;
    return theme::Text;
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
    m_keys = new QLabel(QStringLiteral("↑↓ select · Enter expand · d done · x cancel · o reopen · r re-ask · Esc close"));
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
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { if (!m_rebuilding) { updateDetail(); updateButtons(); } });
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
    auto *buttons = new QHBoxLayout;
    auto button = [&](const QString &text, const QString &tip, const QString &action) {
        auto *b = new QToolButton; b->setText(text); b->setToolTip(tip); b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [this, action] { act(action); });
        buttons->addWidget(b);
        return b;
    };
    m_done = button(QStringLiteral("Mark done  d"), QStringLiteral("Mark the request done (the agent will not change it)"), QStringLiteral("done"));
    m_cancel = button(QStringLiteral("Cancel  x"), QStringLiteral("Cancel the request (the agent will not change it)"), QStringLiteral("cancel"));
    m_reopen = button(QStringLiteral("Reopen  o"), QStringLiteral("Mark the request open again"), QStringLiteral("reopen"));
    m_reask = button(QStringLiteral("Re-ask  r"), QStringLiteral("Queue the verbatim request again"), QStringLiteral("reask"));
    buttons->addStretch(1);
    layout->addLayout(buttons);
    refresh();
}

QString RequestsPanel::selectedId() const {
    QTreeWidgetItem *item = m_tree->currentItem();
    while (item && item->parent()) item = item->parent();
    return item ? item->data(0, kIdRole).toString() : QString();
}

void RequestsPanel::select(const QString &ledgerId) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (m_tree->topLevelItem(i)->data(0, kIdRole).toString() == ledgerId) { m_tree->setCurrentItem(m_tree->topLevelItem(i)); return; }
}

void RequestsPanel::refresh() {
    const QString selected = selectedId();
    QSet<QString> expanded;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (m_tree->topLevelItem(i)->isExpanded()) expanded.insert(m_tree->topLevelItem(i)->data(0, kIdRole).toString());
    m_rebuilding = true;
    m_tree->clear();
    auto child = [](QTreeWidgetItem *parent, const QString &text, const QColor &color) {
        auto *row = new QTreeWidgetItem(parent, {clean(text).simplified()});
        row->setForeground(0, color);
        row->setToolTip(0, clean(text));
        return row;
    };
    for (const auto &request : m_model->requests()) {
        QString text = QStringLiteral("%1  %2  %3").arg(RequestLedgerModel::statusGlyph(request.status), request.id, clean(request.preview).simplified());
        if (request.origin == QStringLiteral("relay")) text += QStringLiteral("  · Relay");
        auto *item = new QTreeWidgetItem(m_tree, {text});
        item->setData(0, kIdRole, request.id);
        item->setForeground(0, statusColor(request.status));
        item->setToolTip(0, QStringLiteral("%1 · %2 · %3\n\n%4").arg(request.id, RequestLedgerModel::statusLabel(request.status), request.source, clean(request.fullText())));
        if (!request.reason.isEmpty())
            child(item, QStringLiteral("%1: %2").arg(RequestLedgerModel::statusLabel(request.status), request.reason), kWarn);
        for (const QString &quote : request.auditQuotes)
            child(item, QStringLiteral("⚠ may be unaddressed: “%1”").arg(quote), kWarn);
        for (const auto &todo : m_model->todosFor(request.id)) {
            QString line = QStringLiteral("%1 %2  %3").arg(RequestLedgerModel::todoGlyph(todo.status), todo.id, todo.text);
            if (!todo.note.isEmpty()) line += QStringLiteral(" — ") + todo.note;
            child(item, line, statusColor(todo.status));
        }
        if (expanded.contains(request.id)) item->setExpanded(true);
    }
    const auto unlinked = m_model->unlinkedTodos();
    if (!unlinked.isEmpty()) {
        auto *group = new QTreeWidgetItem(m_tree, {QStringLiteral("Other todos (%1)").arg(unlinked.size())});
        group->setForeground(0, theme::TextMuted);
        for (const auto &todo : unlinked) {
            QString line = QStringLiteral("%1 %2  %3").arg(RequestLedgerModel::todoGlyph(todo.status), todo.id, todo.text);
            if (!todo.note.isEmpty()) line += QStringLiteral(" — ") + todo.note;
            child(group, line, statusColor(todo.status));
        }
        group->setExpanded(true);
    }
    m_rebuilding = false;
    if (!selected.isEmpty()) select(selected);
    if (!m_tree->currentItem() && m_tree->topLevelItemCount() > 0) m_tree->setCurrentItem(m_tree->topLevelItem(m_tree->topLevelItemCount() - 1));
    int todosOpen = m_model->openTodos();
    m_title->setText(QStringLiteral("Requests · %1 open of %2%3").arg(m_model->openCount()).arg(m_model->total())
                         .arg(todosOpen > 0 ? QStringLiteral(" · %1 todo%2 open").arg(todosOpen).arg(todosOpen == 1 ? QString() : QStringLiteral("s")) : QString()));
    updateDetail();
    updateButtons();
}

void RequestsPanel::enter() {
    QString target;
    for (const auto &request : m_model->requests()) if (request.open() && request.requiresCompletion) { target = request.id; break; }
    if (target.isEmpty() && !m_model->requests().isEmpty()) target = m_model->requests().last().id;
    if (!target.isEmpty()) select(target);
    m_tree->setFocus(Qt::OtherFocusReason);
}

void RequestsPanel::updateDetail() {
    const LedgerRequest *request = m_model->find(selectedId());
    if (!request) { m_detail->setText(m_model->requests().isEmpty() ? QStringLiteral("No requests in this session yet.") : QString()); return; }
    QStringList lines;
    lines << QStringLiteral("%1 · %2 · %3%4").arg(request->id, RequestLedgerModel::statusLabel(request->status), request->source,
                                                   request->turn > 0 ? QStringLiteral(" · turn %1").arg(request->turn) : QString());
    if (!request->reason.isEmpty()) lines << QStringLiteral("Reason: ") + request->reason;
    for (const QString &quote : request->auditQuotes) lines << QStringLiteral("⚠ may be unaddressed: “%1”").arg(quote);
    if (!request->attachments.isEmpty()) lines << QStringLiteral("Attachments: ") + request->attachments.join(QStringLiteral(", "));
    lines << QString() << request->fullText();
    m_detail->setText(clean(lines.join('\n')));
    if (request->text.isEmpty() && !m_fetched.contains(request->id) && onFetch) {
        m_fetched.insert(request->id);
        onFetch(request->id);
    }
}

void RequestsPanel::updateButtons() {
    const LedgerRequest *request = m_model->find(selectedId());
    const QString status = request ? request->status : QString();
    m_done->setEnabled(request && status != QStringLiteral("done"));
    m_cancel->setEnabled(request && status != QStringLiteral("cancelled_by_user"));
    m_reopen->setEnabled(request && !request->open());
    m_reask->setEnabled(request && RequestLedgerModel::canReask(*request));
}

void RequestsPanel::act(const QString &action) {
    const LedgerRequest *request = m_model->find(selectedId());
    if (!request) return;
    const QString id = request->id;
    if (action == QStringLiteral("done") && m_done->isEnabled() && onSetStatus) onSetStatus(id, QStringLiteral("done"));
    else if (action == QStringLiteral("cancel") && m_cancel->isEnabled() && onSetStatus) onSetStatus(id, QStringLiteral("cancelled_by_user"));
    else if (action == QStringLiteral("reopen") && m_reopen->isEnabled() && onSetStatus) onSetStatus(id, QStringLiteral("open"));
    else if (action == QStringLiteral("reask") && m_reask->isEnabled() && onReask) onReask(id);
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
        if (item && item->parent()) item = item->parent();
        if (item) { item->setExpanded(!item->isExpanded()); m_tree->setCurrentItem(item); }
        return true;
    }
    case Qt::Key_D: act(QStringLiteral("done")); return true;
    case Qt::Key_X: case Qt::Key_Delete: act(QStringLiteral("cancel")); return true;
    case Qt::Key_O: act(QStringLiteral("reopen")); return true;
    case Qt::Key_R: act(QStringLiteral("reask")); return true;
    default: return false;
    }
}

}  // namespace relay
