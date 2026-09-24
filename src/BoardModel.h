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
    // The pane that claimed this card (#R9G7): the session token of the terminal pane whose
    // agent is working on it, written into the card's front matter by `board_claim`. Empty on a
    // card nobody has claimed. The pane it names may be long gone — the token outlives the pane,
    // so whoever draws it asks the window whether that pane is still open.
    QString session;
    // The manual section this card is parked in (#3XZV): the id of a configured column that
    // collects nothing. Empty everywhere else — the card sits in its status's own section. It
    // wins over the status for as long as that column exists, so a parked card stays put while
    // its stage moves underneath it, and the drop that takes it out clears it.
    QString section;
    // The card's whole text — body, then each thread entry — as the worker used to send it on
    // every row (protocol 19.2 `text`) for the filter's plain words. It stopped sending it on
    // 2026-09-20 (#7M6E): it was 92.6 % of the `board` event and past about 1,160 cards the
    // event overflowed the worker pipe's read buffer, so the pane never loaded at all. The
    // filter is still full-text search (owner, 2026-09-19) — `board_search` answers the plain
    // words in the worker now (see `matches` below). Empty from a current worker, filled by an
    // older one, and then `matches` searches it exactly as it always did.
    QString text;
    QString type = QStringLiteral("work");
    QStringList labels;
    int threadEntries = 0, tasksDone = 0, tasksTotal = 0;
    // The row's flag (#VKFV): −1…+3, 0 unflagged. The worker clamps what it sends; this side
    // clamps again so a hostile or stale row can never paint a colour that does not exist.
    int priority = 0;
    bool isPrivate = false;
    bool unread = false;   // local, from QSettings; never in git

    static Card fromJson(const QJsonObject &object);
    bool closed() const;                 // done or dropped
    bool parked() const;                 // deferred
    QString reference() const { return QStringLiteral("#") + id; }
    // The category folder on disk (`issues/changes/2026-…md` -> "changes"), for `folder:`.
    QString folder() const;
};

// ---- self-closed cards (#93WR) ---------------------------------------------------------------
//
// The agent finished a small piece of work and closed the card itself, with no verifier: the
// status is `done`, the worker stamped `verified_by` on that close, and the signature it stamped
// is the one that implemented the card (policy v3's *medium* tier,
// `backend/relay_core/board_policy.md`). That is the whole definition and it lives here alone, so
// the section a card falls into, the badge it wears and the row that folds it cannot disagree.
// Empty stamps are not a match — an unset field says nothing about who closed the card — and a
// card in any other status is not one however its stamps read.
bool selfClosed(const Card &card);

// The fold row's own words: "1 closed by the agent", "12 closed by the agent".
QString selfClosedTitle(int count);

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
// column header (below): a click on Flag, Card, Created or Updated orders the cards *inside every
// section* by that column. `Manual` is the board's own rank — the order drags and Alt+Shift+↑↓
// write — and the closed sections stay newest first under it, as they always were. Any other
// order takes the manual reorder off (a rank nobody can see is a rank nobody can write), so what
// is on screen and what a drag would say never disagree.
enum class Sort { Manual, NewestFirst, OldestFirst, RecentlyUpdated, OldestUpdated, TitleAsc,
                  TitleDesc, PriorityHigh, PriorityLow };
// The id the pane's layout node keeps ("manual", "newest", "oldest", "updated", "updated-oldest",
// "title", "title-desc") and back; an unknown id reads as Manual, so a saved pane survives a sort
// being renamed away.
QString sortId(Sort sort);
Sort sortFromId(const QString &id);
// "Manual", "Newest first", "Oldest first", "Recently updated", "Least recently updated",
// "Title A→Z", "Title Z→A" — the word for what is on, in a notice or a tooltip.
QString sortTitle(Sort sort);

// ---- grouping (#ESDF, owner 2026-09-21: "i think it would be better if that was one of the sort
// options. instead the stage should be a column") ------------------------------------------------
//
// Whether the list is cut into a section per stage or is one run of cards. `Flat` is the default
// for a new pane (owner, 2026-09-21, "yes"): every shown card in one list, in the sort that is on
// across the whole of it, each row naming its stage in the Stage column. `Sections` is the board
// as it was — a header per status with the sort inside each. The Stage header toggles the two.
enum class Grouping { Sections, Flat };
// "sections", "flat" — the id the pane's layout node keeps — and back; an unknown id reads as
// Sections, and an *empty* one (a node saved before the choice existed) as the default, Flat.
QString groupingId(Grouping grouping);
Grouping groupingFromId(const QString &id);

// ---- the list's columns (owner, 2026-09-19: "add a 'created' and 'updated' column") ----------
//
// The header row over the list, left to right: the priority flag, the card itself, then when it
// was created and when it last changed. Each is a sort, and a click cycles that column's own
// orders and then back to Manual, so the board's drag order is always one click away.
enum class SortColumn { Priority, Card, Created, Updated };
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

// The mark at the head of a card row was retired with the flag (owner, 2026-09-19, card #VKFV:
// the glyphs "just reflect sections", so the column is the flag now). Nothing draws it.

// One badge on a card row (design 4.2): what it says and how the pane colours it.
struct Badge {
    enum Kind { Label, Agent, Assignee, Waiting, Status, Tasks, TasksDone, Thread, Private,
                Verified, Session, SessionClosed };
    Kind kind;
    QString text;
};
// The badges a card carries, in reading order. `showStatus` is for sections that collect several
// statuses (Waiting, Needs QA, Done), where the exact one is otherwise invisible. `sessionLive`
// is whether the pane the card's `session` names is still open (#R9G7): a closed one still shows
// its token — the thread's link is the history of who took the card — but says so and is muted.
QList<Badge> badges(const Card &card, bool showStatus, bool sessionLive = true);

// The chip a claimed card wears, on a row and on the card page (#R9G7): "⧉ xxxxxxxx" for a live
// pane, "⧉ xxxxxxxx closed" once that pane has gone. The glyph is the one SessionInfo already
// uses for a session id (U+29C9). Empty for a card with no `session`.
QString sessionChip(const QString &token, bool live);

// Which badge a narrow row gives up first: the lowest number goes first. Labels and the thread
// count are pleasant to have; `waiting:` and the exact status are why the row is being read.
int badgeDropOrder(Badge::Kind kind);

// The badges that fit at the right of a row. `measured` is every badge with its pixel width, in
// reading order; `available` is the room the row can spare and `gap` the space between two.
// Badges are dropped in `badgeDropOrder` until the rest fit, so a narrow pane loses decoration
// before it loses meaning. Returns what is kept, still in reading order.
QList<Badge> fitBadges(const QList<QPair<Badge, int>> &measured, int available, int gap);

// A card turn's mode as the thread shows it (protocol 19.10, #XS6Q): "discuss" -> "Discuss",
// "plan" -> "Plan", "refine" -> "Refine" (#6W9X), "execute" -> "Execute"; empty for anything else, so an entry written before
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
//   "anthropic/claude-opus-5-5 via claude-code" -> "Claude Opus 5.5 · Claude Code"
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

// What the verifier's pane is handed when Verify is pressed on a card in a verify lane —
// `needs-verification`, the implementer's checklist being checked (#3XZV), or one of the QA
// lanes after it. The card travels with it (`ask {cards: [id]}`) for a preset runner; a guest
// CLI gets this text alone as its first prompt, so the brief says where to find the card as
// well as what to do with it. `verifier` is the recommendation's label, `implementedBy` the
// card's signature, `status` the lane the card is in (which is what the pass/fail moves are),
// and `note` whatever the owner had typed in the reply box, passed on verbatim.
QString verifyTask(const QString &id, const QString &title, const QString &verifier,
                   const QString &implementedBy, const QString &status = QString(),
                   const QString &note = QString());

// The body without its leading `# Title` line when that only repeats the title: the card
// detail already shows the title in its header, so the heading would be said twice.
QString bodyWithoutTitle(const QString &body, const QString &title);

// The card's `verify:` block (#WFRA, the QA ladder): how the card says it will be checked, as
// `board_card` sends it — a JSON object under `verify`, absent when the card has none. The
// worker validates the vocabularies on read and on write; this side only carries the words to
// the strip, so a value it has never heard of is drawn as it came rather than dropped. The
// `also` list is the alternate modes in ladder order, `deferred` the "until …" that says the
// card is knowingly unverified for now, and `human` says whether a person is in the plan.
struct VerifyPlan {
    bool present = false;   // the card carries a block at all
    QString artifact, primary, deferred, human, criteria, sample, signOff, effort, stakes, blast;
    QStringList also;

    static VerifyPlan fromJson(const QJsonValue &value);
};

// What of the block the person is shown, one line: `Your review: <criteria> · effort medium`
// when the plan asks for them (`human` required or optional), `Needs your sign-off: publish`
// when `sign_off` is more than none, `Unverified until <deferred>` when the card is knowingly
// waiting. Owner steer 2026-09-23: most of the block is the agent's work and the user does not
// see it directly, so a card whose plan is fully machine-verified shows nothing at all — no
// placeholder, no "no plan yet". Empty when there is nothing for a person in it.
QString verifyStripText(const VerifyPlan &plan);
// The rest of the block for the strip's tooltip, one `key: value` per line in the card's own
// order, so the fields the strip leaves out (artifact, sample, sign-off, stakes, blast) are a
// hover away without a second strip. Empty for a card with no block.
QString verifyPlanDetail(const VerifyPlan &plan);

// How long ago a thread entry was written, from its sortable id (`20260918T021603Z-tg`):
// "just now", "12 min ago", "3 h ago", "yesterday", "Sep 16", or empty when the id has no time.
QString entryAge(const QString &entryId, const QDateTime &now);

// Where a card lands in a section: `order` is the section's ids top to bottom (it may contain
// `moving`), `slot` the insertion row counted in `order` *without* `moving`. Returns the id the
// card goes before and the id it goes after; either is empty at an end (protocol 19, board_move).
QPair<QString, QString> placement(const QStringList &order, const QString &moving, int slot);

// One line of the pane's single scrolling list (design 4.6): either a section header for a status
// or one card. The view paints these and holds nothing else, so what the list shows is decided
// here and can be tested without a widget.
struct Row {
    // `Fold` is the row that is neither a card nor a section (#93WR): the self-closed cards of
    // one section, put away behind "N closed by the agent" at the end of that section's cards.
    // Its own cards follow it when it is open, as ordinary card rows.
    //
    // The last three are the machine's own faults (#AQ6X, src/BoardSignals.h), spliced in above
    // the sections: `SignalFold` is "N signals", `DismissedFold` the "N dismissed" toggle at the
    // end of that block, and `Signal` one signal. A signal is **not** a card — it has no id in
    // `issues/` — so it carries `signalKey` and never `cardId`, and every card action steps over
    // it exactly as it steps over a fold row.
    enum Kind { Section, Card, Fold, Signal, SignalFold, DismissedFold };
    Kind kind = Card;
    QString columnId;         // every kind: the section this row belongs to
    QString title;            // Section: the status name. Fold: "3 closed by the agent".
                              // SignalFold: "5 signals". Signal: the signal's key
    QString cardId;           // Card: the card
    int count = 0;            // Section: how many cards it holds, after the filter.
                              // Fold: how many self-closed cards it stands for.
                              // SignalFold/DismissedFold: how many signals. Signal: its failures
    bool collapsed = false;   // Section, Fold, SignalFold, DismissedFold: its rows are not in the list
    bool showStatus = false;  // Card: its section holds several statuses, so the row names this one
    // The signal fields come last on purpose: a `Row{Row::Card, "ready", {}, "A", 0, false,
    // false}` in a test names its members by position, so a field inserted above would silently
    // shift what those braces mean.
    QString signalKey;        // Signal: the signal's key (`ctest:panelayout`), never a card id
    int indent = 0;           // Signal: 1 for a member of a group, or a dismissed signal under
                              // its toggle — the row is drawn one step in from its parent
    QString stage;            // Card, flat list only (#ESDF): the Stage column's text — the
                              // card's exact status, or "Verified". Empty under sections, whose
                              // header already says it
};

// The card rows of one section, top to bottom.
QStringList cardsInSection(const QList<Row> &rows, const QString &columnId);
// Every card row of the list, top to bottom: what a drag or Alt+Shift+↑↓ reorders against when
// the list is flat (#ESDF), where one stage's cards are not a contiguous run.
QStringList cardsInList(const QList<Row> &rows);

// Where a drop lands: `beforeRow` is the row index the card would be inserted in front of
// (0 … rows.size()). The point just above a section header belongs to the section above it, so a
// line drawn anywhere inside a section drops into that section. Returns its id and the slot
// inside it, counted in card rows.
QPair<QString, int> dropTarget(const QList<Row> &rows, int beforeRow);

// The next selectable row at or past `from` in direction `delta`, skipping section headers; -1
// when there is none, so Up/Down walks the whole list across section breaks and stops at its
// ends. A fold row is selectable — Enter and →/← work on it — so the walk stops there too, and so
// do the signal rows and their two toggles (#AQ6X).
int stepRow(const QList<Row> &rows, int from, int delta);
// Whether a row can be stood on: everything but a section header.
bool selectableRow(const Row &row);

// The row index of a card, of a section header, or of a section's fold row (#93WR); -1 for none.
int rowOfCard(const QList<Row> &rows, const QString &cardId);
int rowOfSection(const QList<Row> &rows, const QString &columnId);
int rowOfFold(const QList<Row> &rows, const QString &columnId);
// The row index of a signal, of the "N signals" row, or of the "N dismissed" toggle (#AQ6X); -1
// for none. There is at most one of each in the list: the signals are the board's, not a
// section's.
int rowOfSignal(const QList<Row> &rows, const QString &key);
int rowOfSignalFold(const QList<Row> &rows);
int rowOfDismissedFold(const QList<Row> &rows);

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
    // Sections or one flat list (#ESDF, above). `rows()` follows it; `cards()` and `sections()`
    // do not — a section still exists in a flat list, it is only not drawn as a header.
    void setGrouping(Grouping grouping) { m_grouping = grouping; }
    Grouping grouping() const { return m_grouping; }
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
    // header and all, whether or not a filter is active: the two compose. So do the label chips:
    // a card the ticked labels rule out is off the page exactly as though the text filter had.
    // `selfClosedOpen` holds the sections whose "N closed by the agent" row is open (#93WR): the
    // self-closed cards of a section are one fold row at the end of its cards, and only a section
    // in this set draws them as ordinary rows under it. Nothing folds while a filter is active,
    // here as for a section header: a search that hid its own matches would be a search that does
    // nothing, so a matching self-closed card is an ordinary row and there is no fold row at all.
    //
    // Flat (#ESDF): no headers and no fold rows — every shown card of every section `hidden` does
    // not name, in the sort across the whole list (Manual is the board's global rank), each with
    // its `stage`. `collapsed` and `selfClosedOpen` say nothing then: folding is a section's.
    QList<Row> rows(const QSet<QString> &collapsed, const QSet<QString> &hidden = {},
                    const QSet<QString> &selfClosedOpen = {}) const;

    // ---- filtering
    void setFilter(const QString &text);
    QString filter() const { return m_filter; }
    // The label chips beside the section checkboxes (#VKFV): a card is on the page only when it
    // carries every ticked label, composing with the text filter and the section checkboxes the
    // way `label:` terms do. Labels compare case-insensitively, as the filter terms do.
    void setLabelFilter(const QSet<QString> &labels) { m_labelFilter = labels; }
    QSet<QString> labelFilter() const { return m_labelFilter; }
    // The filter's plain words — every term that is not `label:`, `status:`, `waiting:`,
    // `folder:`, `@` or `#` — in the order they were typed. These, and only these, are what the
    // worker answers (`board_search`); an all-scoped filter needs no search at all.
    static QStringList plainTerms(const QString &filter);
    // `label:voice status:ready folder:changes @agent waiting:me some words`; every term matches.
    //
    // `textMatch` is the worker's answer for `plainTerms(filter)`: the ids whose row fields, body
    // or thread hold every one of those words. Null means no answer for *this* filter has
    // arrived yet, and then a plain word is judged on the row's own fields (and `card.text`, for
    // an older worker that still sends it), so a title match shows the instant it is typed and
    // the body matches join it a moment later.
    static bool matches(const Card &card, const QString &filter,
                        const QSet<QString> *textMatch = nullptr);
    // The worker's `board_search` answer (#7M6E). `terms` is the query it answers, which is
    // `plainTerms(filter).join(' ')`; an answer for anything else is held but not used, so a
    // superseded search never filters the list by the wrong words.
    void setSearchResult(const QString &terms, const QSet<QString> &ids);
    // What the held answer answers, for the pane's debounce (empty when there is none).
    QString searchTerms() const { return m_searchTerms; }
    // Fuzzy ranking for the composer's `#` picker and the pane's search, best first.
    QList<Card> search(const QString &query, int limit = 20) const;
    static int score(const QString &query, const Card &card);

private:
    QList<Card> sorted(QList<Card> cards, bool newestFirst) const;
    // status -> the section that collects it, built once per query.
    QMap<QString, QString> sectionIndex(const QList<Column> &sections) const;
    // The text filter and the label chips together: what every gating query asks of a card.
    bool shown(const Card &card) const;

    QList<Tab> m_tabs;
    QStringList m_columns;                       // configured work columns, in order
    QMap<QString, QStringList> m_columnStatuses; // column id -> statuses (from board.yaml)
    QMap<QString, QString> m_columnTitles;       // column id -> the board's own name for it
    QStringList m_statusChoices;                 // every status a section may collect
    QMap<QString, Card> m_cards;                 // by id
    QString m_filter;
    // The plain words of `m_filter`, and the worker's answer for them (#7M6E). The answer is
    // used only while `m_searchTerms` still equals `m_plainTerms`: the moment another key is
    // typed the held ids are one query out of date, and the fields alone answer until the new
    // one lands.
    QString m_plainTerms, m_searchTerms;
    QSet<QString> m_searchIds;
    QSet<QString> m_labelFilter;
    Sort m_sort = Sort::Manual;
    Grouping m_grouping = Grouping::Sections;
};

}  // namespace board
}  // namespace relay
