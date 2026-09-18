// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardModel.h"

#include <QJsonValue>
#include <algorithm>

namespace relay {
namespace board {
namespace {

QStringList stringList(const QJsonValue &value)
{
    QStringList out;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        const QString text = item.toString();
        if (!text.isEmpty())
            out << text;
    }
    return out;
}

// The columns a plan or memory tab shows. Work columns come from board.yaml instead.
QList<Column> typeColumns(const QString &type)
{
    if (type == QStringLiteral("plan"))
        return {{QStringLiteral("draft"), QStringLiteral("Draft"), {QStringLiteral("draft")}},
                {QStringLiteral("approved"), QStringLiteral("Approved"), {QStringLiteral("approved")}},
                {QStringLiteral("executing"), QStringLiteral("Executing"), {QStringLiteral("executing")}},
                {QStringLiteral("done"), QStringLiteral("Done"),
                 {QStringLiteral("done"), QStringLiteral("dropped")}}};
    if (type == QStringLiteral("memory"))
        return {{QStringLiteral("active"), QStringLiteral("Active"), {QStringLiteral("active")}},
                {QStringLiteral("retired"), QStringLiteral("Retired"), {QStringLiteral("retired")}}};
    return {};
}

// Used when the worker sends no column_statuses (an old board.yaml, or a test fixture).
QStringList fallbackStatuses(const QString &column)
{
    if (column == QStringLiteral("waiting"))
        return {QStringLiteral("needs-review"), QStringLiteral("needs-labels"), QStringLiteral("needs-ab")};
    if (column == QStringLiteral("needs-qa"))
        return {QStringLiteral("needs-qa-llm"), QStringLiteral("needs-qa-human")};
    if (column == QStringLiteral("done"))
        return {QStringLiteral("done"), QStringLiteral("dropped")};
    return {column};
}

bool containsCaseless(const QStringList &list, const QString &value)
{
    for (const QString &item : list)
        if (item.compare(value, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// Subsequence match, the same shape as the composer's `@` picker: every character of the
// query appears in order, and a run of adjacent characters scores higher.
int fuzzy(const QString &query, const QString &text)
{
    if (query.isEmpty())
        return 1;
    int points = 0, run = 0, at = 0;
    for (int i = 0; i < text.size() && at < query.size(); ++i) {
        if (text.at(i).toLower() != query.at(at).toLower()) {
            run = 0;
            continue;
        }
        ++at;
        ++run;
        points += 1 + run;
        if (i == 0 || text.at(i - 1) == QLatin1Char(' ') || text.at(i - 1) == QLatin1Char('-'))
            points += 3;   // a word start
    }
    return at == query.size() ? points : 0;
}

}  // namespace

QString statusTitle(const QString &status)
{
    static const QMap<QString, QString> names{
        {QStringLiteral("inbox"), QStringLiteral("Inbox")},
        {QStringLiteral("discussing"), QStringLiteral("Discussing")},
        {QStringLiteral("ready"), QStringLiteral("Ready")},
        {QStringLiteral("in-progress"), QStringLiteral("In progress")},
        {QStringLiteral("needs-review"), QStringLiteral("Needs review")},
        {QStringLiteral("needs-labels"), QStringLiteral("Needs labels")},
        {QStringLiteral("needs-ab"), QStringLiteral("Needs A/B")},
        {QStringLiteral("needs-qa"), QStringLiteral("Needs QA")},
        {QStringLiteral("needs-qa-llm"), QStringLiteral("Needs QA (LLM)")},
        {QStringLiteral("needs-qa-human"), QStringLiteral("Needs QA (human)")},
        {QStringLiteral("deferred"), QStringLiteral("Deferred")},
        {QStringLiteral("done"), QStringLiteral("Done")},
        {QStringLiteral("dropped"), QStringLiteral("Dropped")},
        {QStringLiteral("waiting"), QStringLiteral("Waiting")},
        {QStringLiteral("draft"), QStringLiteral("Draft")},
        {QStringLiteral("approved"), QStringLiteral("Approved")},
        {QStringLiteral("executing"), QStringLiteral("Executing")},
        {QStringLiteral("active"), QStringLiteral("Active")},
        {QStringLiteral("retired"), QStringLiteral("Retired")}};
    const QString known = names.value(status);
    if (!known.isEmpty())
        return known;
    QString text = status;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString tabTitle(const QString &id)
{
    if (id == QStringLiteral("planning"))
        return QStringLiteral("Plans");
    QString text = id;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

Card Card::fromJson(const QJsonObject &object)
{
    Card card;
    card.id = object.value(QStringLiteral("id")).toString();
    card.title = object.value(QStringLiteral("title")).toString();
    card.type = object.value(QStringLiteral("type")).toString(QStringLiteral("work"));
    card.status = object.value(QStringLiteral("status")).toString();
    card.tab = object.value(QStringLiteral("tab")).toString();
    card.assignee = object.value(QStringLiteral("assignee")).toString();
    card.waitingOn = object.value(QStringLiteral("waiting_on")).toString();
    card.rank = object.value(QStringLiteral("rank")).toString();
    card.path = object.value(QStringLiteral("path")).toString();
    card.implementedBy = object.value(QStringLiteral("implemented_by")).toString();
    card.milestone = object.value(QStringLiteral("milestone")).toString();
    card.created = object.value(QStringLiteral("created")).toString();
    card.topic = object.value(QStringLiteral("topic")).toString();
    card.labels = stringList(object.value(QStringLiteral("labels")));
    card.threadEntries = object.value(QStringLiteral("thread_entries")).toInt();
    card.tasksDone = object.value(QStringLiteral("tasks_done")).toInt();
    card.tasksTotal = object.value(QStringLiteral("tasks_total")).toInt();
    card.isPrivate = object.value(QStringLiteral("private")).toBool();
    return card;
}

bool Card::closed() const
{
    return status == QStringLiteral("done") || status == QStringLiteral("dropped");
}

bool Card::parked() const
{
    return status == QStringLiteral("deferred");
}

// --------------------------------------------------------------------------- config

void Model::setConfig(const QJsonObject &config)
{
    m_tabs.clear();
    m_columns.clear();
    m_columnStatuses.clear();

    const QJsonObject statuses = config.value(QStringLiteral("column_statuses")).toObject();
    for (auto it = statuses.begin(); it != statuses.end(); ++it)
        m_columnStatuses.insert(it.key(), stringList(it.value()));

    m_columns = stringList(config.value(QStringLiteral("columns")));
    if (m_columns.isEmpty())
        m_columns = QStringList{QStringLiteral("inbox"), QStringLiteral("discussing"),
                                QStringLiteral("ready"), QStringLiteral("in-progress"),
                                QStringLiteral("waiting"), QStringLiteral("needs-qa"),
                                QStringLiteral("done")};

    const QJsonArray tabs = config.value(QStringLiteral("tabs")).toArray();
    bool haveMemory = false;
    for (const QJsonValue &value : tabs) {
        const QJsonObject item = value.toObject();
        Tab tab;
        tab.id = item.value(QStringLiteral("id")).toString();
        if (tab.id.isEmpty())
            continue;
        tab.folder = item.value(QStringLiteral("folder")).toString();
        tab.filter = item.value(QStringLiteral("filter")).toString();
        tab.title = item.value(QStringLiteral("title")).toString(tabTitle(tab.id));
        // Plans and memory are card types with their own statuses and view, not work columns
        // (TASKS-AND-MEMORY-DESIGN section 9, "One object model").
        if (tab.folder == QStringLiteral("planning"))
            tab.type = QStringLiteral("plan");
        else if (tab.folder == QStringLiteral("memory"))
            tab.type = QStringLiteral("memory");
        haveMemory = haveMemory || tab.type == QStringLiteral("memory");
        m_tabs << tab;
    }
    if (!haveMemory) {
        // Memory cards exist in the format whether or not board.yaml lists a tab for them.
        Tab memory;
        memory.id = QStringLiteral("memory");
        memory.title = QStringLiteral("Memory");
        memory.folder = QStringLiteral("memory");
        memory.type = QStringLiteral("memory");
        m_tabs << memory;
    }
}

const Tab *Model::tab(const QString &id) const
{
    for (const Tab &tab : m_tabs)
        if (tab.id == id)
            return &tab;
    return nullptr;
}

QList<Column> Model::columnsFor(const QString &tabId) const
{
    const Tab *tab = this->tab(tabId);
    if (!tab)
        return {};
    if (tab->type == QStringLiteral("memory")) {
        // Memory is one list per topic, not a board of work columns
        // (TASKS-AND-MEMORY-DESIGN section 9).
        QStringList topics;
        bool retired = false;
        for (const Card &card : m_cards) {
            if (card.type != QStringLiteral("memory"))
                continue;
            if (card.status == QStringLiteral("retired")) {
                retired = true;
                continue;
            }
            const QString topic = card.topic.isEmpty() ? QStringLiteral("general") : card.topic;
            if (!topics.contains(topic))
                topics << topic;
        }
        topics.sort();
        QList<Column> out;
        for (const QString &topic : topics)
            out << Column{QStringLiteral("topic:") + topic, tabTitle(topic), {QStringLiteral("active")}};
        if (out.isEmpty())
            out << Column{QStringLiteral("topic:general"), QStringLiteral("General"),
                          {QStringLiteral("active")}};
        if (retired)
            out << Column{QStringLiteral("retired"), QStringLiteral("Retired"),
                          {QStringLiteral("retired")}};
        return out;
    }
    if (tab->type != QStringLiteral("work"))
        return typeColumns(tab->type);
    if (tab->isFilter()) {
        // Deferred groups by category; Done is one newest-first column (design section 3).
        if (tab->filter.contains(QStringLiteral("done"))) {
            return {{QStringLiteral("done"), QStringLiteral("Done"),
                     {QStringLiteral("done"), QStringLiteral("dropped")}}};
        }
        QList<Column> out;
        for (const Tab &other : m_tabs) {
            if (other.folder.isEmpty() || other.type != QStringLiteral("work"))
                continue;
            out << Column{other.id, other.title, {QStringLiteral("deferred")}};
        }
        return out;
    }
    QList<Column> out;
    for (const QString &id : m_columns) {
        QStringList statuses = m_columnStatuses.value(id);
        if (statuses.isEmpty())
            statuses = fallbackStatuses(id);
        out << Column{id, statusTitle(id), statuses};
    }
    return out;
}

QString Model::dropStatus(const QString &tabId, const QString &columnId) const
{
    const QList<Column> columns = columnsFor(tabId);
    for (const Column &column : columns)
        if (column.id == columnId)
            return column.statuses.value(0);
    return QString();
}

// ---------------------------------------------------------------------------- cards

void Model::reset(const QJsonArray &cards)
{
    m_cards.clear();
    upsert(cards);
}

void Model::upsert(const QJsonArray &cards)
{
    for (const QJsonValue &value : cards)
        upsert(Card::fromJson(value.toObject()));
}

void Model::upsert(const Card &card)
{
    if (!card.id.isEmpty())
        m_cards.insert(card.id, card);
}

void Model::remove(const QStringList &ids)
{
    for (const QString &id : ids)
        m_cards.remove(id);
}

void Model::clear()
{
    m_cards.clear();
}

const Card *Model::card(const QString &id) const
{
    auto it = m_cards.constFind(id.toUpper());
    return it == m_cards.constEnd() ? nullptr : &it.value();
}

bool Model::showsInTab(const Tab &tab, const Card &card) const
{
    if (card.type != tab.type)
        return false;
    if (tab.isFilter()) {
        if (tab.filter.contains(QStringLiteral("deferred")))
            return card.parked();
        return card.closed();
    }
    if (tab.type == QStringLiteral("memory") || tab.type == QStringLiteral("plan"))
        return true;      // one tab per type; the type check above is the whole test
    if (card.tab != tab.id && !(tab.folder == card.tab))
        return false;
    // A category tab shows the live work; Deferred and Done have their own tabs.
    if (tab.type == QStringLiteral("work"))
        return !card.parked() && !card.closed();
    return true;
}

QString Model::columnOf(const QString &tabId, const Card &card) const
{
    const Tab *tab = this->tab(tabId);
    if (!tab || !showsInTab(*tab, card))
        return QString();
    if (tab->type == QStringLiteral("memory")) {
        if (card.status == QStringLiteral("retired"))
            return QStringLiteral("retired");
        return QStringLiteral("topic:")
               + (card.topic.isEmpty() ? QStringLiteral("general") : card.topic);
    }
    const QList<Column> columns = columnsFor(tabId);
    for (const Column &column : columns) {
        if (tab->isFilter() && !tab->filter.contains(QStringLiteral("done"))) {
            if (column.id == card.tab)
                return column.id;
            continue;
        }
        if (column.statuses.contains(card.status))
            return column.id;
    }
    return QString();
}

QList<Card> Model::sorted(QList<Card> cards, const QString &tabId) const
{
    const Tab *tab = this->tab(tabId);
    const bool newestFirst = tab && tab->isFilter() && tab->filter.contains(QStringLiteral("done"));
    std::sort(cards.begin(), cards.end(), [newestFirst](const Card &a, const Card &b) {
        if (newestFirst && a.created != b.created)
            return a.created > b.created;
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.path < b.path;
    });
    return cards;
}

QList<Card> Model::cards(const QString &tabId, const QString &columnId) const
{
    QList<Card> out;
    for (const Card &card : m_cards) {
        if (columnOf(tabId, card) != columnId)
            continue;
        if (!matches(card, m_filter))
            continue;
        out << card;
    }
    return sorted(out, tabId);
}

int Model::count(const QString &tabId) const
{
    const Tab *tab = this->tab(tabId);
    if (!tab)
        return 0;
    int total = 0;
    for (const Card &card : m_cards)
        if (showsInTab(*tab, card) && matches(card, m_filter))
            ++total;
    return total;
}

QStringList Model::allLabels() const
{
    QStringList out;
    for (const Card &card : m_cards)
        for (const QString &label : card.labels)
            if (!out.contains(label))
                out << label;
    out.sort();
    return out;
}

QStringList Model::allIds() const
{
    QStringList out = m_cards.keys();
    out.sort();
    return out;
}

// ------------------------------------------------------------------------ filtering

void Model::setFilter(const QString &text)
{
    m_filter = text.trimmed();
}

bool Model::matches(const Card &card, const QString &filter)
{
    const QString trimmed = filter.trimmed();
    if (trimmed.isEmpty())
        return true;
    const QStringList terms = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &raw : terms) {
        const QString term = raw.trimmed();
        if (term.isEmpty())
            continue;
        if (term.startsWith(QStringLiteral("label:"))) {
            if (!containsCaseless(card.labels, term.mid(6)))
                return false;
        } else if (term.startsWith(QStringLiteral("status:"))) {
            if (card.status.compare(term.mid(7), Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QStringLiteral("waiting:"))) {
            const QString who = term.mid(8);
            const QString wanted = who == QStringLiteral("me") ? QStringLiteral("owner") : who;
            if (card.waitingOn.compare(wanted, Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QLatin1Char('@'))) {
            if (card.assignee.compare(term.mid(1), Qt::CaseInsensitive) != 0)
                return false;
        } else if (term.startsWith(QLatin1Char('#'))) {
            if (card.id.compare(term.mid(1), Qt::CaseInsensitive) != 0)
                return false;
        } else {
            const QString haystack = card.id + QLatin1Char(' ') + card.title + QLatin1Char(' ')
                                     + card.labels.join(QLatin1Char(' ')) + QLatin1Char(' ')
                                     + card.assignee + QLatin1Char(' ') + card.milestone;
            if (!haystack.contains(term, Qt::CaseInsensitive))
                return false;
        }
    }
    return true;
}

int Model::score(const QString &query, const Card &card)
{
    if (query.isEmpty())
        return card.closed() ? 1 : 100;
    if (card.id.compare(query, Qt::CaseInsensitive) == 0)
        return 100000;
    const int title = fuzzy(query, card.title);
    const int id = fuzzy(query, card.id);
    if (title == 0 && id == 0)
        return 0;
    // Open cards first (design section 5): a closed card has to earn its place.
    return title * 3 + id * 4 + (card.closed() ? 0 : 500);
}

QList<Card> Model::search(const QString &query, int limit) const
{
    QList<QPair<int, Card>> scored;
    for (const Card &card : m_cards) {
        const int points = score(query, card);
        if (points > 0)
            scored << qMakePair(points, card);
    }
    std::sort(scored.begin(), scored.end(), [](const QPair<int, Card> &a, const QPair<int, Card> &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second.id < b.second.id;
    });
    QList<Card> out;
    for (const auto &item : scored) {
        if (out.size() >= limit)
            break;
        out << item.second;
    }
    return out;
}

}  // namespace board
}  // namespace relay
