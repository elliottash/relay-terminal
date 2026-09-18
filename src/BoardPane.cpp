// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardPane.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include "RichEditor.h"

namespace relay {
namespace {

constexpr int kCardRole = Qt::UserRole;          // the card id on a list row
// CardList has no Q_OBJECT (it declares no signals of its own), so its column travels as a
// dynamic property rather than through qobject_cast.
const char *const kColumnProperty = "relayBoardColumn";

QString columnIdOf(const QWidget *widget)
{
    return widget ? widget->property(kColumnProperty).toString() : QString();
}

QString chip(const QString &text)
{
    return text.isEmpty() ? QString() : QStringLiteral(" · ") + text;
}

// Two lines: the id and the title, then the chips the design asks a card to show.
QString rowText(const board::Card &card)
{
    QString second;
    if (!card.labels.isEmpty())
        second += card.labels.join(QStringLiteral(", "));
    if (card.assignee == QStringLiteral("agent"))
        second += chip(QStringLiteral("✦ agent"));
    else if (!card.assignee.isEmpty())
        second += chip(card.assignee);
    if (!card.waitingOn.isEmpty())
        second += chip(QStringLiteral("waiting: ") + card.waitingOn);
    if (card.tasksTotal > 0)
        second += chip(QStringLiteral("%1/%2 tasks").arg(card.tasksDone).arg(card.tasksTotal));
    if (card.threadEntries > 0)
        second += chip(QStringLiteral("%1 ✎").arg(card.threadEntries));
    if (card.isPrivate)
        second += chip(QStringLiteral("private"));
    QString head = QStringLiteral("#%1  %2").arg(card.id, card.title);
    return second.isEmpty() ? head : head + QLatin1Char('\n') + second.trimmed();
}

// A column list that reports a drop instead of moving the row itself: the card only moves once
// the worker has written the file and sent board_changed back.
class CardList final : public QListWidget {
public:
    explicit CardList(const QString &columnId, QWidget *parent = nullptr)
        : QListWidget(parent), m_columnId(columnId)
    {
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setUniformItemSizes(false);
        setWordWrap(true);
        setFrameShape(QFrame::NoFrame);
        setObjectName(QStringLiteral("boardColumn"));
        setProperty(kColumnProperty, columnId);
    }

    QString columnId() const { return m_columnId; }
    // (card id, id of the row it was dropped above, id of the row it was dropped below)
    std::function<void(const QString &, const QString &, const QString &)> onDropped;
    static QString dragging;

protected:
    void startDrag(Qt::DropActions actions) override
    {
        const QListWidgetItem *item = currentItem();
        dragging = item ? item->data(kCardRole).toString() : QString();
        QListWidget::startDrag(actions);
    }

    void dragEnterEvent(QDragEnterEvent *event) override { event->acceptProposedAction(); }
    void dragMoveEvent(QDragMoveEvent *event) override { event->acceptProposedAction(); }

    void dropEvent(QDropEvent *event) override
    {
        const QString card = dragging;
        dragging.clear();
        if (card.isEmpty()) {
            event->ignore();
            return;
        }
#if QT_VERSION_MAJOR >= 6
        const QPoint at = event->position().toPoint();
#else
        const QPoint at = event->pos();
#endif
        const int row = indexAt(at).isValid() ? indexAt(at).row() : count();
        QString before, after;
        for (int i = 0; i < count(); ++i) {
            const QString other = item(i)->data(kCardRole).toString();
            if (other == card)
                continue;
            if (i < row)
                after = other;
            else if (before.isEmpty())
                before = other;
        }
        // The model is the file on disk; nothing moves in the view until the worker says so.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (onDropped)
            onDropped(card, before, after);
    }

private:
    QString m_columnId;
};

QString CardList::dragging;

}  // namespace

// --------------------------------------------------------------------- card detail

// The right half of the Switchboard: the card as a document, plus its thread and a reply box.
class CardDetail final : public QWidget {
public:
    explicit CardDetail(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);

        m_title = new QLabel(this);
        m_title->setWordWrap(true);
        m_title->setObjectName(QStringLiteral("boardCardTitle"));
        layout->addWidget(m_title);

        auto *header = new QHBoxLayout;
        m_status = new QComboBox(this);
        m_tab = new QComboBox(this);
        m_close = new QToolButton(this);
        m_close->setText(QStringLiteral("×"));
        m_close->setToolTip(QStringLiteral("Close the card (Esc)"));
        header->addWidget(m_status);
        header->addWidget(m_tab);
        header->addStretch();
        header->addWidget(m_close);
        layout->addLayout(header);

        m_meta = new QLabel(this);
        m_meta->setWordWrap(true);
        m_meta->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_meta->setObjectName(QStringLiteral("boardCardMeta"));
        layout->addWidget(m_meta);

        m_body = new QTextBrowser(this);
        m_body->setOpenExternalLinks(true);
        m_body->setObjectName(QStringLiteral("filePreviewMarkdown"));
        layout->addWidget(m_body, 3);

        m_tasks = new QListWidget(this);
        m_tasks->setObjectName(QStringLiteral("boardTasks"));
        m_tasks->setFrameShape(QFrame::NoFrame);
        m_tasks->setMaximumHeight(160);
        layout->addWidget(m_tasks);

        m_threadLabel = new QLabel(QStringLiteral("Thread"), this);
        layout->addWidget(m_threadLabel);
        m_thread = new QTextBrowser(this);
        m_thread->setOpenExternalLinks(true);
        m_thread->setObjectName(QStringLiteral("filePreviewMarkdown"));
        layout->addWidget(m_thread, 2);

        m_reply = new RichEditor(this);
        m_reply->setPlaceholders({QStringLiteral("Reply — Enter asks the Switchboard agent"),
                                  QStringLiteral("Reply to this card…"), QStringLiteral("Reply…")});
        m_reply->setAutoHeight(2, 8);
        layout->addWidget(m_reply);

        auto *buttons = new QHBoxLayout;
        m_ask = new QPushButton(QStringLiteral("Ask the agent"), this);
        m_comment = new QPushButton(QStringLiteral("Comment only"), this);
        m_comment->setToolTip(QStringLiteral("Append to the thread without calling a model"));
        buttons->addWidget(m_ask);
        buttons->addWidget(m_comment);
        buttons->addStretch();
        layout->addLayout(buttons);

        connect(m_ask, &QPushButton::clicked, this, [this] { submit(true); });
        connect(m_comment, &QPushButton::clicked, this, [this] { submit(false); });
        connect(m_close, &QToolButton::clicked, this, [this] { if (onClose) onClose(); });
        m_reply->onSubmit = [this](const QString &) { submit(true); };
        connect(m_tasks, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
            if (m_loading || !onToggleTask)
                return;
            onToggleTask(item->data(kCardRole).toString(),
                         item->checkState() == Qt::Checked);
        });
        connect(m_status, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (!m_loading && onMove)
                onMove(QStringLiteral("status"), m_status->itemData(index).toString());
        });
        connect(m_tab, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            if (!m_loading && onMove)
                onMove(QStringLiteral("tab"), m_tab->itemData(index).toString());
        });
    }

    std::function<void(const QString &text, bool ask)> onReply;
    std::function<void()> onClose;
    std::function<void(const QString &itemId, bool done)> onToggleTask;
    std::function<void(const QString &what, const QString &value)> onMove;

    QString cardId() const { return m_id; }
    QString hash() const { return m_hash; }

    void setChoices(const QStringList &statuses, const QList<QPair<QString, QString>> &tabs)
    {
        m_loading = true;
        m_status->clear();
        for (const QString &status : statuses)
            m_status->addItem(board::statusTitle(status), status);
        m_tab->clear();
        for (const auto &tab : tabs)
            m_tab->addItem(tab.second, tab.first);
        m_loading = false;
    }

    void show(const QJsonObject &card)
    {
        m_loading = true;
        m_id = card.value(QStringLiteral("card_id")).toString();
        m_hash = card.value(QStringLiteral("hash")).toString();
        const QJsonObject front = card.value(QStringLiteral("front")).toObject();
        m_title->setText(QStringLiteral("#%1 · %2")
                             .arg(m_id, card.value(QStringLiteral("title")).toString()));
        const int status = m_status->findData(card.value(QStringLiteral("status")).toString());
        if (status >= 0)
            m_status->setCurrentIndex(status);
        const int tab = m_tab->findData(card.value(QStringLiteral("tab")).toString());
        if (tab >= 0)
            m_tab->setCurrentIndex(tab);
        m_meta->setText(metaText(front, card.value(QStringLiteral("path")).toString()));
        m_body->setMarkdown(card.value(QStringLiteral("body")).toString());

        m_tasks->clear();
        const QJsonArray tasks = card.value(QStringLiteral("tasks")).toArray();
        for (const QJsonValue &value : tasks) {
            const QJsonObject task = value.toObject();
            auto *item = new QListWidgetItem(
                QString(task.value(QStringLiteral("depth")).toInt() * 4, QLatin1Char(' '))
                + task.value(QStringLiteral("text")).toString(), m_tasks);
            item->setData(kCardRole, task.value(QStringLiteral("item_id")).toString());
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(task.value(QStringLiteral("done")).toBool() ? Qt::Checked : Qt::Unchecked);
        }
        m_tasks->setVisible(!tasks.isEmpty());

        m_entries.clear();
        const QJsonArray thread = card.value(QStringLiteral("thread")).toArray();
        for (const QJsonValue &value : thread)
            m_entries << entryMarkdown(value.toObject());
        m_streaming.clear();
        renderThread();
        m_threadLabel->setText(QStringLiteral("Thread · %1 entries")
                                   .arg(card.value(QStringLiteral("thread_total")).toInt()));
        m_loading = false;
    }

    void appendEntry(const QJsonObject &entry)
    {
        m_streaming.clear();
        m_entries << entryMarkdown(entry);
        renderThread();
    }

    void appendDelta(const QString &text)
    {
        m_streaming += text;
        renderThread();
    }

    void setBusy(bool busy)
    {
        m_ask->setEnabled(!busy);
        m_ask->setText(busy ? QStringLiteral("Thinking…") : QStringLiteral("Ask the agent"));
    }

    void focusReply() { m_reply->setFocus(); }

private:
    void submit(bool ask)
    {
        const QString text = m_reply->toPlainText().trimmed();
        if (text.isEmpty() || !onReply)
            return;
        m_reply->remember(text);
        m_reply->clear();
        onReply(text, ask);
    }

    static QString metaText(const QJsonObject &front, const QString &path)
    {
        QStringList parts;
        const auto add = [&parts, &front](const char *key, const QString &label) {
            const QJsonValue value = front.value(QLatin1String(key));
            if (value.isString() && !value.toString().isEmpty())
                parts << label + QStringLiteral(": ") + value.toString();
            else if (value.isArray() && !value.toArray().isEmpty()) {
                QStringList items;
                for (const QJsonValue &item : value.toArray())
                    items << item.toString();
                parts << label + QStringLiteral(": ") + items.join(QStringLiteral(", "));
            }
        };
        add("labels", QStringLiteral("labels"));
        add("assignee", QStringLiteral("assignee"));
        add("waiting_on", QStringLiteral("waiting on"));
        add("milestone", QStringLiteral("milestone"));
        add("component", QStringLiteral("component"));
        add("implemented_by", QStringLiteral("implemented by"));
        add("acceptance", QStringLiteral("acceptance"));
        const QJsonObject links = front.value(QStringLiteral("links")).toObject();
        for (auto it = links.begin(); it != links.end(); ++it) {
            if (!it.value().isArray() || it.value().toArray().isEmpty())
                continue;
            QStringList items;
            for (const QJsonValue &item : it.value().toArray())
                items << item.toString();
            parts << it.key() + QStringLiteral(": ") + items.join(QStringLiteral(", "));
        }
        if (!path.isEmpty())
            parts << path;
        return parts.join(QStringLiteral("  ·  "));
    }

    static QString entryMarkdown(const QJsonObject &entry)
    {
        const QJsonObject attrs = entry.value(QStringLiteral("attrs")).toObject();
        const QString author = entry.value(QStringLiteral("author")).toString(
            attrs.value(QStringLiteral("author")).toString());
        const QString kind = entry.value(QStringLiteral("kind")).toString(
            attrs.value(QStringLiteral("kind")).toString());
        const QString model = attrs.value(QStringLiteral("model")).toString();
        QString head = author == QStringLiteral("agent") ? QStringLiteral("✦ agent") : author;
        if (!model.isEmpty())
            head += QStringLiteral(" (") + model + QLatin1Char(')');
        if (!kind.isEmpty() && kind != QStringLiteral("comment"))
            head += QStringLiteral(" · ") + kind;
        return QStringLiteral("**%1**\n\n%2\n").arg(head, entry.value(QStringLiteral("text")).toString());
    }

    void renderThread()
    {
        QString text = m_entries.join(QStringLiteral("\n---\n\n"));
        if (!m_streaming.isEmpty())
            text += QStringLiteral("\n---\n\n**✦ agent**\n\n") + m_streaming;
        m_thread->setMarkdown(text);
        m_thread->verticalScrollBar()->setValue(m_thread->verticalScrollBar()->maximum());
    }

    QLabel *m_title = nullptr, *m_meta = nullptr, *m_threadLabel = nullptr;
    QComboBox *m_status = nullptr, *m_tab = nullptr;
    QToolButton *m_close = nullptr;
    QTextBrowser *m_body = nullptr, *m_thread = nullptr;
    QListWidget *m_tasks = nullptr;
    RichEditor *m_reply = nullptr;
    QPushButton *m_ask = nullptr, *m_comment = nullptr;
    QStringList m_entries;
    QString m_streaming, m_id, m_hash;
    bool m_loading = false;
};

// ------------------------------------------------------------------------ the view

BoardView::BoardView(const QString &workspace, QWidget *parent)
    : QWidget(parent), m_workspace(workspace)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    buildChrome(layout);
    setFocusPolicy(Qt::StrongFocus);
    watchIssues();
}

void BoardView::buildChrome(QVBoxLayout *layout)
{
    auto *top = new QHBoxLayout;
    top->setContentsMargins(6, 4, 6, 0);
    m_tabs = new QTabBar(this);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    top->addWidget(m_tabs, 1);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Filter — label:voice status:ready @agent waiting:me"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(360);
    top->addWidget(m_filter);

    auto *add = new QToolButton(this);
    add->setText(QStringLiteral("+"));
    add->setToolTip(QStringLiteral("New card (n)"));
    top->addWidget(add);
    layout->addLayout(top);

    m_problems = new QLabel(this);
    m_problems->setWordWrap(true);
    m_problems->setObjectName(QStringLiteral("boardProblems"));
    m_problems->hide();
    layout->addWidget(m_problems);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_scroll = new QScrollArea(m_splitter);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_columns = new QWidget(m_scroll);
    auto *columns = new QHBoxLayout(m_columns);
    columns->setContentsMargins(6, 0, 6, 6);
    columns->setSpacing(8);
    m_scroll->setWidget(m_columns);
    m_splitter->addWidget(m_scroll);

    m_detail = new CardDetail(m_splitter);
    m_detail->hide();
    m_splitter->addWidget(m_detail);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    layout->addWidget(m_splitter, 1);

    m_empty = new QLabel(QStringLiteral("Loading the Switchboard…"), this);
    m_empty->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_empty);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (m_buildingTabs || index < 0 || index >= m_model.tabs().size())
            return;
        m_tab = m_model.tabs().at(index).id;
        rebuild();
    });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model.setFilter(text);
        rebuild();
    });
    connect(add, &QToolButton::clicked, this, [this] {
        if (onHint)
            onHint(QStringLiteral("board.quickAdd"), QStringLiteral("n"));
        quickAdd();
    });
    m_filter->installEventFilter(this);

    m_detail->onClose = [this] { closeDetail(); };
    m_detail->onReply = [this](const QString &text, bool ask) {
        const QString card = m_detail->cardId();
        if (card.isEmpty())
            return;
        if (ask) {
            m_askCard = card;
            m_detail->setBusy(true);
            send({{QStringLiteral("type"), QStringLiteral("board_ask")},
                  {QStringLiteral("card"), card}, {QStringLiteral("text"), text}});
        } else {
            send({{QStringLiteral("type"), QStringLiteral("board_comment")},
                  {QStringLiteral("card"), card}, {QStringLiteral("text"), text},
                  {QStringLiteral("kind"), QStringLiteral("note")}});
        }
    };
    m_detail->onMove = [this](const QString &what, const QString &value) {
        const QString card = m_detail->cardId();
        if (card.isEmpty() || value.isEmpty())
            return;
        send({{QStringLiteral("type"), QStringLiteral("board_move")},
              {QStringLiteral("card"), card}, {what, value},
              {QStringLiteral("reason"), QStringLiteral("changed in the Switchboard")}});
    };
    m_detail->onToggleTask = [this](const QString &, bool) {
        // Phase 1 keeps the checklist read-only in the pane: ticking a box has to rewrite the
        // task line with its marker, which board_update_card's `tasks` does with the whole list.
        if (onStatus)
            onStatus(QStringLiteral("Tick tasks in the card file or ask the agent (phase 2)."));
        if (!m_detail->cardId().isEmpty())
            send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                  {QStringLiteral("card"), m_detail->cardId()}});
    };
}

// Watch `issues/` and its folders. A card write anywhere (this window, a pane agent, a
// collaborator's merge) becomes one debounced board_refresh, which the worker answers with the
// rows that actually changed.
void BoardView::watchIssues()
{
    const QString root = m_workspace + QStringLiteral("/issues");
    if (!QFileInfo::exists(root))
        return;
    m_refresh = new QTimer(this);
    m_refresh->setSingleShot(true);
    m_refresh->setInterval(400);
    connect(m_refresh, &QTimer::timeout, this, [this] {
        if (m_open)
            send({{QStringLiteral("type"), QStringLiteral("board_refresh")}});
        watchIssues();   // folders come and go as cards move between state subfolders
    });
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        const auto touched = [this](const QString &) { m_refresh->start(); };
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, touched);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, touched);
    }
    QStringList wanted{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && wanted.size() < 200)
        wanted << it.next();
    const QStringList known = m_watcher->directories();
    QStringList fresh;
    for (const QString &path : wanted)
        if (!known.contains(path))
            fresh << path;
    if (!fresh.isEmpty())
        m_watcher->addPaths(fresh);
}

void BoardView::send(const QJsonObject &message)
{
    if (onSend)
        onSend(message);
}

void BoardView::reload()
{
    send({{QStringLiteral("type"), QStringLiteral("board_open")}});
}

QString BoardView::title() const
{
    const int total = m_model.total();
    return total > 0 ? QStringLiteral("Switchboard · %1").arg(total) : QStringLiteral("Switchboard");
}

QString BoardView::currentTab() const
{
    return m_tab;
}

void BoardView::setCurrentTab(const QString &tabId)
{
    const QList<board::Tab> tabs = m_model.tabs();
    for (int i = 0; i < tabs.size(); ++i) {
        if (tabs.at(i).id != tabId)
            continue;
        m_tab = tabId;
        if (m_tabs->currentIndex() != i)
            m_tabs->setCurrentIndex(i);
        else
            rebuild();
        return;
    }
}

// ------------------------------------------------------------------------- events

void BoardView::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("board")) {
        m_open = true;
        m_model.setConfig(event.value(QStringLiteral("config")).toObject());
        m_model.reset(event.value(QStringLiteral("cards")).toArray());
        if (m_tab.isEmpty() && !m_model.tabs().isEmpty())
            m_tab = m_model.tabs().first().id;
        rebuildTabs();
        rebuild();
        showProblems(event.value(QStringLiteral("problems")).toArray());
        if (onTitleChanged)
            onTitleChanged(title());
        return;
    }
    if (type == QStringLiteral("board_changed")) {
        m_model.upsert(event.value(QStringLiteral("upserts")).toArray());
        QStringList removed;
        const QJsonArray gone = event.value(QStringLiteral("removed")).toArray();
        for (const QJsonValue &value : gone)
            removed << value.toString();
        m_model.remove(removed);
        rebuild();
        showProblems(event.value(QStringLiteral("problems")).toArray());
        if (onTitleChanged)
            onTitleChanged(title());
        // A card that is open stays in step with the file.
        if (!m_detail->cardId().isEmpty() && !event.value(QStringLiteral("upserts")).toArray().isEmpty())
            send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
                  {QStringLiteral("card"), m_detail->cardId()}});
        return;
    }
    if (type == QStringLiteral("board_card")) {
        QStringList statuses;
        const board::Tab *tab = m_model.tab(m_tab);
        const QString cardType = event.value(QStringLiteral("type")).toString(QStringLiteral("work"));
        const QList<board::Column> columns =
            m_model.columnsFor(tab && tab->type == cardType ? m_tab : m_tab);
        for (const board::Column &column : columns)
            for (const QString &status : column.statuses)
                if (!statuses.contains(status))
                    statuses << status;
        const QString current = event.value(QStringLiteral("status")).toString();
        if (!current.isEmpty() && !statuses.contains(current))
            statuses.prepend(current);
        QList<QPair<QString, QString>> tabs;
        for (const board::Tab &item : m_model.tabs())
            if (!item.folder.isEmpty())
                tabs << qMakePair(item.id, item.title);
        m_detail->setChoices(statuses, tabs);
        m_detail->show(event);
        m_detail->setVisible(true);
        return;
    }
    if (type == QStringLiteral("board_thread_appended")) {
        if (event.value(QStringLiteral("card_id")).toString() == m_detail->cardId())
            m_detail->appendEntry(event);
        return;
    }
    if (type == QStringLiteral("board_problems")) {
        showProblems(event.value(QStringLiteral("items")).toArray());
        return;
    }
    // A board_ask turn: the answer streams into the open card.
    const QString card = event.value(QStringLiteral("card_id")).toString();
    if (card.isEmpty() || card != m_detail->cardId())
        return;
    if (type == QStringLiteral("delta"))
        m_detail->appendDelta(event.value(QStringLiteral("text")).toString());
    else if (type == QStringLiteral("done") || type == QStringLiteral("error")
             || type == QStringLiteral("cancelled")) {
        m_detail->setBusy(false);
        m_askCard.clear();
        if (type != QStringLiteral("done") && onStatus)
            onStatus(event.value(QStringLiteral("text")).toString());
    }
}

void BoardView::showProblems(const QJsonArray &problems)
{
    int errors = 0;
    QString first;
    for (const QJsonValue &value : problems) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("severity")).toString() != QStringLiteral("error"))
            continue;
        ++errors;
        if (first.isEmpty())
            first = item.value(QStringLiteral("path")).toString() + QStringLiteral(": ")
                    + item.value(QStringLiteral("message")).toString();
    }
    m_problems->setVisible(errors > 0);
    if (errors > 0)
        m_problems->setText(QStringLiteral("⚠ %1 problem(s) in issues/ — %2").arg(errors).arg(first));
}

// ------------------------------------------------------------------------ rendering

void BoardView::rebuildTabs()
{
    // The caller rebuilds the columns once afterwards; currentChanged must not do it again.
    m_buildingTabs = true;
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);
    const QList<board::Tab> tabs = m_model.tabs();
    for (int i = 0; i < tabs.size(); ++i) {
        m_tabs->addTab(tabs.at(i).title);
        if (tabs.at(i).id == m_tab)
            m_tabs->setCurrentIndex(i);
    }
    m_buildingTabs = false;
    updateTabCounts();
}

void BoardView::updateTabCounts()
{
    const QList<board::Tab> tabs = m_model.tabs();
    for (int i = 0; i < tabs.size() && i < m_tabs->count(); ++i) {
        const int count = m_model.count(tabs.at(i).id);
        m_tabs->setTabText(i, count > 0 ? QStringLiteral("%1 %2").arg(tabs.at(i).title).arg(count)
                                        : tabs.at(i).title);
    }
}

QStringList BoardView::columnIds() const
{
    QStringList out;
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    for (const board::Column &column : columns)
        out << column.id;
    return out;
}

QListWidget *BoardView::listFor(const QString &columnId) const
{
    return m_lists.value(columnId);
}

QWidget *BoardView::buildColumn(const board::Column &column)
{
    auto *widget = new QWidget(m_columns);
    widget->setMinimumWidth(230);
    widget->setMaximumWidth(340);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    const QList<board::Card> cards = m_model.cards(m_tab, column.id);
    auto *header = new QLabel(QStringLiteral("%1  %2").arg(column.title).arg(cards.size()), widget);
    header->setObjectName(QStringLiteral("boardColumnHeader"));
    layout->addWidget(header);

    auto *list = new CardList(column.id, widget);
    for (const board::Card &card : cards) {
        auto *item = new QListWidgetItem(rowText(card), list);
        item->setData(kCardRole, card.id);
        item->setToolTip(QStringLiteral("#%1 · %2\n%3").arg(card.id, card.status, card.path));
        if (card.id == m_selected)
            list->setCurrentItem(item);
    }
    list->onDropped = [this, id = column.id](const QString &card, const QString &before,
                                             const QString &after) {
        if (onHint)
            onHint(QStringLiteral("board.drag"), QStringLiteral("m"));
        moveCard(card, id, before, after);
    };
    connect(list, &QListWidget::itemSelectionChanged, this, [this, list] {
        if (auto *item = list->currentItem())
            m_selected = item->data(kCardRole).toString();
    });
    connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        selectCard(item->data(kCardRole).toString());
        openSelected();
    });
    list->installEventFilter(this);
    layout->addWidget(list, 1);
    m_lists.insert(column.id, list);
    return widget;
}

void BoardView::rebuild()
{
    m_lists.clear();
    if (auto *old = m_columns->layout()) {
        QLayoutItem *item = nullptr;
        while ((item = old->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->setParent(nullptr);
                delete item->widget();
            }
            delete item;
        }
    }
    auto *layout = qobject_cast<QHBoxLayout *>(m_columns->layout());
    if (!layout)
        return;
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    for (const board::Column &column : columns)
        layout->addWidget(buildColumn(column));
    layout->addStretch();
    updateTabCounts();

    const bool empty = m_model.total() == 0;
    m_empty->setVisible(empty);
    m_empty->setText(m_open ? QStringLiteral("No cards yet. Press n to add one.")
                            : QStringLiteral("Loading the Switchboard…"));
    m_scroll->setVisible(!empty);
}

// ------------------------------------------------------------------------- actions

void BoardView::moveCard(const QString &id, const QString &columnId, const QString &beforeId,
                         const QString &afterId)
{
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("board_move")},
                        {QStringLiteral("card"), id},
                        {QStringLiteral("reason"), QStringLiteral("moved in the Switchboard")}};
    const QString status = m_model.dropStatus(m_tab, columnId);
    const board::Tab *tab = m_model.tab(m_tab);
    if (tab && tab->isFilter() && !tab->filter.contains(QStringLiteral("done")))
        message.insert(QStringLiteral("tab"), columnId);   // Deferred groups by category
    else if (!status.isEmpty())
        message.insert(QStringLiteral("status"), status);
    if (!beforeId.isEmpty())
        message.insert(QStringLiteral("before"), beforeId);
    if (!afterId.isEmpty())
        message.insert(QStringLiteral("after"), afterId);
    send(message);
}

void BoardView::quickAdd()
{
    const QStringList columns = columnIds();
    if (columns.isEmpty())
        return;
    QString columnId = columns.first();
    for (const QString &id : columns)
        if (auto *list = listFor(id); list && list->hasFocus())
            columnId = id;
    auto *list = listFor(columnId);
    if (!list)
        return;
    auto *field = new QLineEdit(list->parentWidget());
    field->setPlaceholderText(QStringLiteral("New card — the text is kept verbatim"));
    if (auto *layout = qobject_cast<QVBoxLayout *>(list->parentWidget()->layout()))
        layout->insertWidget(1, field);
    field->setFocus();
    m_quickAdd = field;
    const QString status = m_model.dropStatus(m_tab, columnId);
    const board::Tab *tab = m_model.tab(m_tab);
    const QString tabId = tab && !tab->folder.isEmpty() ? tab->id : QStringLiteral("features");
    const QString cardType = tab ? tab->type : QStringLiteral("work");
    connect(field, &QLineEdit::returnPressed, this, [this, field, status, tabId, cardType] {
        const QString text = field->text().trimmed();
        if (!text.isEmpty())
            send({{QStringLiteral("type"), QStringLiteral("board_create")},
                  {QStringLiteral("tab"), tabId},
                  {QStringLiteral("status"), status.isEmpty() ? QStringLiteral("inbox") : status},
                  {QStringLiteral("card_type"), cardType},
                  {QStringLiteral("text"), text}});
        field->clear();
        field->deleteLater();
        m_quickAdd = nullptr;
    });
    connect(field, &QLineEdit::editingFinished, this, [this, field] {
        if (m_quickAdd == field) {
            m_quickAdd = nullptr;
            field->deleteLater();
        }
    });
}

void BoardView::openSelected()
{
    if (m_selected.isEmpty())
        return;
    send({{QStringLiteral("type"), QStringLiteral("board_card_get")},
          {QStringLiteral("card"), m_selected}});
}

void BoardView::closeDetail()
{
    m_detail->hide();
    focusInput();
}

void BoardView::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

void BoardView::selectCard(const QString &id)
{
    m_selected = id;
    for (auto it = m_lists.begin(); it != m_lists.end(); ++it) {
        QListWidget *list = it.value();
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(kCardRole).toString() != id)
                continue;
            list->setCurrentItem(list->item(i));
            list->setFocus();
            return;
        }
    }
}

void BoardView::copyReference()
{
    if (m_selected.isEmpty())
        return;
    QApplication::clipboard()->setText(QStringLiteral("#") + m_selected);
    if (onStatus)
        onStatus(QStringLiteral("Copied #%1").arg(m_selected));
}

void BoardView::sendSelectionToTerminal()
{
    if (m_selected.isEmpty() || !onSendToTerminal)
        return;
    onSendToTerminal(QStringLiteral("#") + m_selected + QLatin1Char(' '));
}

void BoardView::openSelectedFile()
{
    const board::Card *card = m_model.card(m_selected);
    if (card && onOpenFile && !card->path.isEmpty())
        onOpenFile(m_workspace + QLatin1Char('/') + card->path);
}

void BoardView::moveSelected()
{
    if (m_selected.isEmpty())
        return;
    QMenu menu(this);
    const QList<board::Column> columns = m_model.columnsFor(m_tab);
    int index = 1;
    for (const board::Column &column : columns) {
        QAction *action = menu.addAction(QStringLiteral("&%1  %2").arg(index++).arg(column.title));
        const QString id = column.id;
        connect(action, &QAction::triggered, this, [this, id] { moveCard(m_selected, id, {}, {}); });
    }
    menu.addSeparator();
    for (const board::Tab &tab : m_model.tabs()) {
        if (tab.folder.isEmpty() || tab.id == m_tab)
            continue;
        QAction *action = menu.addAction(QStringLiteral("Move to %1").arg(tab.title));
        const QString id = tab.id;
        connect(action, &QAction::triggered, this, [this, id] {
            send({{QStringLiteral("type"), QStringLiteral("board_move")},
                  {QStringLiteral("card"), m_selected}, {QStringLiteral("tab"), id},
                  {QStringLiteral("reason"), QStringLiteral("moved in the Switchboard")}});
        });
    }
    menu.exec(QCursor::pos());
}

void BoardView::focusInput()
{
    const QStringList columns = columnIds();
    for (const QString &id : columns) {
        if (auto *list = listFor(id); list && list->count() > 0) {
            list->setFocus();
            if (!list->currentItem())
                list->setCurrentRow(0);
            return;
        }
    }
    m_filter->setFocus();
}

void BoardView::step(int columns, int rows)
{
    const QStringList ids = columnIds();
    int at = -1;
    for (int i = 0; i < ids.size(); ++i)
        if (auto *list = listFor(ids.at(i)); list && list->hasFocus())
            at = i;
    if (at < 0)
        return;
    if (columns != 0) {
        for (int i = at + columns; i >= 0 && i < ids.size(); i += columns) {
            auto *list = listFor(ids.at(i));
            if (!list || list->count() == 0)
                continue;
            list->setFocus();
            list->setCurrentRow(qMin(qMax(0, list->currentRow()), list->count() - 1));
            if (!list->currentItem())
                list->setCurrentRow(0);
            return;
        }
        return;
    }
    auto *list = listFor(ids.at(at));
    if (list && list->count() > 0)
        list->setCurrentRow(qBound(0, list->currentRow() + rows, list->count() - 1));
}

bool BoardView::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(object, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (object == m_filter) {
        if (key->key() == Qt::Key_Escape) {
            m_filter->clear();
            focusInput();
            return true;
        }
        if (key->key() == Qt::Key_Down) {
            focusInput();
            return true;
        }
        return QWidget::eventFilter(object, event);
    }
    auto *list = qobject_cast<QListWidget *>(object);
    if (!list)
        return QWidget::eventFilter(object, event);

    const bool plain = (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
    if (key->modifiers().testFlag(Qt::ControlModifier)
        && (key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown)) {
        const int step = key->key() == Qt::Key_PageDown ? 1 : -1;
        const int next = (m_tabs->currentIndex() + step + m_tabs->count()) % qMax(1, m_tabs->count());
        m_tabs->setCurrentIndex(next);
        return true;
    }
    if (key->modifiers().testFlag(Qt::AltModifier) && key->modifiers().testFlag(Qt::ShiftModifier)) {
        const QStringList ids = columnIds();
        const int at = ids.indexOf(columnIdOf(list));
        if (key->key() == Qt::Key_Left && at > 0) {
            moveCard(m_selected, ids.at(at - 1), {}, {});
            return true;
        }
        if (key->key() == Qt::Key_Right && at >= 0 && at + 1 < ids.size()) {
            moveCard(m_selected, ids.at(at + 1), {}, {});
            return true;
        }
        if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
            const int row = list->currentRow();
            const int target = row + (key->key() == Qt::Key_Up ? -2 : 1);
            const QString neighbour = target >= 0 && target < list->count()
                                          ? list->item(target)->data(kCardRole).toString()
                                          : QString();
            moveCard(m_selected, columnIdOf(list), {}, neighbour);
            return true;
        }
    }
    if (plain) {
        switch (key->key()) {
        case Qt::Key_Left:
            step(-1, 0);
            return true;
        case Qt::Key_Right:
            step(1, 0);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            openSelected();
            return true;
        case Qt::Key_Escape:
            if (m_detail->isVisible()) {
                closeDetail();
                return true;
            }
            if (!m_filter->text().isEmpty()) {
                m_filter->clear();
                return true;
            }
            return false;
        default:
            break;
        }
        const QString text = key->text();
        if (text == QStringLiteral("n")) {
            quickAdd();
            return true;
        }
        if (text == QStringLiteral("m")) {
            moveSelected();
            return true;
        }
        if (text == QStringLiteral("/")) {
            focusFilter();
            return true;
        }
        if (text == QStringLiteral("c")) {
            if (!m_detail->isVisible())
                openSelected();
            m_detail->focusReply();
            return true;
        }
        if (text == QStringLiteral("y")) {
            copyReference();
            return true;
        }
        if (text == QStringLiteral("t")) {
            sendSelectionToTerminal();
            return true;
        }
        if (text == QStringLiteral("o")) {
            openSelectedFile();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

}  // namespace relay
