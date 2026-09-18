// SPDX-License-Identifier: GPL-3.0-or-later
#include "Conversations.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace relay::conversations {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kItemRole = Qt::UserRole + 2;
}  // namespace

// ----- pure helpers ------------------------------------------------------------------------

QString kindLabel(const QString &kind) {
    if (kind == QLatin1String("prompt")) return QStringLiteral("You");
    if (kind == QLatin1String("reply")) return QStringLiteral("Agent");
    if (kind == QLatin1String("tool_call")) return QStringLiteral("Tool");
    if (kind == QLatin1String("tool_output")) return QStringLiteral("Tool output");
    if (kind == QLatin1String("command")) return QStringLiteral("Command");
    if (kind == QLatin1String("command_output")) return QStringLiteral("Command output");
    return kind;
}

bool isTerminalKind(const QString &kind) {
    return kind == QLatin1String("command") || kind == QLatin1String("command_output");
}

QString highlighted(const QString &line, const QJsonArray &ranges) {
    QString out;
    int cursor = 0;
    for (const auto &value : ranges) {
        const QJsonArray pair = value.toArray();
        if (pair.size() != 2) continue;
        const int start = pair.at(0).toInt(-1);
        const int length = pair.at(1).toInt(0);
        if (start < cursor || length <= 0 || start >= line.size() || start + length > line.size()) continue;
        out += line.mid(cursor, start - cursor).toHtmlEscaped();
        // Qt's rich text has no <mark>, so the highlight is an inline background span.
        out += QStringLiteral("<span style=\"background-color:#f5d76e;color:#101216;\">")
             + line.mid(start, length).toHtmlEscaped() + QStringLiteral("</span>");
        cursor = start + length;
    }
    out += line.mid(cursor).toHtmlEscaped();
    return out;
}

QString whenText(double epochSeconds, const QDateTime &now) {
    if (epochSeconds <= 0) return QStringLiteral("—");
    const QDateTime when = QDateTime::fromSecsSinceEpoch(qint64(epochSeconds));
    const qint64 seconds = when.secsTo(now);
    if (seconds < 60 && seconds > -60) return QStringLiteral("just now");
    if (seconds < 3600 && seconds > 0) return QStringLiteral("%1 min ago").arg(seconds / 60);
    if (when.date() == now.date()) return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (when.date().addDays(1) == now.date())
        return QStringLiteral("yesterday ") + when.toString(QStringLiteral("HH:mm"));
    if (when.date().year() == now.date().year()) return when.toString(QStringLiteral("d MMM"));
    return when.toString(QStringLiteral("d MMM yyyy"));
}

double sinceFor(const QString &id, const QDateTime &now) {
    if (id == QLatin1String("today")) return double(QDateTime(now.date(), QTime(0, 0)).toSecsSinceEpoch());
    if (id == QLatin1String("week")) return double(now.addDays(-7).toSecsSinceEpoch());
    if (id == QLatin1String("month")) return double(now.addDays(-30).toSecsSinceEpoch());
    return 0;
}

QString stripAnsi(const QByteArray &bytes, int maxChars) {
    const QString text = QString::fromUtf8(bytes);
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QChar(0x1b)) {
            if (i + 1 >= text.size()) break;
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('[')) {               // CSI: parameters, then a final byte
                i += 2;
                while (i < text.size() && (text.at(i) < QChar(0x40) || text.at(i) > QChar(0x7e))) ++i;
            } else if (next == QLatin1Char(']')) {        // OSC: up to BEL or ST
                i += 2;
                while (i < text.size() && text.at(i) != QChar(0x07)
                       && !(text.at(i) == QChar(0x1b) && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\\')))
                    ++i;
                if (i < text.size() && text.at(i) == QChar(0x1b)) ++i;
            } else if (next == QLatin1Char('P') || next == QLatin1Char('X') || next == QLatin1Char('^')
                       || next == QLatin1Char('_')) {     // DCS/SOS/PM/APC: up to ST
                i += 2;
                while (i + 1 < text.size() && !(text.at(i) == QChar(0x1b) && text.at(i + 1) == QLatin1Char('\\'))) ++i;
                ++i;
            } else {
                ++i;                                       // two-character escape
            }
            continue;
        }
        if (ch == QLatin1Char('\r')) continue;
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\t')) { out += ch; continue; }
        if (ch < QChar(0x20) || ch == QChar(0x7f)) continue;
        out += ch;
    }
    while (out.endsWith(QLatin1Char('\n')) || out.endsWith(QLatin1Char(' '))) out.chop(1);
    if (maxChars > 0 && out.size() > maxChars) out = out.left(maxChars);
    return out;
}

// ----- dialog ------------------------------------------------------------------------------

Dialog::Dialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("conversationsDialog"));
    setWindowTitle(QStringLiteral("Conversations"));
    resize(1080, 640);

    m_search = new QLineEdit;
    m_search->setClearButtonEnabled(true);
    m_search->setPlaceholderText(QStringLiteral("Search every conversation and Relay's terminal history · \"quoted\" for a phrase"));
    m_search->installEventFilter(this);

    m_scope = new QComboBox;
    m_scope->addItem(QStringLiteral("This project"), QStringLiteral("project"));
    m_scope->addItem(QStringLiteral("All projects"), QStringLiteral("all"));
    m_kind = new QComboBox;
    m_kind->addItem(QStringLiteral("Everything"), QString());
    m_kind->addItem(QStringLiteral("Agent threads"), QStringLiteral("agent"));
    m_kind->addItem(QStringLiteral("Terminal history"), QStringLiteral("terminal"));
    m_model = new QComboBox;
    m_model->addItem(QStringLiteral("Any model"), QString());
    m_date = new QComboBox;
    m_date->addItem(QStringLiteral("Any time"), QStringLiteral("any"));
    m_date->addItem(QStringLiteral("Today"), QStringLiteral("today"));
    m_date->addItem(QStringLiteral("Last 7 days"), QStringLiteral("week"));
    m_date->addItem(QStringLiteral("Last 30 days"), QStringLiteral("month"));
    m_open = new QCheckBox(QStringLiteral("Open tasks"));

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("Conversation"), QStringLiteral("Updated"),
                             QStringLiteral("Turns"), QStringLiteral("Model")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->installEventFilter(this);

    m_header = new QLabel;
    m_header->setWordWrap(true);
    m_header->setTextFormat(Qt::RichText);
    m_preview = new QTextBrowser;
    m_preview->setOpenExternalLinks(false);
    m_preview->setObjectName(QStringLiteral("conversationPreview"));

    auto *right = new QWidget;
    auto *rightBox = new QVBoxLayout(right);
    rightBox->setContentsMargins(0, 0, 0, 0);
    rightBox->addWidget(m_header);
    rightBox->addWidget(m_preview, 1);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(m_tree);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);

    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);

    m_resume = new QPushButton(QStringLiteral("Resume here"));
    m_resume->setDefault(true);
    m_newPane = new QPushButton(QStringLiteral("Open in new pane"));
    m_rename = new QPushButton(QStringLiteral("Rename…"));
    m_pin = new QPushButton(QStringLiteral("Pin"));
    m_delete = new QPushButton(QStringLiteral("Delete…"));
    auto *close = new QPushButton(QStringLiteral("Close"));

    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    filters->addWidget(m_scope);
    filters->addWidget(m_kind);
    filters->addWidget(m_model);
    filters->addWidget(m_date);
    filters->addWidget(m_open);
    filters->addStretch(1);
    filters->addWidget(m_status);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_rename);
    buttons->addWidget(m_pin);
    buttons->addWidget(m_delete);
    buttons->addStretch(1);
    buttons->addWidget(m_newPane);
    buttons->addWidget(m_resume);
    buttons->addWidget(close);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(8);
    box->addWidget(m_search);
    box->addLayout(filters);
    box->addWidget(splitter, 1);
    auto *hint = new QLabel(QStringLiteral("Enter resumes in this pane · Shift+Enter opens a new pane · Esc closes"));
    hint->setObjectName(QStringLiteral("dialogHint"));
    box->addWidget(hint);
    box->addLayout(buttons);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &Dialog::requery);

    connect(m_search, &QLineEdit::textChanged, this, &Dialog::scheduleQuery);
    for (QComboBox *combo : {m_scope, m_kind, m_model, m_date})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Dialog::requery);
    connect(m_open, &QCheckBox::toggled, this, &Dialog::requery);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &Dialog::selectionChanged);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this] { activate(false); });
    connect(m_resume, &QPushButton::clicked, this, [this] { activate(false); });
    connect(m_newPane, &QPushButton::clicked, this, [this] { activate(true); });
    connect(m_rename, &QPushButton::clicked, this, &Dialog::rename);
    connect(m_pin, &QPushButton::clicked, this, &Dialog::togglePin);
    connect(m_delete, &QPushButton::clicked, this, &Dialog::remove);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    updateButtons();
}

QString Dialog::query() const { return m_search ? m_search->text() : QString(); }

void Dialog::focusSearch() {
    m_search->setFocus();
    m_search->selectAll();
}

void Dialog::scheduleQuery() { m_debounce->start(); }

void Dialog::refresh() { requery(); }

void Dialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    // Opening the list must show the conversations without the user typing anything.
    requery();
}

void Dialog::requery() {
    if (!onQuery) return;
    QJsonObject request{{QStringLiteral("query"), m_search->text()},
                        {QStringLiteral("scope"), m_scope->currentData().toString()},
                        {QStringLiteral("limit"), 100}};
    if (m_open->isChecked()) request.insert(QStringLiteral("has_open_tasks"), true);
    const QString model = m_model->currentData().toString();
    if (!model.isEmpty()) request.insert(QStringLiteral("model"), model);
    const double since = sinceFor(m_date->currentData().toString(), QDateTime::currentDateTime());
    if (since > 0) request.insert(QStringLiteral("since"), since);
    const QString kind = m_kind->currentData().toString();
    if (!kind.isEmpty()) request.insert(QStringLiteral("sources"), QJsonArray{kind});
    onQuery(request);
}

void Dialog::removed(const QString &sessionId) {
    Q_UNUSED(sessionId);
    requery();
}

void Dialog::setResults(const QJsonObject &event) {
    m_items = event.value(QStringLiteral("items")).toArray();
    const QString keep = m_pendingSelect.isEmpty() ? selectedId() : m_pendingSelect;
    m_pendingSelect.clear();
    m_filling = true;
    m_tree->clear();

    // Remember every model we have seen so the filter keeps working across queries.
    QStringList models;
    for (int i = 1; i < m_model->count(); ++i) models << m_model->itemData(i).toString();

    QHash<QString, QTreeWidgetItem *> groups;
    QTreeWidgetItem *first = nullptr, *wanted = nullptr;
    const QDateTime now = QDateTime::currentDateTime();
    int matches = 0;
    for (const auto &value : std::as_const(m_items)) {
        const QJsonObject item = value.toObject();
        const QString project = item.value(QStringLiteral("project")).toString();
        QTreeWidgetItem *group = groups.value(project);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {project.isEmpty() ? QStringLiteral("(no project)") : project});
            group->setFirstColumnSpanned(true);
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
            groups.insert(project, group);
        }
        const bool terminal = item.value(QStringLiteral("source")).toString() == QLatin1String("terminal");
        const bool pinned = item.value(QStringLiteral("pinned")).toInt() > 0;
        const int open = item.value(QStringLiteral("open_requests")).toInt();
        QString title = item.value(QStringLiteral("title")).toString();
        if (title.isEmpty()) title = QStringLiteral("Untitled");
        QString label = (pinned ? QStringLiteral("📌 ") : QString()) + (terminal ? QStringLiteral("$ ") : QString()) + title;
        auto *row = new QTreeWidgetItem(group, {label,
            whenText(item.value(QStringLiteral("updated")).toDouble(), now),
            QString::number(item.value(QStringLiteral("turns")).toInt()) + (open > 0 ? QStringLiteral(" · %1 open").arg(open) : QString()),
            terminal ? QStringLiteral("terminal") : item.value(QStringLiteral("model")).toString()});
        const QString sessionId = item.value(QStringLiteral("session_id")).toString();
        row->setData(0, kIdRole, sessionId);
        row->setData(0, kItemRole, QString::fromUtf8(QJsonDocument(item).toJson(QJsonDocument::Compact)));
        row->setToolTip(0, item.value(QStringLiteral("workspace")).toString());
        const QJsonArray rowMatches = item.value(QStringLiteral("matches")).toArray();
        matches += item.value(QStringLiteral("match_count")).toInt();
        if (!rowMatches.isEmpty()) {
            const QJsonObject first = rowMatches.first().toObject();
            row->setToolTip(0, QStringLiteral("%1 · turn %2 · %3")
                                   .arg(item.value(QStringLiteral("workspace")).toString())
                                   .arg(first.value(QStringLiteral("turn")).toInt())
                                   .arg(kindLabel(first.value(QStringLiteral("kind")).toString())));
        } else if (!item.value(QStringLiteral("snippet")).toString().isEmpty()) {
            row->setToolTip(0, item.value(QStringLiteral("snippet")).toString());
        }
        const QString model = item.value(QStringLiteral("model")).toString();
        if (!model.isEmpty() && !models.contains(model)) { models << model; m_model->addItem(model, model); }
        if (!first) first = row;
        if (!keep.isEmpty() && sessionId == keep) wanted = row;
    }
    m_filling = false;
    if (QTreeWidgetItem *select = wanted ? wanted : first) m_tree->setCurrentItem(select);
    const int elapsed = event.value(QStringLiteral("elapsed_ms")).toInt();
    const QString scope = m_scope->currentData().toString() == QLatin1String("project")
                              ? QStringLiteral("this project") : QStringLiteral("all projects");
    m_status->setText(m_search->text().trimmed().isEmpty()
        ? QStringLiteral("%1 conversation(s) in %2 · %3 ms").arg(m_items.size()).arg(scope).arg(elapsed)
        : QStringLiteral("%1 conversation(s), %2 match(es) in %3 · %4 ms").arg(m_items.size()).arg(matches).arg(scope).arg(elapsed));
    if (m_items.isEmpty()) {
        m_header->clear();
        m_preview->setPlainText(m_search->text().trimmed().isEmpty()
            ? QStringLiteral("No saved conversations yet in this scope.")
            : QStringLiteral("No conversation or terminal command matches “%1”.").arg(m_search->text()));
    }
    updateButtons();
}

QString Dialog::selectedId() const {
    auto *item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

QJsonObject Dialog::selectedItem() const {
    auto *item = m_tree->currentItem();
    if (!item) return {};
    return QJsonDocument::fromJson(item->data(0, kItemRole).toString().toUtf8()).object();
}

void Dialog::selectionChanged() {
    updateButtons();
    if (m_filling) return;
    const QString id = selectedId();
    if (id.isEmpty()) { m_header->clear(); m_preview->clear(); return; }
    const QJsonObject item = selectedItem();
    const QJsonArray matches = item.value(QStringLiteral("matches")).toArray();
    // Show the matching turns at once; the full preview arrives from the worker.
    QString html;
    for (const auto &value : matches) {
        const QJsonObject match = value.toObject();
        html += QStringLiteral("<p><b>turn %1 · %2</b><br>%3</p>")
                    .arg(match.value(QStringLiteral("turn")).toInt())
                    .arg(kindLabel(match.value(QStringLiteral("kind")).toString()),
                         highlighted(match.value(QStringLiteral("line")).toString(),
                                     match.value(QStringLiteral("ranges")).toArray()));
    }
    m_preview->setHtml(html.isEmpty() ? QStringLiteral("<p>Loading…</p>") : html);
    m_header->setText(QStringLiteral("<b>%1</b><br>%2")
                          .arg(item.value(QStringLiteral("title")).toString().toHtmlEscaped(),
                               item.value(QStringLiteral("workspace")).toString().toHtmlEscaped()));
    if (onPreview) onPreview(id, m_search->text());
}

void Dialog::setPreview(const QJsonObject &event) {
    const QString id = event.value(QStringLiteral("session_id")).toString();
    if (id != selectedId()) return;
    const QJsonArray items = event.value(QStringLiteral("items")).toArray();
    QString html;
    int lastTurn = -1;
    for (const auto &value : items) {
        const QJsonObject entry = value.toObject();
        const int turn = entry.value(QStringLiteral("turn")).toInt();
        if (turn != lastTurn) {
            lastTurn = turn;
            html += QStringLiteral("<h4>turn %1</h4>").arg(turn);
        }
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const QJsonArray ranges = entry.value(QStringLiteral("ranges")).toArray();
        QString body;
        if (!ranges.isEmpty())
            body = highlighted(entry.value(QStringLiteral("line")).toString(), ranges);
        else
            body = entry.value(QStringLiteral("text")).toString().left(1200).toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        QString status;
        if (entry.contains(QStringLiteral("exit_status")))
            status = QStringLiteral(" · exit %1").arg(entry.value(QStringLiteral("exit_status")).toInt());
        html += QStringLiteral("<p><b>%1%2</b><br>%3</p>").arg(kindLabel(kind), status, body);
    }
    if (html.isEmpty()) html = QStringLiteral("<p>This conversation has no indexed turns.</p>");
    m_preview->setHtml(html);
    const int found = event.value(QStringLiteral("match_count")).toInt();
    m_header->setText(QStringLiteral("<b>%1</b><br>%2%3")
                          .arg(event.value(QStringLiteral("title")).toString().toHtmlEscaped(),
                               event.value(QStringLiteral("workspace")).toString().toHtmlEscaped(),
                               found > 0 ? QStringLiteral(" · %1 matching turn(s)").arg(found) : QString()));
}

void Dialog::updateButtons() {
    const QJsonObject item = selectedItem();
    const bool has = !item.isEmpty();
    const bool terminal = item.value(QStringLiteral("source")).toString() == QLatin1String("terminal");
    m_resume->setEnabled(has && !terminal);
    m_newPane->setEnabled(has && !terminal);
    m_rename->setEnabled(has);
    m_pin->setEnabled(has);
    m_delete->setEnabled(has);
    m_pin->setText(item.value(QStringLiteral("pinned")).toInt() > 0 ? QStringLiteral("Unpin") : QStringLiteral("Pin"));
    m_resume->setToolTip(terminal ? QStringLiteral("Terminal history cannot be resumed; it is here to be searched.")
                                  : QStringLiteral("Replace this pane's conversation with the selected one."));
}

void Dialog::activate(bool newPane) {
    const QJsonObject item = selectedItem();
    if (item.isEmpty()) return;
    if (item.value(QStringLiteral("source")).toString() == QLatin1String("terminal")) {
        m_status->setText(QStringLiteral("Terminal history cannot be resumed; use the preview."));
        return;
    }
    if (onResume) onResume(item, newPane);
    accept();
}

void Dialog::rename() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || !onRename) return;
    bool ok = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("Rename conversation"),
                                                QStringLiteral("Title (empty restores the generated one):"),
                                                QLineEdit::Normal, item.value(QStringLiteral("title")).toString(), &ok);
    if (!ok) return;
    m_pendingSelect = item.value(QStringLiteral("session_id")).toString();
    onRename(m_pendingSelect, title);
}

void Dialog::togglePin() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || !onPin) return;
    m_pendingSelect = item.value(QStringLiteral("session_id")).toString();
    onPin(m_pendingSelect, item.value(QStringLiteral("pinned")).toInt() == 0);
}

void Dialog::remove() {
    const QJsonObject item = selectedItem();
    if (item.isEmpty() || !onDelete) return;
    const bool terminal = item.value(QStringLiteral("source")).toString() == QLatin1String("terminal");
    QMessageBox confirm(QMessageBox::Warning, QStringLiteral("Delete conversation"),
                        terminal ? QStringLiteral("Delete the indexed terminal history of “%1”?")
                                       .arg(item.value(QStringLiteral("project")).toString())
                                 : QStringLiteral("Delete “%1” permanently?").arg(item.value(QStringLiteral("title")).toString()),
                        QMessageBox::Cancel, this);
    confirm.setInformativeText(terminal
        ? QStringLiteral("Only the index rows are removed; your shell's own history file is untouched.")
        : QStringLiteral("The conversation, its checkpoint file copies and its index rows are deleted. This cannot be undone."));
    auto *remove = confirm.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
    confirm.exec();
    if (confirm.clickedButton() != remove) return;
    onDelete(item.value(QStringLiteral("session_id")).toString());
}

bool Dialog::eventFilter(QObject *object, QEvent *event) {
    if (event->type() != QEvent::KeyPress) return QDialog::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (object == m_search) {
        switch (key->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
            QApplication::sendEvent(m_tree, key);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activate(key->modifiers() & Qt::ShiftModifier);
            return true;
        default:
            break;
        }
        return QDialog::eventFilter(object, event);
    }
    if (object == m_tree && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
        activate(key->modifiers() & Qt::ShiftModifier);
        return true;
    }
    return QDialog::eventFilter(object, event);
}

// ----- find bar ------------------------------------------------------------------------------

FindBar::FindBar(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("findBar"));
    m_field = new QLineEdit;
    m_field->setPlaceholderText(QStringLiteral("Find in this pane"));
    m_field->setClearButtonEnabled(true);
    m_label = new QLabel;
    m_label->setObjectName(QStringLiteral("findCount"));
    m_previous = new QToolButton; m_previous->setText(QStringLiteral("↑")); m_previous->setAutoRaise(true);
    m_previous->setToolTip(QStringLiteral("Previous match (Shift+Enter)"));
    m_next = new QToolButton; m_next->setText(QStringLiteral("↓")); m_next->setAutoRaise(true);
    m_next->setToolTip(QStringLiteral("Next match (Enter)"));
    m_inConversation = new QPushButton(QStringLiteral("In conversation"));
    m_inConversation->setFlat(true);
    m_inConversation->setVisible(false);
    m_close = new QToolButton; m_close->setText(QStringLiteral("×")); m_close->setAutoRaise(true);
    m_close->setToolTip(QStringLiteral("Close (Esc)"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(6);
    row->addWidget(new QLabel(QStringLiteral("Find")));
    row->addWidget(m_field, 1);
    row->addWidget(m_label);
    row->addWidget(m_previous);
    row->addWidget(m_next);
    row->addWidget(m_inConversation);
    row->addWidget(m_close);

    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textChanged, this, &FindBar::refresh);
    connect(m_next, &QToolButton::clicked, this, [this] { step(false); });
    connect(m_previous, &QToolButton::clicked, this, [this] { step(true); });
    connect(m_close, &QToolButton::clicked, this, [this] { hide(); if (onClosed) onClosed(); });
    connect(m_inConversation, &QPushButton::clicked, this,
            [this] { if (onOpenConversation) onOpenConversation(m_field->text()); });
}

QString FindBar::text() const { return m_field->text(); }

void FindBar::setTerminalSearchable(bool can) { m_canSearchTerminal = can; updateLabel(); }

void FindBar::start(const QString &preset) {
    // Setting the text already re-runs the search through textChanged; do not ask twice.
    const bool changed = !preset.isEmpty() && preset != m_field->text();
    if (changed) m_field->setText(preset);
    show();
    m_field->setFocus();
    m_field->selectAll();
    if (!changed) refresh();
}

void FindBar::refresh() {
    m_conversationMatches = -1;
    const QString needle = m_field->text();
    m_terminalMatches = (needle.isEmpty() || !onFind || !m_canSearchTerminal) ? 0 : onFind(needle, false);
    if (!needle.isEmpty() && onCountConversation) onCountConversation(needle);
    updateLabel();
}

void FindBar::step(bool backwards) {
    if (m_field->text().isEmpty() || !onFind || !m_canSearchTerminal) return;
    m_terminalMatches = onFind(m_field->text(), backwards);
    updateLabel();
}

void FindBar::setConversationMatches(int matches) {
    m_conversationMatches = matches;
    updateLabel();
}

void FindBar::updateLabel() {
    if (m_field->text().isEmpty()) { m_label->setText(QString()); m_inConversation->setVisible(false); return; }
    QStringList parts;
    parts << (m_canSearchTerminal ? QStringLiteral("%1 in terminal").arg(m_terminalMatches)
                                  : QStringLiteral("terminal search needs the Relay engine"));
    if (m_conversationMatches >= 0) parts << QStringLiteral("%1 in conversation").arg(m_conversationMatches);
    m_label->setText(parts.join(QStringLiteral(" · ")));
    m_inConversation->setVisible(m_conversationMatches > 0);
}

void FindBar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { hide(); if (onClosed) onClosed(); return; }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        step(event->modifiers() & Qt::ShiftModifier);
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FindBar::eventFilter(QObject *object, QEvent *event) {
    if (object != m_field || event->type() != QEvent::KeyPress) return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape) { hide(); if (onClosed) onClosed(); return true; }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        step(key->modifiers() & Qt::ShiftModifier);
        return true;
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay::conversations
