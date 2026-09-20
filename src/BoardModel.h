// SPDX-License-Identifier: AGPL-3.0-or-later
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
    // When the card last changed on disk — its file's mtime, or its thread file's if that is
    // later — as an ISO timestamp from the worker (protocol 19.2 `updated`). Empty from an older
    // worker or a card with no file, and then the RecentlyUpdated sort falls back to `created`.
    QString updated;
    // The signature of the model that closed this card out of a QA lane, stamped by the worker
    // (#T71W). Empty on everything else, and that is what puts a `done` card in Verified rather
    // than in Done — the section is derived, not a status of its own.
    QString verifiedBy;
    // The card's whole text — body, then each thread entry — sent by the worker (protocol 19.2
    // `text`, capped at 64 KiB there) so the filter's plain words search the whole card, not
    // only the title (owner, 2026-09-19: "switchboard filter bar should be full text search").
    // Not for drawing: the card detail reads the real body.
    QString text;
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

// ---- sorting -----------------------------------------------------------------
//
// How the cards inside each section are ordered (owner, 2026-09-19: "add sorting options,
// especially by time"; then 2026-09-19: "change switchboard sorting from a sort button to adding
// header columns that you click on ... and sorting is within section"). The sort is the list's
// column header (below): a click on Card, Created or Updated orders the cards *inside every
// section* by that column. `Manual` is the board's own rank — the order drags and Alt+Shift+↑↓
// write — and the closed sections stay newest first under it, as they always were. Any other
// order takes the manual reorder off (a rank nobody can see is a rank nobody can write), so what
// is on screen and what a drag would say never disagree.
enum class Sort { Manual, NewestFirst, OldestFirst, RecentlyUpdated, OldestUpdated, TitleAsc,
                  TitleDesc };
// The id the pane's layout node keeps ("manual", "newest", "oldest", "updated", "updated-oldest",
// "title", "title-desc") and back; an unknown id reads as Manual, so a saved pane survives a sort
// being renamed away.
QString sortId(Sort sort);
Sort sortFromId(const QString &id);
// "Manual", "Newest first", "Oldest first", "Recently updated", "Least recently updated",
// "Title A→Z", "Title Z→A" — the word for what is on, in a notice or a tooltip.
QString sortTitle(Sort sort);

// ---- the list's columns (owner, 2026-09-19: "add a 'created' and 'updated' column") ----------
//
// The header row over the list, left to right: the card itself, then when it was created and when
// it last changed. Each is a sort, and a click cycles that column's own orders and then back to
// Manual, so the board's drag order is always one click away.
enum class SortColumn { Card, Created, Updated };
// "Card", "Created", "Updated" — the header's word for a column.
QString columnTitle(SortColumn column);
// The order a click on this column's header puts the list in, given the sort that is on.
Sort nextColumnSort(SortColumn column, Sort current);
// Which column is showing its arrow, or -1 when the list is in Manual (no column is active).
int sortColumnIndex(Sort sort);
// Whether a sort runs ascending — oldest first, A→Z, least recently updated — so its arrow points
// up. Manual has no arrow and answers false.
bool sortAscending(Sort sort);
// A date column's text: the date part of a card's `created` or `updated`, so an ISO timestamp and
// a bare date both read "2026-09-19", and empty when the field holds nothing that looks like a
// date — the cell is then blank rather than wrong.
QString dateCell(const QString &stamp);

// What a section collects when `board.yaml` does not override it (`column_statuses:`), which is
// `board.COLUMN_STATUSES` on the worker's side. The gear writes an override only where the board
// wants something else, so a board.yaml stays as short as the board is ordinary.
QStringList defaultSectionStatuses(const QString &column);

// One clause saying what a section is for ("ready" -> "agreed and not started…"), shown wherever
// a section is named without its cards. Empty for an id with no definition, so a board that
// configures a column of its own gets no invented explanation.
QString sectionMeaning(const QString &id);

QString tabTitle(const QString &id);

// The `## ` section holding what the card is about, in the words of whoever asked for it. It
// was called "Request" until 2026-09-18 (owner: "i'm not sure about 'request' there, let's call
// it issue"); the worker reads both spellings and settles a card on this one when it writes it,
// so no existing card file has to be rewritten.
QString issueHeading();

// The mark at the head of a card row (design 4.6): one character that says the status at a
// glance, so a row is readable without reading its section header. Open shapes are early
// states, solid ones are committed, a check or a cross is closed.
QString statusGlyph(const QString &status);

// One badge on a card row (design 4.2): what it says and how the pane colours it.
struct Badge {
    enum Kind { Label, Agent, Assignee, Waiting, Status, Tasks, TasksDone, Thread, Private, Age,
                Verified };
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

// A card turn's mode as the thread shows it (protocol 19.10, #XS6Q): "discuss" -> "Discuss",
// "plan" -> "Plan", "execute" -> "Execute"; empty for anything else, so an entry written before
// the modes existed reads as it always did.
QString modeTitle(const QString &mode);

// A thread entry's Markdown as the card detail shows it. The file keeps GitHub's
// `<details><summary>before</summary> … </details>` around a rewrite's old and new text, which
// QTextDocument's Markdown reader drops the tags of — the labels then came out *under* the text
// they name ("before" beneath the old title). Here each summary becomes a bold label above its
// block and the tags go; a rewrite's leading "- " bullet goes too, as an event's does.
QString threadMarkdown(const QString &text, const QString &kind);

// What a terminal pane's agent is handed when the owner presses Execute on a card (#XS6Q). The
// card itself (issue, plan, acceptance, thread tail) travels as `ask {cards: [id]}`, so this is
// the instruction around it: the card id, what to do, and the board's conventions for the work —
// `implemented_by`, `#ID` in each commit message and `links.commits`, and the QA lane at the end.
// `note` is whatever the owner had typed in the card's reply box, passed on verbatim.
QString executeTask(const QString &id, const QString &title, bool hasPlan, bool hasAcceptance,
                    const QString &note = QString());

// ---- cross-provider QA (#T71W) --------------------------------------------------------------
//
// The worker puts a `qa` object on the `board_card_get` answer of every work card that has an
// `implemented_by`: who implemented it, the one verifier it recommends, the alternates, and why
// each family was skipped or is unavailable (the card's "Where it shows"; availability is the
// worker's to compute, never the GUI's). These four read that object; they hold no policy of
// their own, so the ranking can change in `backend/relay_core/qa_verifiers.py` alone.

// A vendor family as a card line names it: "anthropic" -> "Claude", "openai" -> "OpenAI",
// "relay-free" -> "Relay Free". An entry's own `label` wins where it has one.
QString familyLabel(const QString &family);

// A signature as a badge or a fields line names it, short (owner, 2026-09-19: "lets try to record
// the model used", so these say the *model*, not only its vendor):
//   "openai/codex"                            -> "Codex"
//   "glm/glm-5.3"                             -> "GLM-5.3"
//   "anthropic/claude-opus-5 via claude-code" -> "Claude Opus 5 · Claude Code"
// A trailing parenthetical is free text and is ignored, as the worker's own reader ignores it.
// Only the capitalisation is invented: an acronym goes upper case and keeps the hyphen to its
// version, everything else keeps the model id's own words, so a model this file has never heard
// of still comes out readable and nothing has to be added here when one ships.
QString signatureLabel(const QString &signature);

// The recommendation's runner id — "guest:codex", "guest:claude" or "preset:<id>" — or empty
// when nothing is available. This is what Verify opens a pane on.
QString verifyRunner(const QJsonObject &qa);
// The recommendation's label ("Codex"), for the thread note and the brief; empty with no
// recommendation.
QString verifyLabel(const QJsonObject &qa);

// The one line a card in a QA lane shows under its fields:
//   "Verify with Codex (installed) · then GLM-5.3 · Claude skipped: implemented this card"
// and, when no verifier is available at all:
//   "No verifier available: Kimi: no key · Codex: not installed"
// Empty for a card the worker sent no `qa` for.
QString verifyLine(const QJsonObject &qa);

// The worker's `note` on the recommendation, when it sent one: the whole story when there is no
// verifier at all ("Verifying is not available on Relay Free…"), and a warning beside the line
// when the best this machine can do is a weak check (a same-lineage verifier, a local model).
// The card shows it in the amber that means "a human should look", never as an ordinary field.
QString verifyNote(const QJsonObject &qa);

// What the verifier's pane is handed when Verify is pressed on a card in a QA lane. The card
// travels with it (`ask {cards: [id]}`) for a preset runner; a guest CLI gets this text alone as
// its first prompt, so the brief says where to find the card as well as what to do with it.
// `verifier` is the recommendation's label, `implementedBy` the card's signature, and `note`
// whatever the owner had typed in the reply box, passed on verbatim.
QString verifyTask(const QString &id, const QString &title, const QString &verifier,
                   const QString &implementedBy, const QString &note = QString());

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

// The section above it (owner, 2026-09-19: "so we need a Verified section in the switchboard?").
// Derived, not a status: a `done` card the worker signed `verified_by` sits here, and Done keeps
// the rest — a card closed without a cross-model check, and every dropped one. Nothing can be
// moved *into* it, because the only way in is closing a card out of a QA lane.
QString verifiedSection();

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
    // What this board calls a section: `column_titles:` when it names one, Relay's own wording
    // otherwise. The id never changes, so this is the only place a section's name comes from.
    QString sectionTitle(const QString &id) const;
    // Every status a section may collect, as the worker listed them: what the gear offers when
    // a new section is given something to hold.
    QStringList statusChoices() const;
    // The section list exactly as board.yaml configures it, which is what the gear edits: the
    // ordered ids, and the statuses each one overrides. `sections()` is the drawn list — it also
    // holds the lanes a card's status appends and the two that are always last.
    QStringList columns() const { return m_columns; }
    QMap<QString, QStringList> columnStatuses() const { return m_columnStatuses; }
    QMap<QString, QString> columnTitles() const { return m_columnTitles; }

    // ---- cards
    const Card *card(const QString &id) const;
    QList<Card> cards(const QString &columnId) const;   // filtered, ordered
    // The order the cards inside a section come in (above). `rows()` and `cards()` both follow it.
    void setSort(Sort sort) { m_sort = sort; }
    Sort sort() const { return m_sort; }
    // Which section this card belongs in, or empty when no section collects its status.
    QString sectionOf(const Card &card) const;
    int openCount() const;                               // filtered, not done or dropped
    // How many open cards the section checkboxes are keeping out of the list: filtered, not
    // closed, and in a section `hidden` names. `openCount() - hiddenCount()` is what is on
    // screen, so the count label can say "62 of 84 open" without counting rows twice.
    int hiddenCount(const QSet<QString> &hidden) const;
    int total() const { return m_cards.size(); }
    QStringList allLabels() const;
    QStringList allIds() const;
    // The rows of the list: a header per section, then its cards unless the section is in
    // `collapsed`. A section with no matches is left out while a filter is active, and nothing
    // is folded then — a search that hid its own matches would be a search that does nothing.
    // A section in `hidden` (its checkbox at the top of the list page is unticked) is left out
    // header and all, whether or not a filter is active: the two compose.
    QList<Row> rows(const QSet<QString> &collapsed, const QSet<QString> &hidden = {}) const;

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
    QMap<QString, QString> m_columnTitles;       // column id -> the board's own name for it
    QStringList m_statusChoices;                 // every status a section may collect
    QMap<QString, Card> m_cards;                 // by id
    QString m_filter;
    Sort m_sort = Sort::Manual;
};

}  // namespace board
}  // namespace relay
