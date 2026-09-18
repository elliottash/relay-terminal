// SPDX-License-Identifier: GPL-3.0-or-later
#include "BoardModel.h"

#include <QRegularExpression>

#include <QJsonValue>
#include <QLocale>
#include <QTimeZone>
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

// Where a status that no configured column collects sorts among the sections: a plan's
// lifecycle first, then the parked ones, then anything the board has invented.
int extraStatusRank(const QString &status)
{
    static const QMap<QString, int> ranks{
        {QStringLiteral("draft"), 1},    {QStringLiteral("approved"), 2},
        {QStringLiteral("executing"), 3}, {QStringLiteral("active"), 4},
        {QStringLiteral("deferred"), 8}, {QStringLiteral("retired"), 9}};
    return ranks.value(status, 5);
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

QString issueHeading()
{
    return QStringLiteral("Issue");
}

QString modeTitle(const QString &mode)
{
    if (mode == QStringLiteral("discuss"))
        return QStringLiteral("Discuss");
    if (mode == QStringLiteral("plan"))
        return QStringLiteral("Plan");
    if (mode == QStringLiteral("execute"))
        return QStringLiteral("Execute");
    return {};
}

QString threadMarkdown(const QString &text, const QString &kind)
{
    static const QRegularExpression summary(QStringLiteral("<details>\\s*<summary>(.*?)</summary>"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression close(QStringLiteral("\\s*</details>"));
    QString out = text;
    out.replace(summary, QStringLiteral("**\\1**"));
    out.replace(close, QString());
    if (kind == QStringLiteral("rewrite") && out.startsWith(QStringLiteral("- ")))
        out = out.mid(2);
    return out;
}

QString executeTask(const QString &id, const QString &title, bool hasPlan, bool hasAcceptance,
                    const QString &note)
{
    const QString ref = QStringLiteral("#") + id;
    QStringList lines;
    lines << QStringLiteral("Execute %1: %2").arg(ref, title) << QString();
    QString what = QStringLiteral("The Switchboard card %1 is attached");
    if (hasPlan && hasAcceptance)
        what += QStringLiteral(" with its issue, its `## Plan` and its acceptance. Carry out the plan "
                               "until the acceptance holds.");
    else if (hasPlan)
        what += QStringLiteral(" with its issue and its `## Plan`. Carry out the plan.");
    else if (hasAcceptance)
        what += QStringLiteral(" with its issue and its acceptance. It has no plan: read the code "
                               "first, then implement it until the acceptance holds.");
    else
        what += QStringLiteral(" with its issue. It has no plan and no acceptance: read the code "
                               "first, and say what you took \"done\" to mean when you finish.");
    lines << what.arg(ref) << QString();
    lines << QStringLiteral("The owner handed it to you from the Switchboard; it is already in "
                            "progress and assigned to the agent.")
          << QStringLiteral("- When you start, set `implemented_by` on %1 to your model "
                            "(board_update_card).").arg(ref)
          << QStringLiteral("- Put %1 in the message of every commit you make for it, and after "
                            "each commit add its short hash to the card's `links.commits` "
                            "(board_update_card `fields.links`: the card's whole `links` object "
                            "from board_read, with the hash appended).").arg(ref)
          << QStringLiteral("- Post progress, questions and decisions on %1 with board_comment, "
                            "not only here.").arg(ref)
          << QStringLiteral("- When it lands, move %1 to needs-qa-llm with the evidence path and a "
                            "`## QA checklist`, as the Switchboard rules say.").arg(ref);
    if (!note.trimmed().isEmpty())
        lines << QString() << QStringLiteral("The owner adds, verbatim:") << note.trimmed();
    return lines.join(QLatin1Char('\n'));
}

QString statusGlyph(const QString &status)
{
    static const QMap<QString, QString> marks{
        // Open shapes are states nothing has been committed to yet…
        {QStringLiteral("inbox"), QStringLiteral("○")},
        {QStringLiteral("discussing"), QStringLiteral("◇")},
        {QStringLiteral("draft"), QStringLiteral("○")},
        // …a solid one is work that has been agreed or is running…
        {QStringLiteral("ready"), QStringLiteral("◆")},
        {QStringLiteral("approved"), QStringLiteral("◆")},
        {QStringLiteral("in-progress"), QStringLiteral("▶")},
        {QStringLiteral("executing"), QStringLiteral("▶")},
        {QStringLiteral("active"), QStringLiteral("●")},
        // …a half circle is waiting on somebody, a ringed one is in a QA lane…
        {QStringLiteral("needs-review"), QStringLiteral("◐")},
        {QStringLiteral("needs-labels"), QStringLiteral("◐")},
        {QStringLiteral("needs-ab"), QStringLiteral("◐")},
        {QStringLiteral("needs-qa"), QStringLiteral("◉")},
        {QStringLiteral("needs-qa-llm"), QStringLiteral("◉")},
        {QStringLiteral("needs-qa-human"), QStringLiteral("◉")},
        // …and a dotted circle, a check or a cross is off the live board.
        {QStringLiteral("deferred"), QStringLiteral("◌")},
        {QStringLiteral("retired"), QStringLiteral("◌")},
        {QStringLiteral("done"), QStringLiteral("✓")},
        {QStringLiteral("dropped"), QStringLiteral("✗")}};
    return marks.value(status, QStringLiteral("·"));
}

QList<Badge> badges(const Card &card, bool showStatus)
{
    QList<Badge> out;
    if (showStatus && !card.status.isEmpty()) {
        // Only the part the column header does not already say ("Needs QA" → "LLM QA").
        static const QMap<QString, QString> shortNames{
            {QStringLiteral("needs-qa-llm"), QStringLiteral("LLM QA")},
            {QStringLiteral("needs-qa-human"), QStringLiteral("human QA")},
            {QStringLiteral("needs-review"), QStringLiteral("review")},
            {QStringLiteral("needs-labels"), QStringLiteral("labels")},
            {QStringLiteral("needs-ab"), QStringLiteral("A/B")},
            {QStringLiteral("dropped"), QStringLiteral("dropped")}};
        out << Badge{Badge::Status, shortNames.value(card.status, statusTitle(card.status))};
    }
    for (const QString &label : card.labels)
        out << Badge{Badge::Label, label};
    if (card.assignee == QStringLiteral("agent"))
        out << Badge{Badge::Agent, QStringLiteral("✦ agent")};
    else if (!card.assignee.isEmpty())
        out << Badge{Badge::Assignee, card.assignee};
    if (!card.waitingOn.isEmpty())
        out << Badge{Badge::Waiting, QStringLiteral("waiting: ") + card.waitingOn};
    if (card.tasksTotal > 0)
        out << Badge{card.tasksDone >= card.tasksTotal ? Badge::TasksDone : Badge::Tasks,
                     QStringLiteral("☑ %1/%2").arg(card.tasksDone).arg(card.tasksTotal)};
    if (card.threadEntries > 0)
        out << Badge{Badge::Thread, QStringLiteral("✎ %1").arg(card.threadEntries)};
    if (card.isPrivate)
        out << Badge{Badge::Private, QStringLiteral("private")};
    return out;
}

QList<Badge> rowBadges(const Card &card, bool showStatus, const QDate &today)
{
    QList<Badge> out = badges(card, showStatus);
    const QString age = cardAge(card.created, today);
    if (!age.isEmpty())
        out << Badge{Badge::Age, age};
    return out;
}

int badgeDropOrder(Badge::Kind kind)
{
    switch (kind) {
    case Badge::Label:
        return 1;
    case Badge::Thread:
        return 2;
    case Badge::Age:
        return 3;
    case Badge::Tasks:
    case Badge::TasksDone:
        return 4;
    case Badge::Assignee:
        return 5;
    case Badge::Agent:
        return 6;
    case Badge::Private:
        return 7;
    case Badge::Status:
        return 8;
    case Badge::Waiting:
        return 9;
    }
    return 5;
}

QList<Badge> fitBadges(const QList<QPair<Badge, int>> &measured, int available, int gap)
{
    QList<int> kept;
    int width = 0;
    for (int i = 0; i < measured.size(); ++i) {
        kept << i;
        width += measured.at(i).second + (i > 0 ? gap : 0);
    }
    while (width > available && !kept.isEmpty()) {
        // The least important goes, and among equals the leftmost: with three labels the row
        // keeps the last one it can, which sits nearest the badges that earned their place.
        int worst = 0;
        for (int i = 1; i < kept.size(); ++i) {
            const int a = badgeDropOrder(measured.at(kept.at(i)).first.kind);
            const int b = badgeDropOrder(measured.at(kept.at(worst)).first.kind);
            if (a < b)
                worst = i;
        }
        width -= measured.at(kept.at(worst)).second + (kept.size() > 1 ? gap : 0);
        kept.removeAt(worst);
    }
    QList<Badge> out;
    for (int index : std::as_const(kept))
        out << measured.at(index).first;
    return out;
}

QString bodyWithoutTitle(const QString &body, const QString &title)
{
    // Leading blank lines are skipped; anything else before the heading means it is not a title.
    int start = 0;
    while (start < body.size() && (body.at(start) == QLatin1Char('\n') || body.at(start) == QLatin1Char('\r')))
        ++start;
    if (!QStringView(body).mid(start).startsWith(QLatin1String("# ")))
        return body;
    int end = body.indexOf(QLatin1Char('\n'), start);
    if (end < 0)
        end = body.size();
    const QString heading = body.mid(start + 2, end - start - 2).trimmed();
    if (heading.compare(title.trimmed(), Qt::CaseInsensitive) != 0)
        return body;
    while (end < body.size() && (body.at(end) == QLatin1Char('\n') || body.at(end) == QLatin1Char('\r')))
        ++end;
    return body.mid(end);
}

QString entryAge(const QString &entryId, const QDateTime &now)
{
    const QDateTime parsed = QDateTime::fromString(entryId.left(16), QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    if (!parsed.isValid())
        return QString();
    const QDateTime at(parsed.date(), parsed.time(), QTimeZone::utc());
    const qint64 seconds = at.secsTo(now);
    if (seconds < 60)
        return QStringLiteral("just now");
    if (seconds < 3600)
        return QStringLiteral("%1 min ago").arg(seconds / 60);
    const QDate day = at.toLocalTime().date(), today = now.toLocalTime().date();
    if (day == today)
        return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (day.addDays(1) == today)
        return QStringLiteral("yesterday");
    const QLocale c = QLocale::c();
    return day.year() == today.year() ? c.toString(day, QStringLiteral("MMM d"))
                                      : c.toString(day, QStringLiteral("MMM d yyyy"));
}

QString cardAge(const QString &created, const QDate &today)
{
    if (created.isEmpty() || !today.isValid())
        return QString();
    // `created` is a date in the front matter, but a timestamp is accepted: take the date part.
    const QDate day = QDate::fromString(created.left(10), Qt::ISODate);
    if (!day.isValid())
        return QString();
    const qint64 days = day.daysTo(today);
    if (days < 0)
        return QStringLiteral("today");
    if (days == 0)
        return QStringLiteral("today");
    if (days < 14)
        return QStringLiteral("%1 d").arg(days);
    if (days < 70)
        return QStringLiteral("%1 w").arg(days / 7);
    if (days < 730)
        return QStringLiteral("%1 mo").arg(days / 30);
    return QStringLiteral("%1 y").arg(days / 365);
}

QPair<QString, QString> placement(const QStringList &order, const QString &moving, int slot)
{
    QStringList others = order;
    others.removeAll(moving);
    slot = qBound(0, slot, int(others.size()));
    return qMakePair(slot < others.size() ? others.at(slot) : QString(),
                     slot > 0 ? others.at(slot - 1) : QString());
}

QStringList cardsInSection(const QList<Row> &rows, const QString &columnId)
{
    QStringList out;
    for (const Row &row : rows)
        if (row.kind == Row::Card && row.columnId == columnId)
            out << row.cardId;
    return out;
}

QPair<QString, int> dropTarget(const QList<Row> &rows, int beforeRow)
{
    if (rows.isEmpty())
        return qMakePair(QString(), 0);
    beforeRow = qBound(0, beforeRow, int(rows.size()));
    // Walk back to the header that owns this point. Above the very first header there is nothing
    // to own it, so the drop falls into the first section.
    int header = -1;
    for (int i = beforeRow - 1; i >= 0; --i) {
        if (rows.at(i).kind == Row::Section) {
            header = i;
            break;
        }
    }
    if (header < 0) {
        for (int i = 0; i < rows.size(); ++i)
            if (rows.at(i).kind == Row::Section)
                return qMakePair(rows.at(i).columnId, 0);
        return qMakePair(QString(), 0);
    }
    int slot = 0;
    for (int i = header + 1; i < beforeRow; ++i)
        if (rows.at(i).kind == Row::Card)
            ++slot;
    return qMakePair(rows.at(header).columnId, slot);
}

int stepRow(const QList<Row> &rows, int from, int delta)
{
    if (delta == 0)
        return from;
    for (int i = from + delta; i >= 0 && i < rows.size(); i += delta)
        if (rows.at(i).kind == Row::Card)
            return i;
    return -1;
}

int rowOfCard(const QList<Row> &rows, const QString &cardId)
{
    if (cardId.isEmpty())
        return -1;
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::Card && rows.at(i).cardId == cardId)
            return i;
    return -1;
}

int rowOfSection(const QList<Row> &rows, const QString &columnId)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).kind == Row::Section && rows.at(i).columnId == columnId)
            return i;
    return -1;
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

QString Card::folder() const
{
    // `issues/changes/2026-09-17-x.md`, or `issues/.private/changes/…` for a private card.
    QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    while (!parts.isEmpty() && (parts.first() == QStringLiteral("issues")
                                || parts.first().startsWith(QLatin1Char('.'))))
        parts.removeFirst();
    return parts.size() > 1 ? parts.first() : QString();
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

QString doneSection()
{
    return QStringLiteral("done");
}

const Tab *Model::tab(const QString &id) const
{
    for (const Tab &tab : m_tabs)
        if (tab.id == id)
            return &tab;
    return nullptr;
}

// The one list's sections. The configured columns come first in their configured order, then any
// status they do not collect — a plan's Draft, a memory's Active, a Deferred card — so that one
// list really does hold every card that is not closed. Done is always last.
QList<Column> Model::sections() const
{
    QList<Column> out;
    QSet<QString> collected;
    for (const QString &id : m_columns) {
        QStringList statuses = m_columnStatuses.value(id);
        if (statuses.isEmpty())
            statuses = fallbackStatuses(id);
        statuses.removeAll(QStringLiteral("done"));
        statuses.removeAll(QStringLiteral("dropped"));
        if (statuses.isEmpty())
            continue;                    // a configured Done column: it is the last section
        for (const QString &status : statuses)
            collected.insert(status);
        out << Column{id, statusTitle(id), statuses};
    }
    QStringList extras;
    for (const Card &card : m_cards) {
        if (card.closed() || card.status.isEmpty() || collected.contains(card.status))
            continue;
        if (!extras.contains(card.status))
            extras << card.status;
    }
    std::sort(extras.begin(), extras.end(), [](const QString &a, const QString &b) {
        const int ra = extraStatusRank(a), rb = extraStatusRank(b);
        return ra != rb ? ra < rb : a < b;
    });
    for (const QString &status : std::as_const(extras))
        out << Column{status, statusTitle(status), {status}};
    out << Column{doneSection(), statusTitle(QStringLiteral("done")),
                  {QStringLiteral("done"), QStringLiteral("dropped")}};
    return out;
}

QString Model::dropStatus(const QString &columnId) const
{
    const QList<Column> list = sections();
    for (const Column &column : list)
        if (column.id == columnId)
            return column.statuses.value(0);
    return QString();
}

QMap<QString, QString> Model::sectionIndex(const QList<Column> &sections) const
{
    QMap<QString, QString> out;
    for (const Column &column : sections)
        for (const QString &status : column.statuses)
            if (!out.contains(status))
                out.insert(status, column.id);
    return out;
}

QString Model::sectionOf(const Card &card) const
{
    if (card.closed())
        return doneSection();
    return sectionIndex(sections()).value(card.status);
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

QList<Card> Model::sorted(QList<Card> cards, bool newestFirst) const
{
    std::sort(cards.begin(), cards.end(), [newestFirst](const Card &a, const Card &b) {
        if (newestFirst && a.created != b.created)
            return a.created > b.created;
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return a.path < b.path;
    });
    return cards;
}

QList<Card> Model::cards(const QString &columnId) const
{
    const QMap<QString, QString> index = sectionIndex(sections());
    QList<Card> out;
    for (const Card &card : m_cards) {
        const QString section = card.closed() ? doneSection() : index.value(card.status);
        if (section != columnId || section.isEmpty())
            continue;
        if (!matches(card, m_filter))
            continue;
        out << card;
    }
    return sorted(out, columnId == doneSection());
}

int Model::openCount() const
{
    int total = 0;
    for (const Card &card : m_cards)
        if (!card.closed() && matches(card, m_filter))
            ++total;
    return total;
}

// Open cards a section checkbox is keeping out of the list. Only open ones, so that the count
// label's "62 of 84 open" subtracts exactly: closed cards are not in the 84 either.
int Model::hiddenCount(const QSet<QString> &hidden) const
{
    if (hidden.isEmpty())
        return 0;
    const QMap<QString, QString> index = sectionIndex(sections());
    int total = 0;
    for (const Card &card : m_cards) {
        if (card.closed() || !matches(card, m_filter))
            continue;
        const QString section = index.value(card.status);
        if (!section.isEmpty() && hidden.contains(section))
            ++total;
    }
    return total;
}

QList<Row> Model::rows(const QSet<QString> &collapsed, const QSet<QString> &hidden) const
{
    const bool filtered = !m_filter.trimmed().isEmpty();
    const QList<Column> list = sections();
    const QMap<QString, QString> index = sectionIndex(list);
    QMap<QString, QList<Card>> grouped;
    for (const Card &card : m_cards) {
        if (!matches(card, m_filter))
            continue;
        const QString section = card.closed() ? doneSection() : index.value(card.status);
        if (section.isEmpty())
            continue;      // a status no section collects and that is not closed: nothing to show
        grouped[section] << card;
    }
    QList<Row> out;
    for (const Column &column : list) {
        if (hidden.contains(column.id))
            continue;      // its checkbox is unticked: the section is not on the page at all
        const QList<Card> cards = sorted(grouped.value(column.id), column.id == doneSection());
        if (filtered && cards.isEmpty())
            continue;      // a section with nothing to show gets out of the way
        Row header;
        header.kind = Row::Section;
        header.columnId = column.id;
        header.title = column.title;
        header.count = int(cards.size());
        // Nothing is folded while a filter is active: a search that hid its own matches would be
        // a search that does nothing.
        header.collapsed = !filtered && collapsed.contains(column.id);
        out << header;
        if (header.collapsed)
            continue;
        // A section that collects several statuses (Waiting, Needs QA, Done) names each card's
        // exact one; the one whose status is the section's own repeats nothing.
        const bool multi = column.statuses.size() > 1;
        for (const Card &card : cards) {
            Row row;
            row.kind = Row::Card;
            row.columnId = column.id;
            row.cardId = card.id;
            row.showStatus = multi && card.status != column.id;
            out << row;
        }
    }
    return out;
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
        } else if (term.startsWith(QStringLiteral("folder:"))) {
            // The folder on disk, or the board.yaml tab id that names it: `folder:changes` and
            // `folder:bugs` both reach the same cards.
            const QString want = term.mid(7);
            if (card.folder().compare(want, Qt::CaseInsensitive) != 0
                && card.tab.compare(want, Qt::CaseInsensitive) != 0)
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
