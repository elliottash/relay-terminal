// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelRows.h"

#include "FilterPopup.h"   // kTrailingItemRole: the via column, carried on a combo row

#include <QComboBox>
#include <QDateTime>
#include <QStandardItemModel>

#include <algorithm>

namespace relay::modelrows {

QString roleLabel(const QString &role)
{
    static const QHash<QString, QString> labels{
        // The tiers, in the project's own words (card #MDL1, rule 1: "tier and role words are
        // lower-case too: main, flash, not Main agent").
        {QStringLiteral("main"), QStringLiteral("main")},
        {QStringLiteral("high"), QStringLiteral("high")},
        {QStringLiteral("flash"), QStringLiteral("flash")},
        {QStringLiteral("lite"), QStringLiteral("lite")},
        {QStringLiteral("local"), QStringLiteral("local")},
        // The worker-only roles: a plain lower-case phrase for the job, never a protocol name and
        // never "Switchboard agent" / "Helper agent" in a line that names a model.
        {QStringLiteral("terminal_use"), QStringLiteral("terminal use")},
        {QStringLiteral("subagent"), QStringLiteral("subagents")},
        {QStringLiteral("switchboard"), QStringLiteral("helpers")},
        {QStringLiteral("planning"), QStringLiteral("plan mode")},
        {QStringLiteral("summaries"), QStringLiteral("summaries")},
        {QStringLiteral("suggestions"), QStringLiteral("suggestions")},
        {QStringLiteral("chores"), QStringLiteral("chores")},
        {QStringLiteral("audit"), QStringLiteral("request audit")},
        {QStringLiteral("loop_check"), QStringLiteral("loop check")},
        {QStringLiteral("vision"), QStringLiteral("vision")},
        {QStringLiteral("route_assist"), QStringLiteral("route assist")}};
    // An unknown role reads as its own id with the underscores opened out, still lower-case, so a
    // role added to the worker before this table is not printed as "loop_check".
    return labels.value(role, QString(role).replace(QLatin1Char('_'), QLatin1Char(' ')).toLower());
}

// The tier a role runs on (the worker's roles.ROLE_TIERS): what the row's parentheses say. A role's
// protocol name is not a word for a person — the Switchboard's agent read "kimi-k3 (switchboard)"
// (owner, 2026-09-21: "i also dont like how it says switchboard in the picker").
QString roleTier(const QString &role)
{
    static const QHash<QString, QString> tiers{
        {QStringLiteral("switchboard"), QStringLiteral("main")}, {QStringLiteral("subagent"), QStringLiteral("main")},
        {QStringLiteral("planning"), QStringLiteral("high")},
        {QStringLiteral("terminal_use"), QStringLiteral("flash")}, {QStringLiteral("summaries"), QStringLiteral("flash")},
        {QStringLiteral("suggestions"), QStringLiteral("flash")},
        {QStringLiteral("chores"), QStringLiteral("lite")}, {QStringLiteral("audit"), QStringLiteral("lite")},
        {QStringLiteral("loop_check"), QStringLiteral("lite")}};
    return tiers.value(role, role);
}

QString roleRowText(const QString &role, const QString &model)
{
    if (model.isEmpty()) return roleLabel(role);
    // The pane's own model is just the model (owner, 2026-09-21: "i dont want it to say (main)
    // either"): the parentheses are for the rows that are something else — (flash), (local).
    const QString tier = roleTier(role);
    return tier == QStringLiteral("main") ? model : QStringLiteral("%1 (%2)").arg(model, tier);
}

namespace {

// The providers a group would spend, for the row's right-hand column: the one the turn actually
// goes to, plus how many more stand behind it. "codex", "z.ai +1".
QString viaText(const models::Group &group, const models::Catalog &catalog, qint64 now)
{
    if (group.entries.isEmpty()) return QString();
    const models::Entry preferred = group.preferred(catalog, now);
    const QString provider = preferred.provider.isEmpty() ? preferred.preset : preferred.provider;
    const int others = int(group.entries.size()) - 1;
    return others > 0 ? QStringLiteral("%1 +%2").arg(provider).arg(others) : provider;
}

// Why a row is greyed, in a sentence, and every provider it has: the tooltip of a row the box keeps
// but cannot run. A subscription that is merely spent says when it comes back.
QString viaTooltip(const models::Group &group, const models::Catalog &catalog, qint64 now)
{
    QStringList lines;
    for (const models::Entry &entry : group.entries) {
        const QString provider = entry.provider.isEmpty() ? entry.preset : entry.provider;
        QString why;
        if (!entry.usable) {
            why = QStringLiteral(" — no stored key");
        } else if (models::exhausted(catalog, entry.preset, now)) {
            const qint64 until = models::exhaustedUntil(catalog, entry.preset, now);
            const QString when = until > 0 ? models::resetText(until, now) : QString();
            why = when.isEmpty() ? QStringLiteral(" — spent")
                                 : QStringLiteral(" — spent, back %1").arg(when);
        }
        lines << provider + why;
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

QString modeRowText(const QString &mode, const QString &model, bool current)
{
    // The marker the design draws in the gutter (section 5.1). It is in the text rather than in the
    // delegate so that every box — the popup, the combo's own model, the phone's menu — says the
    // same thing about which mode the pane is in, and two spaces stand in its place elsewhere so
    // the names line up under each other.
    const QString mark = current ? QStringLiteral("\u2022 ") : QStringLiteral("  ");
    return model.isEmpty() ? mark + mode : QStringLiteral("%1%2 (%3)").arg(mark, mode, model);
}

// The tier's own list as catalog entries, in list order and including the ones that cannot be
// used. Empty when the user has ranked nothing there — which is a different thing from "no models",
// and the two callers below want different answers to it.
static QList<models::Entry> tierEntriesOf(const Context &context, const QString &mode)
{
    QList<models::Entry> out;
    for (const models::curation::TierEntry &item : models::curation::tierList(mode))
        if (const models::Entry *entry = context.catalog.find(item.key)) out << *entry;
    return out;
}

QList<models::Entry> modeEntries(const Context &context, const QString &mode)
{
    if (const QList<models::Entry> listed = tierEntriesOf(context, mode); !listed.isEmpty()) return listed;
    // A tier the user has never ranked: its built-in default runs it, and the useful thing to offer
    // is every model, in rank order — which is what this box drew before the lists existed. Local
    // is the exception: "local" is a promise about where the text goes (design 3.2), so its page
    // never offers a cloud model, even when nothing has been ranked there.
    const QList<models::Entry> all = models::shown(context.catalog);
    if (mode != QStringLiteral("local")) return all;
    QList<models::Entry> out;
    for (const models::Entry &entry : all)
        if (entry.local) out << entry;
    return out;
}

QString modeKey(const Context &context, const QString &mode)
{
    if (const QString pick = context.modePick.value(mode); !pick.isEmpty()) return pick;
    const qint64 now = context.now;
    // Rank 1 of that tier's list — the *list's*, never the fallback the page draws: a tier the user
    // has ranked nothing in is resolved by the worker (the pane's own model at max for high, the
    // provider's flash model, the first saved endpoint for local), and guessing a cloud model out
    // of the whole catalog would put a name in the parentheses that the mode would not run.
    for (const models::Entry &entry : tierEntriesOf(context, mode))
        if (entry.usable && !models::exhausted(context.catalog, entry.preset, now)) return entry.key;
    // Main is the exception, and the same one `models::mainDefault` makes: with no lists stored at
    // all, rank 1 of the ranked catalog *is* what a new pane starts on (rule 3).
    if (mode == QStringLiteral("main") && !models::curation::tierListsSet()) {
        const QList<models::Entry> live = models::live(context.catalog, now);
        if (!live.isEmpty()) return live.first().key;
    }
    return QString();
}

QString modeModel(const Context &context, const QString &mode)
{
    if (const QString key = modeKey(context, mode); !key.isEmpty()) {
        if (const models::Entry *entry = context.catalog.find(key))
            return entry->name.isEmpty() ? models::nameOf(entry->model) : entry->name;
        QString preset, model;
        if (models::Catalog::splitKey(key, &preset, &model)) return models::nameOf(model);
    }
    // Nothing in the catalog answers: what the worker's role summary resolved, named by the one
    // rule. This is the ordinary case for `high` with no high list — the pane's own model at max.
    const QString role = context.modeRole.value(mode, mode);
    const QString resolved = context.roleModel.value(role);
    return resolved.isEmpty() ? QString() : models::nameOf(resolved);
}

QString collapsedText(const Context &context)
{
    const QString model = modeModel(context, context.mode);
    if (model.isEmpty()) return context.mode == QStringLiteral("main") ? QString() : context.mode;
    // Owner, 2026-09-21: the model alone on main — "i dont want it to say (main) either" — and
    // "<model> · <mode>" on any other mode, so a pane that is not on its own model always says so.
    return context.mode == QStringLiteral("main") ? model
                                                  : QStringLiteral("%1 · %2").arg(model, context.mode);
}

int indexOf(const QList<Row> &rows, const QString &data)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).data == data) return i;
    return -1;
}

Page page(const Context &context, const QString &mode)
{
    Page out;
    out.mode = mode;
    const qint64 now = context.now > 0 ? context.now : QDateTime::currentSecsSinceEpoch();
    // 1. The modes, every page carrying the same four with the marker on the one the pane is in.
    for (const QString &id : context.modes) {
        Row row;
        row.text = modeRowText(id, modeModel(context, id), id == context.mode);
        row.data = QStringLiteral("role:") + context.modeRole.value(id, id);
        row.tooltip = context.roleNote.value(context.modeRole.value(id, id));
        row.enabled = true;
        out.rows << row;
    }
    // 2. This page's models, in list order, one row per model (rule 2). The pane's own model is the
    //    row that opens highlighted, and it keeps its own entry rather than the group's preferred
    //    one — the pane is on the provider it is on.
    const QString paneKey = modeKey(context, mode);
    bool first = true;
    for (const models::Group &group : models::grouped(context.catalog, modeEntries(context, mode), now)) {
        const bool holdsPane = std::any_of(group.entries.cbegin(), group.entries.cend(),
                                           [&paneKey](const models::Entry &entry) { return entry.key == paneKey; });
        const models::Entry preferred = group.preferred(context.catalog, now);
        Row row;
        row.text = group.name.isEmpty() ? preferred.model : group.name;
        row.data = QStringLiteral("pick:%1|%2").arg(mode, holdsPane ? paneKey : preferred.key);
        row.trailing = viaText(group, context.catalog, now);
        row.tooltip = viaTooltip(group, context.catalog, now);
        // Greyed in place, never dropped (design 1.3): a subscription running out moves the turn to
        // the next provider in the row, and only when every one is spent does the row go grey.
        row.enabled = !group.spent(context.catalog, now);
        row.separatorBefore = first;
        first = false;
        if (holdsPane) out.current = int(out.rows.size());
        out.rows << row;
    }
    // 3. Guest agents (26.9) the worker cannot run as a harness, plus whichever one is in the pane.
    //    A guest the worker *can* run is a catalog entry and is already one of the rows above, so
    //    it is not offered twice. They belong to the main page: a guest is the pane's own agent.
    if (mode == QStringLiteral("main")) {
        bool firstGuest = first;
        for (const QString &id : context.guests) {
            Row row;
            row.text = context.guestText.value(id, id);
            row.data = QStringLiteral("guest:") + id;
            row.separatorBefore = firstGuest;
            row.enabled = true;
            firstGuest = false;
            first = false;
            out.rows << row;
        }
    }
    // 4. The picker (every model with filter, sort and reasoning level — the same dialog Ctrl+Alt+M
    //    opens) and Options › Models (providers, the checklist, the order). Both stay reachable
    //    with no stored key, which is exactly when they are needed most.
    Row more;
    more.text = QStringLiteral("more models…");
    more.data = QStringLiteral("gear:picker");
    more.separatorBefore = true;
    more.enabled = true;
    out.rows << more;
    Row gear;
    gear.text = QString(QChar(0x2699)) + QStringLiteral("  customize…");
    gear.data = QStringLiteral("gear:modelOptions");
    gear.enabled = true;
    out.rows << gear;
    // The caller's own current row wins where it has one (a guest in the pane's foreground).
    if (!context.current.isEmpty()) {
        if (const int at = indexOf(out.rows, context.current); at >= 0) out.current = at;
    }
    // Nothing named the pane's model on this page: the mode row is what it is on.
    if (out.current < 0)
        out.current = indexOf(out.rows, QStringLiteral("role:") + context.modeRole.value(mode, mode));
    return out;
}

QList<Page> pages(const Context &context)
{
    QList<Page> out;
    for (const QString &mode : context.modes) out << page(context, mode);
    return out;
}

QList<Row> build(const Context &context)
{
    return page(context, context.mode).rows;
}

int fill(QComboBox *box, const Context &context)
{
    if (box == nullptr) return -1;
    box->clear();
    const Page shown = page(context, context.mode);
    int current = -1;
    for (int i = 0; i < shown.rows.size(); ++i) {
        const Row &row = shown.rows.at(i);
        if (row.separatorBefore) box->insertSeparator(box->count());
        box->addItem(row.text, row.data);
        const int at = box->count() - 1;
        if (!row.tooltip.isEmpty()) box->setItemData(at, row.tooltip, Qt::ToolTipRole);
        if (!row.trailing.isEmpty()) box->setItemData(at, row.trailing, kTrailingItemRole);
        if (!row.enabled) {
            // The combo's own way of switching a row off: its model is a QStandardItemModel, and
            // the flag is what CurrentTextComboBox reads back out for the popup.
            if (auto *model = qobject_cast<QStandardItemModel *>(box->model()); model != nullptr)
                if (QStandardItem *item = model->item(at); item != nullptr) item->setEnabled(false);
        }
        if (i == shown.current) current = at;
    }
    if (current >= 0) box->setCurrentIndex(current);
    return current;
}

QString resolve(const models::Catalog &catalog, const QString &words)
{
    const QString query = words.trimmed();
    if (query.isEmpty()) return QString();
    const QList<models::Entry> rows = models::shown(catalog);
    // The same match the picker's filter makes, shown rows first: its key, its model id, or words
    // of its label and provider.
    const auto exact = [&query](const models::Entry &entry) {
        return entry.key.compare(query, Qt::CaseInsensitive) == 0
            || entry.model.compare(query, Qt::CaseInsensitive) == 0
            || entry.label.compare(query, Qt::CaseInsensitive) == 0;
    };
    for (const auto &entry : rows) if (exact(entry)) return entry.key;
    for (const auto &entry : catalog.entries) if (entry.usable && exact(entry)) return entry.key;
    for (const auto &entry : rows) if (models::matches(entry, query)) return entry.key;
    return QString();
}

}  // namespace relay::modelrows
