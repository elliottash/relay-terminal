// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Switchboard's pure logic: the rows the worker sends, the sections they fall into and the
// filter language. No widgets here, so it can be tested on its own (tests/boardmodel_test.cpp).
// Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 19.
//
// The pane is one list of everything that is not done (owner decision, 2026-09-18: no tabs —
// "bug" and "feature" are labels, done is a status, and the filter box is how the list is
// sliced). `tabs()` survives only as the category folders a card can be filed into.
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace relay {
namespace board {

// One card as `board`/`board_changed` send it (protocol 19.2, "a row").
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
    // The category folder on disk (`issues/changes/2026-…md` -> "changes"), for `folder:`.
    QString folder() const;
};

// A section of the list: one status, or the several a lane collects. The first status is where a
// drop lands.
struct Column {
    QString id, title;
    QStringList statuses;
};

// A category folder from board.yaml, which is where a card's *file* lives. The pane no longer
// renders these as tabs; they are the choices in the card detail's picker and the `m` menu.
struct Tab {
    QString id, title, folder, filter;
    QString type = QStringLiteral("work");
    bool isFilter() const { return folder.isEmpty() && !filter.isEmpty(); }
};

// "in-progress" -> "In progress"; used for section headers, chips and thread lines.
QString statusTitle(const QString &status);
QString tabTitle(const QString &id);

// The mark at the head of a card row (design 4.6): one character that says the status at a
// glance, so a row is readable without reading its section header. Open shapes are early
// states, solid ones are committed, a check or a cross is closed.
QString statusGlyph(const QString &status);

// One badge on a card row (design 4.2): what it says and how the pane colours it.
struct Badge {
    enum Kind { Label, Agent, Assignee, Waiting, Status, Tasks, TasksDone, Thread, Private, Age };
    Kind kind;
    QString text;
};
// The badges a card carries, in reading order. `showStatus` is for sections that collect several
// statuses (Waiting, Needs QA, Done), where the exact one is otherwise invisible.
QList<Badge> badges(const Card &card, bool showStatus);

// The badges at the right of a card *row*: the card's badges plus how old it is, which a row has
// room for where a card box did not. `today` dates the age.
QList<Badge> rowBadges(const Card &card, bool showStatus, const QDate &today);

// Which badge a narrow row gives up first: the lowest number goes first. Labels and the thread
// count are pleasant to have; `waiting:` and the exact status are why the row is being read.
int badgeDropOrder(Badge::Kind kind);

// The badges that fit at the right of a row. `measured` is every badge with its pixel width, in
// reading order; `available` is the room the row can spare and `gap` the space between two.
// Badges are dropped in `badgeDropOrder` until the rest fit, so a narrow pane loses decoration
// before it loses meaning. Returns what is kept, still in reading order.
QList<Badge> fitBadges(const QList<QPair<Badge, int>> &measured, int available, int gap);

// The body without its leading `# Title` line when that only repeats the title: the card
// detail already shows the title in its header, so the heading would be said twice.
QString bodyWithoutTitle(const QString &body, const QString &title);

// How long ago a thread entry was written, from its sortable id (`20260918T021603Z-tg`):
// "just now", "12 min ago", "3 h ago", "yesterday", "Sep 16", or empty when the id has no time.
QString entryAge(const QString &entryId, const QDateTime &now);

// How old a card is, from its `created` date (`2026-09-17`, or an ISO timestamp): "today", "3 d",
// "5 w", "4 mo", "2 y". Short, because it sits at the right end of a row; empty when unparsable.
QString cardAge(const QString &created, const QDate &today);

// Where a card lands in a section: `order` is the section's ids top to bottom (it may contain
// `moving`), `slot` the insertion row counted in `order` *without* `moving`. Returns the id the
// card goes before and the id it goes after; either is empty at an end (protocol 19, board_move).
QPair<QString, QString> placement(const QStringList &order, const QString &moving, int slot);

// One line of the pane's single scrolling list (design 4.6): either a section header for a status
// or one card. The view paints these and holds nothing else, so what the list shows is decided
// here and can be tested without a widget.
struct Row {
    enum Kind { Section, Card };
    Kind kind = Card;
    QString columnId;         // both kinds: the section this row belongs to
    QString title;            // Section: the status name
    QString cardId;           // Card: the card
    int count = 0;            // Section: how many cards it holds, after the filter
    bool collapsed = false;   // Section: its cards are not in the list
    bool showStatus = false;  // Card: its section holds several statuses, so the row names this one
};

// The card rows of one section, top to bottom.
QStringList cardsInSection(const QList<Row> &rows, const QString &columnId);

// Where a drop lands: `beforeRow` is the row index the card would be inserted in front of
// (0 … rows.size()). The point just above a section header belongs to the section above it, so a
// line drawn anywhere inside a section drops into that section. Returns its id and the slot
// inside it, counted in card rows.
QPair<QString, int> dropTarget(const QList<Row> &rows, int beforeRow);

// The next card row at or past `from` in direction `delta`, skipping section headers; -1 when
// there is none, so Up/Down walks the whole list across section breaks and stops at its ends.
int stepRow(const QList<Row> &rows, int from, int delta);

// The row index of a card, or of a section header, or -1.
int rowOfCard(const QList<Row> &rows, const QString &cardId);
int rowOfSection(const QList<Row> &rows, const QString &columnId);

// The section that holds the closed cards. It is always the last one, and the pane folds it by
// default: done is a status, not a place (owner decision, 2026-09-18).
QString doneSection();

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
    // The category folders a card's file can live in, as board.yaml lists them.
    QList<Tab> tabs() const { return m_tabs; }
    const Tab *tab(const QString &id) const;
    // The sections of the list, top to bottom: the configured columns, then any status they do
    // not collect (a plan's Draft, a Deferred card), then Done.
    QList<Column> sections() const;
    // The status a card takes when it is dropped on this section.
    QString dropStatus(const QString &columnId) const;

    // ---- cards
    const Card *card(const QString &id) const;
    QList<Card> cards(const QString &columnId) const;   // filtered, ordered
    // Which section this card belongs in, or empty when no section collects its status.
    QString sectionOf(const Card &card) const;
    int openCount() const;                               // filtered, not done or dropped
    int total() const { return m_cards.size(); }
    QStringList allLabels() const;
    QStringList allIds() const;
    // The rows of the list: a header per section, then its cards unless the section is in
    // `collapsed`. A section with no matches is left out while a filter is active, and nothing
    // is folded then — a search that hid its own matches would be a search that does nothing.
    QList<Row> rows(const QSet<QString> &collapsed) const;

    // ---- filtering
    void setFilter(const QString &text);
    QString filter() const { return m_filter; }
    // `label:voice status:ready folder:changes @agent waiting:me some words`; every term matches.
    static bool matches(const Card &card, const QString &filter);
    // Fuzzy ranking for the composer's `#` picker and the pane's search, best first.
    QList<Card> search(const QString &query, int limit = 20) const;
    static int score(const QString &query, const Card &card);

private:
    QList<Card> sorted(QList<Card> cards, bool newestFirst) const;
    // status -> the section that collects it, built once per query.
    QMap<QString, QString> sectionIndex(const QList<Column> &sections) const;

    QList<Tab> m_tabs;
    QStringList m_columns;                       // configured work columns, in order
    QMap<QString, QStringList> m_columnStatuses; // column id -> statuses (from board.yaml)
    QMap<QString, Card> m_cards;                 // by id
    QString m_filter;
};

}  // namespace board
}  // namespace relay
