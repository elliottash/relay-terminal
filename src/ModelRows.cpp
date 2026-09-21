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

// ----- a pane's picks in its saved layout node (card #MDL1) ------------------------------------

QJsonObject modePicksToJson(const QHash<QString, ModePick> &picks)
{
    QJsonObject out;
    for (auto it = picks.cbegin(); it != picks.cend(); ++it) {
        if (it.key().isEmpty() || it.key() == QStringLiteral("main")) continue;   // main is the pane's own model
        QString preset, model;
        if (!models::Catalog::splitKey(it.value().key, &preset, &model)) continue;
        QJsonObject entry{{QStringLiteral("preset"), preset}, {QStringLiteral("model"), model}};
        if (!it.value().effort.isEmpty()) entry.insert(QStringLiteral("effort"), it.value().effort);
        out.insert(it.key(), entry);
    }
    return out;
}

QHash<QString, ModePick> modePicksFromJson(const QJsonValue &saved)
{
    QHash<QString, ModePick> out;
    if (!saved.isObject()) return out;
    const QJsonObject object = saved.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.key().isEmpty() || it.key() == QStringLiteral("main")) continue;
        if (!it.value().isObject()) continue;
        const QJsonObject entry = it.value().toObject();
        const QString preset = entry.value(QStringLiteral("preset")).toString();
        const QString model = entry.value(QStringLiteral("model")).toString();
        if (preset.isEmpty() || model.isEmpty()) continue;
        out.insert(it.key(), ModePick{models::Catalog::keyFor(preset, model),
                                      entry.value(QStringLiteral("effort")).toString()});
    }
    return out;
}

QHash<QString, ModePick> usableModePicks(const models::Catalog &catalog,
                                         const QHash<QString, ModePick> &picks)
{
    QHash<QString, ModePick> out;
    for (auto it = picks.cbegin(); it != picks.cend(); ++it) {
        const models::Entry *entry = catalog.find(it.value().key);
        // Exhausted is deliberately allowed through: a spent subscription is a row the box greys
        // and a reset brings back, not a pick the user has abandoned.
        if (entry != nullptr && entry->usable) out.insert(it.key(), it.value());
    }
    return out;
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
    // Owner, 2026-09-21: "no need to show the model class in the pane header". The chip is the
    // model and nothing else, on every mode — the class is in the tooltip (`collapsedTooltip`),
    // and the box itself says it, since the highlighted row sits under its class's header. With no
    // model known at all the mode's own word is better than an empty chip.
    if (model.isEmpty()) return context.mode == QStringLiteral("main") ? QString() : context.mode;
    return model;
}

QString collapsedTooltip(const Context &context)
{
    if (context.mode.isEmpty() || context.mode == QStringLiteral("main")) return QString();
    return QStringLiteral("This pane runs on the %1 list.").arg(context.mode);
}

int indexOf(const QList<Row> &rows, const QString &data)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).data == data) return i;
    return -1;
}

namespace {

// One class's models as the box draws them: list order, one row per model, and **nothing that
// cannot take a turn** (owner, 2026-09-21: "exhausted models dont show up"). That is the one place
// this module differs from the dialog, which keeps a spent row greyed in its rank.
QList<models::Group> liveGroupsOf(const Context &context, const QString &klass, qint64 now)
{
    QList<models::Group> out;
    for (const models::Group &group : models::grouped(context.catalog, modeEntries(context, klass), now))
        if (!group.spent(context.catalog, now)) out << group;
    return out;
}

bool holdsKey(const models::Group &group, const QString &key)
{
    if (key.isEmpty()) return false;
    return std::any_of(group.entries.cbegin(), group.entries.cend(),
                       [&key](const models::Entry &entry) { return entry.key == key; });
}

// The groups of one class the box draws: the first `cutoff` of them, plus this pane's own model
// wherever it sits, because the box opens with that row highlighted and the highlight needs a home.
// Expanded, it is the whole list.
QList<models::Group> shownGroupsOf(const QList<models::Group> &live, int cutoff, bool expanded,
                                   const QString &paneKey)
{
    if (expanded) return live;
    QList<models::Group> out = live.mid(0, qMax(1, cutoff));
    for (int i = qMax(1, cutoff); i < live.size(); ++i)
        if (holdsKey(live.at(i), paneKey)) out << live.at(i);
    return out;
}

// The same rule one step further out: a class switched off in the dialog is not drawn at all —
// unless it is the class this pane is running, when its own row is drawn and nothing else. The
// highlight has to have a home wherever the pane is (design 5.3), and a box that opens with
// nothing selected is a box that says the pane is running nothing.
QList<models::Group> paneRowOnly(const QList<models::Group> &live, const QString &paneKey)
{
    QList<models::Group> out;
    for (const models::Group &group : live)
        if (holdsKey(group, paneKey)) out << group;
    return out;
}

}  // namespace

bool expandable(const Context &context, const QString &klass)
{
    // `other models` is not a class: it has no list, no cutoff and no switch, and it is already
    // whole (`filtered`). Right on one of its rows does nothing rather than redrawing the box.
    if (klass == otherGroup()) return false;
    if (!models::curation::boxShown(klass)) return false;
    const qint64 now = context.now > 0 ? context.now : QDateTime::currentSecsSinceEpoch();
    const QList<models::Group> live = liveGroupsOf(context, klass, now);
    return live.size() > shownGroupsOf(live, models::curation::boxCutoff(klass), false,
                                       modeKey(context, klass)).size();
}

Box box(const Context &context)
{
    Box out;
    const qint64 now = context.now > 0 ? context.now : QDateTime::currentSecsSinceEpoch();
    bool first = true;
    // 1. One section per class: a header that is a label, then that class's list down to its
    //    cutoff. A class switched off is not drawn, and neither is one with nothing left to draw
    //    once the spent and keyless rows are gone — an empty header is a promise of nothing.
    for (const QString &klass : context.classes) {
        if (klass == QStringLiteral("lite")) continue;          // never a pane mode, never in the box
        // A class switched off in the dialog is not drawn — unless this pane is running it, when
        // its own row is drawn alone, the same way a pane's model below the cutoff is spared: the
        // box opens on that row highlighted, so the highlight needs a home.
        const bool classOn = models::curation::boxShown(klass);
        if (!classOn && klass != context.mode) continue;
        const QList<models::Group> live = liveGroupsOf(context, klass, now);
        const QString paneKey = modeKey(context, klass);
        const bool expanded = classOn && context.expanded.contains(klass);
        const QList<models::Group> shown =
            classOn ? shownGroupsOf(live, models::curation::boxCutoff(klass), expanded, paneKey)
                    : paneRowOnly(live, paneKey);
        if (shown.isEmpty()) continue;
        Row head;
        head.text = klass;
        head.data = QStringLiteral("class:") + klass;
        head.group = klass;
        head.header = true;
        head.enabled = false;   // a label: Up and Down step over it and it is never highlighted
        // The design's "› / ⌄ at the right to say it expands". `trailing` is the column the
        // popup already draws at the far end in the muted ink, so it needs no third mechanism.
        if (classOn && live.size() > shown.size()) head.trailing = QStringLiteral("\u203a");
        else if (classOn && expanded && live.size() > models::curation::boxCutoff(klass)) head.trailing = QStringLiteral("\u2304");
        head.tooltip = !classOn
            ? QStringLiteral("the %1 list is hidden from this box; this pane is running it").arg(klass)
            : head.trailing.isEmpty()
                ? QStringLiteral("the %1 list").arg(klass)
                : QStringLiteral("the %1 list · → shows all %2, ← goes back to %3")
                      .arg(klass).arg(live.size()).arg(models::curation::boxCutoff(klass));
        out.rows << head;
        for (const models::Group &group : shown) {
            const bool holdsPane = holdsKey(group, paneKey);
            const models::Entry preferred = group.preferred(context.catalog, now);
            Row row;
            row.text = group.name.isEmpty() ? preferred.model : group.name;
            row.data = QStringLiteral("pick:%1|%2").arg(klass, holdsPane ? paneKey : preferred.key);
            row.trailing = viaText(group, context.catalog, now);
            row.tooltip = viaTooltip(group, context.catalog, now);
            row.group = klass;
            row.enabled = true;   // a row the box draws at all is a row that can take the turn
            if (holdsPane && klass == context.mode) out.current = int(out.rows.size());
            out.rows << row;
        }
        first = false;
    }
    // 2. Guest agents (26.9) the worker cannot run as a harness, plus whichever one is in the
    //    pane. A guest the worker *can* run is a catalog entry and is already one of the rows
    //    above. They belong under the classes: a guest is the pane's own agent, not a tier.
    bool firstGuest = true;
    for (const QString &id : context.guests) {
        Row row;
        row.text = context.guestText.value(id, id);
        row.data = QStringLiteral("guest:") + id;
        row.separatorBefore = firstGuest && !first;
        row.enabled = true;
        firstGuest = false;
        out.rows << row;
    }
    // 3. The dialog (every model, with the lists, the levels and the providers). "customize…" is
    //    gone from the box with this design: the dialog has the Options button (design 5.3, 5.5).
    Row more;
    more.text = QStringLiteral("more models…");
    more.data = QStringLiteral("gear:picker");
    more.separatorBefore = true;
    more.enabled = true;
    out.rows << more;
    // The caller's own current row wins where it has one (a guest in the pane's foreground).
    if (!context.current.isEmpty())
        if (const int at = indexOf(out.rows, context.current); at >= 0) out.current = at;
    return out;
}

QString otherGroup() { return QStringLiteral("other"); }

Box filtered(const Context &context)
{
    // Every class whole. The cutoff says what the box draws at rest (step 4); it was never meant
    // to say what you may *type* — the owner's report is exactly that ("its supposed to show all
    // available models, not just the ones selected for the box picker").
    Context wide = context;
    for (const QString &klass : wide.classes) wide.expanded.insert(klass);
    Box out = box(wide);
    // Where the classes end: the guest rows and "more models…" carry no group, and `other models`
    // belongs with the models, above them.
    int at = out.rows.size();
    for (int i = 0; i < out.rows.size(); ++i)
        if (out.rows.at(i).group.isEmpty()) { at = i; break; }
    // Every name the classes already offer. By name, not by key: a model served by three providers
    // is one row (rule 2), and offering it again under `other models` because the class row picked
    // a different provider would be the same model twice.
    QSet<QString> listed;
    for (int i = 0; i < at; ++i)
        if (!out.rows.at(i).header) listed.insert(out.rows.at(i).text);
    const qint64 now = context.now > 0 ? context.now : QDateTime::currentSecsSinceEpoch();
    QList<Row> others;
    // `models::shown` is step 2: every **available** usable entry, in rank order. A row whose every
    // provider is spent is dropped, exactly as it is inside a class — the box is the answer to
    // "what can I run right now", filter or no filter.
    for (const models::Group &group : models::grouped(context.catalog, models::shown(context.catalog), now)) {
        const models::Entry preferred = group.preferred(context.catalog, now);
        const QString name = group.name.isEmpty() ? preferred.model : group.name;
        if (listed.contains(name) || group.spent(context.catalog, now)) continue;
        Row row;
        row.text = name;
        // Main, because a model in no list is not in a class: it becomes this pane's own model.
        row.data = QStringLiteral("pick:main|%1").arg(preferred.key);
        row.trailing = viaText(group, context.catalog, now);
        row.tooltip = viaTooltip(group, context.catalog, now);
        row.group = otherGroup();
        row.enabled = true;
        others << row;
    }
    if (others.isEmpty()) return out;
    Row head;
    head.text = QStringLiteral("other models");
    head.data = QStringLiteral("class:") + otherGroup();
    head.group = otherGroup();
    head.header = true;
    head.enabled = false;
    head.tooltip = QStringLiteral("every model you have made available that none of your lists names");
    out.rows.insert(at, head);
    for (int i = 0; i < others.size(); ++i) out.rows.insert(at + 1 + i, others.at(i));
    // The highlighted row moved down by however many rows went in above it.
    if (out.current >= at) out.current += others.size() + 1;
    return out;
}

QList<Row> build(const Context &context)
{
    return box(context).rows;
}

int fill(QComboBox *combo, const Context &context)
{
    if (combo == nullptr) return -1;
    combo->clear();
    const Box shown = box(context);
    int current = -1;
    for (int i = 0; i < shown.rows.size(); ++i) {
        const Row &row = shown.rows.at(i);
        if (row.separatorBefore) combo->insertSeparator(combo->count());
        combo->addItem(row.text, row.data);
        const int at = combo->count() - 1;
        if (!row.tooltip.isEmpty()) combo->setItemData(at, row.tooltip, Qt::ToolTipRole);
        if (!row.trailing.isEmpty()) combo->setItemData(at, row.trailing, kTrailingItemRole);
        if (!row.group.isEmpty()) combo->setItemData(at, row.group, kGroupItemRole);
        if (row.header) combo->setItemData(at, true, kHeaderItemRole);
        if (!row.enabled) {
            // The combo's own way of switching a row off: its model is a QStandardItemModel, and
            // the flag is what CurrentTextComboBox reads back out for the popup.
            if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()); model != nullptr)
                if (QStandardItem *item = model->item(at); item != nullptr) item->setEnabled(false);
        }
        if (i == shown.current) current = at;
    }
    if (current >= 0) combo->setCurrentIndex(current);
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
    // Available first (step 2), then everything usable: a model you un-ticked is un-ticked in the
    // lists and the box, not forbidden — typing its name is asking for that model (card #MDL1).
    for (const auto &entry : models::allUsable(catalog)) if (models::matches(entry, query)) return entry.key;
    return QString();
}

}  // namespace relay::modelrows
