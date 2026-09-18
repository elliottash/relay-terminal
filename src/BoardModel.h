// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Switchboard's pure logic: the rows the worker sends, the tabs and columns they fall into,
// and the filter language. No widgets here, so it can be tested on its own
// (tests/boardmodel_test.cpp). Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 17.
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

namespace relay {
namespace board {

// One card as `board`/`board_changed` send it (protocol 17.2, "a row").
struct Card {
    QString id, title, status, tab, assignee, waitingOn, rank, path, implementedBy, milestone,
            created, topic;
    QString type = QStringLiteral("work");
    QStringList labels;
    int threadEntries = 0, tasksDone = 0, tasksTotal = 0;
    bool isPrivate = false;
    bool unread = false;   // local, from QSettings; never in git

    static Card fromJson(const QJsonObject &object);
    bool closed() const;                 // done or dropped
    bool parked() const;                 // deferred
    QString reference() const { return QStringLiteral("#") + id; }
};

// A column of the board. `statuses` is what it collects; the first one is where a drop lands.
struct Column {
    QString id, title;
    QStringList statuses;
};

// A tab: a category folder, or a filter across all of them. `type` is the card type it shows.
struct Tab {
    QString id, title, folder, filter;
    QString type = QStringLiteral("work");
    bool isFilter() const { return folder.isEmpty() && !filter.isEmpty(); }
};

// "in-progress" -> "In progress"; used for column headers, chips and thread lines.
QString statusTitle(const QString &status);
QString tabTitle(const QString &id);

// One badge in a card's footer (design 4.2): what it says and how the pane colours it.
struct Badge {
    enum Kind { Label, Agent, Assignee, Waiting, Status, Tasks, TasksDone, Thread, Private };
    Kind kind;
    QString text;
};
// The badges a card shows under its title, in reading order. `showStatus` is for columns that
// collect several statuses (Waiting, Needs QA), where the exact one is otherwise invisible.
QList<Badge> badges(const Card &card, bool showStatus);

// The body without its leading `# Title` line when that only repeats the title: the card
// detail already shows the title in its header, so the heading would be said twice.
QString bodyWithoutTitle(const QString &body, const QString &title);

// How long ago a thread entry was written, from its sortable id (`20260918T021603Z-tg`):
// "just now", "12 min ago", "3 h ago", "yesterday", "Sep 16", or empty when the id has no time.
QString entryAge(const QString &entryId, const QDateTime &now);

// Where a card lands in a column: `order` is the column's ids top to bottom (it may contain
// `moving`), `slot` the insertion row counted in `order` *without* `moving`. Returns the id the
// card goes before and the id it goes after; either is empty at an end (protocol 17, board_move).
QPair<QString, QString> placement(const QStringList &order, const QString &moving, int slot);

class Model {
public:
    // ---- data in
    void setConfig(const QJsonObject &config);
    void reset(const QJsonArray &cards);
    void upsert(const QJsonArray &cards);
    void upsert(const Card &card);
    void remove(const QStringList &ids);
    void clear();

    // ---- structure
    QList<Tab> tabs() const { return m_tabs; }
    const Tab *tab(const QString &id) const;
    QList<Column> columnsFor(const QString &tabId) const;
    // The status a card takes when it is dropped on this column.
    QString dropStatus(const QString &tabId, const QString &columnId) const;

    // ---- cards
    const Card *card(const QString &id) const;
    QList<Card> cards(const QString &tabId, const QString &columnId) const;  // filtered, ordered
    int count(const QString &tabId) const;                                   // filtered
    int total() const { return m_cards.size(); }
    QStringList allLabels() const;
    QStringList allIds() const;
    // Which column of `tabId` this card belongs in, or empty when the tab does not show it.
    QString columnOf(const QString &tabId, const Card &card) const;

    // ---- filtering
    void setFilter(const QString &text);
    QString filter() const { return m_filter; }
    // `label:voice status:ready @agent waiting:me some words`; every term must match.
    static bool matches(const Card &card, const QString &filter);
    // Fuzzy ranking for the composer's `#` picker and the pane's search, best first.
    QList<Card> search(const QString &query, int limit = 20) const;
    static int score(const QString &query, const Card &card);

private:
    bool showsInTab(const Tab &tab, const Card &card) const;
    QList<Card> sorted(QList<Card> cards, const QString &tabId) const;

    QList<Tab> m_tabs;
    QStringList m_columns;                       // configured work columns, in order
    QMap<QString, QStringList> m_columnStatuses; // column id -> statuses (from board.yaml)
    QMap<QString, Card> m_cards;                 // by id
    QString m_filter;
};

}  // namespace board
}  // namespace relay
