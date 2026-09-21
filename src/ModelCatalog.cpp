// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelCatalog.h"

#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>

#include <algorithm>
#include <limits>

namespace relay::models {

namespace {

const QString kPriority = QStringLiteral("models/priority");
const QString kCustom = QStringLiteral("models/custom");
const QString kFavorites = QStringLiteral("models/favorites");
const QString kRecent = QStringLiteral("models/recent");
const QString kSort = QStringLiteral("models/sort");
const QString kOpenrouter = QStringLiteral("models/openrouter_fallback");
const QString kProviderOrder = QStringLiteral("models/provider_order");
const QString kProfileOrder = QStringLiteral("models/profile_order");
const QString kProfile = QStringLiteral("models/profile");
const QString kThreshold = QStringLiteral("models/fallback_threshold");
constexpr int kDefaultThreshold = 2;
constexpr int kRecentCap = 10;

QString str(const QJsonObject &object, const char *field) { return object.value(QLatin1String(field)).toString(); }

QStringList list(const QString &key) { return QSettings().value(key).toStringList(); }
void store(const QString &key, const QStringList &value) {
    if (value.isEmpty()) QSettings().remove(key);
    else QSettings().setValue(key, value);
}
QString perKey(const QString &group, const QString &key) { return QStringLiteral("models/") + group + QLatin1Char('/') + key; }

// A preset row is usable the way Pane::m_stored decides it: a stored key, a local server, a
// runnable guest harness, or Relay Free while the worker reports it available.
bool usableRow(const QJsonObject &preset) {
    const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
    return preset.value(QStringLiteral("has_stored_key")).toBool()
        || preset.value(QStringLiteral("local")).toBool()
        || preset.value(QStringLiteral("harness")).toBool()
        || (hosted && preset.value(QStringLiteral("available")).toBool());
}

QList<LimitWindow> windowsOf(const QJsonObject &preset) {
    QList<LimitWindow> windows;
    // The worker's guest row carries `limits: {windows, status?, updated_at}` (protocol 29.3): the
    // last usage_limits event as it held it. A bare array of windows is read too.
    const QJsonValue held = preset.value(QStringLiteral("limits"));
    const QJsonArray limits = held.isObject() ? held.toObject().value(QStringLiteral("windows")).toArray() : held.toArray();
    for (const auto &value : limits) {
        const QJsonObject window = value.toObject();
        LimitWindow w;
        w.kind = str(window, "kind");
        w.usedPercent = window.contains(QStringLiteral("used_percent")) ? window.value(QStringLiteral("used_percent")).toDouble() : -1;
        w.resetsAt = window.value(QStringLiteral("resets_at")).toVariant().toLongLong();
        if (!w.kind.isEmpty()) windows << w;
    }
    // Relay Free reports one allowance as {limit, used, resets_at} (protocol 13.9): one window.
    const QJsonObject quota = preset.value(QStringLiteral("quota")).toObject();
    if (windows.isEmpty() && !quota.isEmpty() && quota.value(QStringLiteral("limit")).toDouble() > 0) {
        LimitWindow w;
        w.kind = QStringLiteral("daily");
        w.usedPercent = 100.0 * quota.value(QStringLiteral("used")).toDouble() / quota.value(QStringLiteral("limit")).toDouble();
        w.resetsAt = quota.value(QStringLiteral("resets_at")).toVariant().toLongLong();
        windows << w;
    }
    return windows;
}

}  // namespace

// ----- Entry / Catalog ------------------------------------------------------------------------

QString nameOf(const QString &modelId) {
    QString text = modelId.trimmed();
    while (text.startsWith(QLatin1Char('~'))) text.remove(0, 1);
    const int slash = text.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) text = text.mid(slash + 1);
    while (text.startsWith(QLatin1Char('~'))) text.remove(0, 1);
    static const QRegularExpression space(QStringLiteral("\\s+"));
    return text.trimmed().toLower().replace(space, QStringLiteral("-"));
}

QString Entry::displayName() const {
    if (provider.isEmpty()) return name;
    return name + QStringLiteral(" · ") + provider;
}

QString Catalog::keyFor(const QString &preset, const QString &model) { return preset + QLatin1Char('|') + model; }

bool Catalog::splitKey(const QString &key, QString *preset, QString *model) {
    const int bar = key.indexOf(QLatin1Char('|'));
    if (bar <= 0 || bar == key.size() - 1) return false;
    if (preset) *preset = key.left(bar);
    if (model) *model = key.mid(bar + 1);
    return true;
}

const Entry *Catalog::find(const QString &key) const {
    for (const Entry &entry : entries)
        if (entry.key == key) return &entry;
    return nullptr;
}

QList<Entry> Catalog::ofPreset(const QString &preset) const {
    QList<Entry> out;
    for (const Entry &entry : entries)
        if (entry.preset == preset) out << entry;
    return out;
}

QStringList Catalog::presets() const {
    QStringList out;
    for (const Entry &entry : entries)
        if (!out.contains(entry.preset)) out << entry.preset;
    return out;
}

const Entry *Catalog::tierEntry(const QString &preset, const QString &tier) const {
    for (const Entry &entry : entries)
        if (entry.preset == preset && entry.tier == tier) return &entry;
    return nullptr;
}

QString Catalog::resolveKey(const QString &preset, const QString &reportedModel) const {
    if (preset.isEmpty() || reportedModel.isEmpty()) return QString();
    for (const Entry &entry : entries)
        if (entry.preset == preset && entry.model == reportedModel) return entry.key;
    // The alias knowledge is already in the row: the worker names `guest:claude|opus`
    // "claude-opus-5", which is what the CLI reports once it is running, so comparing names is
    // comparing what the two spellings mean. `reportedModel` is compared as it stands too, for a
    // row whose name is itself the alias (a guest whose family nothing maps).
    const QString wanted = nameOf(reportedModel);
    for (const Entry &entry : entries) {
        if (entry.preset != preset) continue;
        if (entry.name.compare(wanted, Qt::CaseInsensitive) == 0
            || entry.name.compare(reportedModel, Qt::CaseInsensitive) == 0)
            return entry.key;
    }
    return QString();
}

Catalog catalogFrom(const QJsonArray &presets) {
    Catalog catalog;
    for (const auto &value : presets) {
        const QJsonObject preset = value.toObject();
        const QString id = str(preset, "id");
        if (id.isEmpty()) continue;
        const bool usable = usableRow(preset);
        const bool guest = id.startsWith(QStringLiteral("guest:"));
        const bool local = preset.value(QStringLiteral("local")).toBool();
        const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
        // The provider's name, lower-case (the worker sends it so since 78644559; older workers
        // and guest rows are folded here so the picker never mixes cases).
        QString provider = str(preset, "provider");
        if (provider.isEmpty()) provider = str(preset, "label").section(QStringLiteral(" · "), 0, 0);
        provider = provider.toLower();
        catalog.presetLabels.insert(id, str(preset, "label").toLower());
        const QList<LimitWindow> windows = windowsOf(preset);
        if (!windows.isEmpty()) catalog.limits.insert(id, windows);
        // A guest row's `limits` is the worker's copy of the last usage_limits event, status included.
        const QString status = str(preset.value(QStringLiteral("limits")).toObject(), "status");
        if (!status.isEmpty()) catalog.status.insert(id, status);

        QJsonArray models = preset.value(QStringLiteral("models")).toArray();
        if (models.isEmpty()) {
            // No catalog: the row's own model is the one entry, so nothing the pane could switch
            // to today is lost. A guest with no list yet (codex's scan still running) has no model
            // id either; its entry is the guest itself, which the pane switches to as before.
            QJsonObject own{{QStringLiteral("id"), str(preset, "model")},
                            {QStringLiteral("name"), str(preset, "model").isEmpty() ? nameOf(str(preset, "label")) : nameOf(str(preset, "model"))},
                            {QStringLiteral("efforts"), preset.value(QStringLiteral("efforts"))}};
            models << own;
        }
        for (const auto &item : std::as_const(models)) {
            const QJsonObject row = item.toObject();
            Entry entry;
            entry.preset = id;
            entry.model = str(row, "id");
            entry.key = Catalog::keyFor(id, entry.model);
            // One name per model (card #MDL1): the worker's `name`, else the same derivation here,
            // so a row from an older worker and a row from this one fold into the same group.
            entry.name = str(row, "name").isEmpty() ? nameOf(entry.model) : str(row, "name").toLower();
            entry.label = entry.name;
            entry.provider = provider;
            entry.plan = str(preset, "plan").toLower();
            entry.tier = str(row, "tier");
            for (const auto &level : row.value(QStringLiteral("efforts")).toArray()) entry.efforts << level.toString();
            entry.defaultEffort = str(row, "default_effort");
            {
                const QJsonObject tiers = row.value(QStringLiteral("tier_effort")).toObject();
                for (auto it = tiers.begin(); it != tiers.end(); ++it)
                    if (it.value().isString() && !it.value().toString().isEmpty())
                        entry.tierEffort.insert(it.key(), it.value().toString());
            }
            entry.intelligence = row.value(QStringLiteral("intelligence")).isDouble() ? row.value(QStringLiteral("intelligence")).toInt() : -1;
            entry.openrouter = str(row, "openrouter");
            {
                const QJsonObject labels = row.value(QStringLiteral("effort_labels")).toObject();
                for (auto it = labels.begin(); it != labels.end(); ++it) entry.effortLabels.insert(it.key(), it.value().toString());
            }
            entry.usable = usable;
            entry.openEnded = models.size() > 6;
            entry.guest = guest;
            entry.local = local;
            entry.hosted = hosted;
            catalog.entries << entry;
        }
        // A preset with a catalog but no row for its own default model (a stored id the catalog
        // does not know, from Model roles' free-text box) is still switchable to.
        const QString own = str(preset, "model");
        if (!own.isEmpty() && !catalog.find(Catalog::keyFor(id, own))) {
            Entry entry;
            entry.preset = id; entry.model = own; entry.key = Catalog::keyFor(id, own);
            entry.name = nameOf(own); entry.label = entry.name;
            entry.provider = provider; entry.plan = str(preset, "plan").toLower();
            for (const auto &level : preset.value(QStringLiteral("efforts")).toArray()) entry.efforts << level.toString();
            entry.usable = usable; entry.guest = guest; entry.local = local; entry.hosted = hosted;
            catalog.entries << entry;
        }
    }
    // The user's own additions: an id typed on the Models page, on a preset the worker knows.
    for (const QString &key : curation::customKeys()) {
        QString preset, model;
        if (!Catalog::splitKey(key, &preset, &model) || catalog.find(key)) continue;
        const QList<Entry> siblings = catalog.ofPreset(preset);
        if (siblings.isEmpty()) continue;   // the preset is gone; the key stays until the user removes it
        Entry entry = siblings.first();
        entry.key = key; entry.model = model; entry.name = nameOf(model); entry.label = entry.name;
        entry.tier.clear();
        entry.intelligence = -1; entry.custom = true;
        catalog.entries << entry;
    }
    return catalog;
}

// The level a model starts at when it is added to a list by hand (card #TKN7). The worker computes
// it per row (`tier_effort`, presets.tier_start_efforts) because two of its three rules are not
// readable off the row: Main is the provider's own default level — codex's `default_reasoning_level`
// is `low` for gpt-5.6-sol — and a codex model's High is `xhigh`, not its top level, which is
// `ultra` (a delegation mode, and the owner's report: "it defaulted effort to ultra reasoning").
QString tierStartEffort(const Entry &entry, const QString &tier) {
    const QString listed = entry.tierEffort.value(tier);
    if (!listed.isEmpty()) return listed;
    if (entry.efforts.isEmpty()) return QString();
    if (tier == QStringLiteral("main"))                       // the provider's own default
        return entry.efforts.contains(entry.defaultEffort) ? entry.defaultEffort : QString();
    if (tier == QStringLiteral("high")) return entry.efforts.last();    // the hardest reasoning
    return entry.efforts.first();       // flash and lite: the lowest level, "with no reasoning"
}

// ----- sorts -----------------------------------------------------------------------------------

QString sortId(Sort sort) {
    switch (sort) {
    case Sort::Priority: return QStringLiteral("priority");
    case Sort::Alphabetical: return QStringLiteral("alpha");
    case Sort::Intelligence: return QStringLiteral("intelligence");
    case Sort::Speed: return QStringLiteral("speed");
    case Sort::Usage: return QStringLiteral("usage");
    case Sort::Remaining: return QStringLiteral("remaining");
    }
    return QStringLiteral("priority");
}

Sort sortFromId(const QString &id) {
    for (Sort sort : allSorts())
        if (sortId(sort) == id) return sort;
    return Sort::Priority;
}

QString sortLabel(Sort sort) {
    switch (sort) {
    case Sort::Priority: return QStringLiteral("priority");
    case Sort::Alphabetical: return QStringLiteral("a to z");
    case Sort::Intelligence: return QStringLiteral("intelligence");
    case Sort::Speed: return QStringLiteral("speed");
    case Sort::Usage: return QStringLiteral("most used");
    case Sort::Remaining: return QStringLiteral("subscription left");
    }
    return QString();
}

QList<Sort> allSorts() {
    return {Sort::Priority, Sort::Alphabetical, Sort::Intelligence, Sort::Speed, Sort::Usage, Sort::Remaining};
}

// ----- curation --------------------------------------------------------------------------------

namespace curation {
// Whether any tier list names this entry (an open-ended provider's tail shows only then).
bool inAnyList(const QString &key);

QStringList priority() { return list(kPriority); }

QStringList ranked(const Catalog &catalog) {
    QStringList out;
    // The main list leads (owner, 2026-09-20), then the other tiers' models, then the rest.
    for (const QString &tier : tierIds())
        for (const TierEntry &entry : tierList(tier))
            if (catalog.find(entry.key) && !out.contains(entry.key)) out << entry.key;
    // The single list the five tier lists replaced. Nothing writes it any more, so once the lists
    // exist a leftover copy is only a way for an order nobody can see to outrank the one on the
    // page (card #MDL1): it is read on an install that has stored no list at all, and there only.
    if (!tierListsSet())
        for (const QString &key : priority())
            if (catalog.find(key) && !out.contains(key)) out << key;
    // The default order behind the explicit list. `provider/preset` is deliberately *not* read
    // here (card #MDL1, rule 3): it is the last provider some pane switched to, and using it to
    // lead every picker is half of why a new pane and /swap disagreed about rank 1.
    auto add = [&](const Entry *entry) { if (entry && !out.contains(entry->key)) out << entry->key; };
    for (const QString &preset : catalog.presets()) {
        const Entry *main = catalog.tierEntry(preset, QStringLiteral("main"));
        if (main && main->usable) add(main);
    }
    for (const Entry &entry : catalog.entries) add(&entry);
    return out;
}

void setRank(const QString &key, int rank, const Catalog &catalog) {
    QStringList keys = ranked(catalog);
    keys.removeAll(key);
    keys.insert(qBound(0, rank, keys.size()), key);
    store(kPriority, keys);
}

void move(const QString &key, int delta, const Catalog &catalog) {
    const QStringList keys = ranked(catalog);
    const int at = keys.indexOf(key);
    if (at < 0) return;
    setRank(key, at + delta, catalog);
}

void resetPriority() { QSettings().remove(kPriority); }

QStringList customKeys() { return list(kCustom); }

Entry addCustom(const QString &preset, const QString &model, const Catalog &catalog) {
    const QString key = Catalog::keyFor(preset, model.trimmed());
    QStringList keys = customKeys();
    if (!catalog.find(key) && !keys.contains(key)) { keys << key; store(kCustom, keys); }
    Entry entry;
    entry.key = key; entry.preset = preset; entry.model = model.trimmed(); entry.label = entry.model.toLower();
    entry.custom = true;
    const QList<Entry> siblings = catalog.ofPreset(preset);
    if (!siblings.isEmpty()) {
        const Entry &sibling = siblings.first();
        entry.provider = sibling.provider; entry.plan = sibling.plan; entry.usable = sibling.usable;
        entry.guest = sibling.guest; entry.local = sibling.local; entry.hosted = sibling.hosted;
    }
    return entry;
}

void removeCustom(const QString &key) {
    QStringList keys = customKeys();
    keys.removeAll(key);
    store(kCustom, keys);
    QStringList ranks = priority();
    ranks.removeAll(key);
    store(kPriority, ranks);
}

QStringList favorites() { return list(kFavorites); }
bool isFavorite(const QString &key) { return favorites().contains(key); }
void toggleFavorite(const QString &key) {
    QStringList keys = favorites();
    if (keys.contains(key)) keys.removeAll(key);
    else keys << key;
    store(kFavorites, keys);
}

QStringList recent() { return list(kRecent); }

void noteUse(const QString &key) {
    if (key.isEmpty()) return;
    QStringList keys = recent();
    keys.removeAll(key);
    keys.prepend(key);
    while (keys.size() > kRecentCap) keys.removeLast();
    store(kRecent, keys);
    QSettings settings;
    const QString counter = perKey(QStringLiteral("uses"), key);
    settings.setValue(counter, settings.value(counter, 0).toInt() + 1);
}

int uses(const QString &key) { return QSettings().value(perKey(QStringLiteral("uses"), key), 0).toInt(); }

double speed(const QString &key) { return QSettings().value(perKey(QStringLiteral("speed"), key), 0.0).toDouble(); }

void noteSpeed(const QString &key, double tokensPerSecond) {
    if (key.isEmpty() || !(tokensPerSecond > 0)) return;
    // A running average that leans on what was measured before: one slow turn on a busy evening
    // should not move a model three places down the list.
    const double old = speed(key);
    const double value = old > 0 ? old * 0.7 + tokensPerSecond * 0.3 : tokensPerSecond;
    QSettings().setValue(perKey(QStringLiteral("speed"), key), value);
}

QStringList openrouterFallbackKeys() { return list(kOpenrouter); }
bool openrouterFallback(const QString &key) { return openrouterFallbackKeys().contains(key); }
void setOpenrouterFallback(const QString &key, bool on) {
    QStringList keys = openrouterFallbackKeys();
    keys.removeAll(key);
    if (on) keys << key;
    store(kOpenrouter, keys);
}
QStringList openrouterFallbackModels() {
    QStringList models;
    for (const QString &key : openrouterFallbackKeys()) {
        QString preset, model;
        if (Catalog::splitKey(key, &preset, &model) && !models.contains(model)) models << model;
    }
    return models;
}

QStringList providerOrder() { return list(kProviderOrder); }
void noteProviders(const QStringList &listedIds) {
    QStringList order = providerOrder();
    bool changed = false;
    for (const QString &id : listedIds)
        if (!order.contains(id)) { order << id; changed = true; }
    if (changed) store(kProviderOrder, order);
}
void moveProviderBefore(const QString &id, const QString &beforeId) {
    QStringList order = providerOrder();
    order.removeAll(id);
    const int at = beforeId.isEmpty() ? -1 : order.indexOf(beforeId);
    if (at < 0) order << id;
    else order.insert(at, id);
    store(kProviderOrder, order);
}

QStringList tierIds() {
    return {QStringLiteral("main"), QStringLiteral("high"), QStringLiteral("flash"), QStringLiteral("lite"), QStringLiteral("local")};
}
QString tierLabel(const QString &tier) { return tier + QStringLiteral(" models"); }
static QString tierKey(const QString &tier) { return QStringLiteral("models/tier/") + tier; }
// One profile's copy of one list. The name goes in the key, which is why `validProfileName` keeps
// "/" and "\" out of it: either would split the name across QSettings groups.
static QString profileTierKey(const QString &name, const QString &tier) {
    return QStringLiteral("models/profiles/") + name + QStringLiteral("/tier/") + tier;
}
bool tierListsSet() {
    QSettings settings;
    for (const QString &tier : tierIds()) if (settings.contains(tierKey(tier))) return true;
    return false;
}
QList<TierEntry> tierList(const QString &tier) {
    QList<TierEntry> out;
    for (const QString &item : list(tierKey(tier))) {
        const int last = item.lastIndexOf(QLatin1Char('|'));
        if (last <= 0) continue;
        TierEntry entry{item.left(last), item.mid(last + 1)};
        QString preset, model;
        if (Catalog::splitKey(entry.key, &preset, &model)) out << entry;
    }
    return out;
}
void setTierList(const QString &tier, const QList<TierEntry> &entries) {
    QStringList items;
    for (const TierEntry &entry : entries) items << entry.key + QLatin1Char('|') + entry.effort;
    // An emptied list is stored as empty, not forgotten: "no fallbacks here" is a choice, and a
    // forgotten key would bring the defaults back.
    QSettings().setValue(tierKey(tier), items);
    // The live lists and the current profile are one thing (owner, 2026-09-20 evening): an edit
    // made while a profile is current is an edit *of* that profile. Nothing to save, nothing to
    // lose by switching away. This is the single writer every other tier function goes through.
    if (const QString name = currentProfile(); !name.isEmpty())
        QSettings().setValue(profileTierKey(name, tier), items);
}
void addToTier(const QString &tier, const QString &key, const QString &effort) {
    QList<TierEntry> entries = tierList(tier);
    for (const TierEntry &entry : entries) if (entry.key == key) return;
    entries << TierEntry{key, effort};
    setTierList(tier, entries);
}
void removeFromTier(const QString &tier, const QString &key) {
    QList<TierEntry> entries = tierList(tier);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const TierEntry &e) { return e.key == key; }), entries.end());
    setTierList(tier, entries);
}
void moveInTier(const QString &tier, const QString &key, int toIndex) {
    QList<TierEntry> entries = tierList(tier);
    int from = -1;
    for (int i = 0; i < entries.size(); ++i) if (entries.at(i).key == key) from = i;
    if (from < 0) return;
    const TierEntry moved = entries.takeAt(from);
    entries.insert(qBound(0, toIndex > from ? toIndex - 1 : toIndex, entries.size()), moved);
    setTierList(tier, entries);
}
void setTierEffort(const QString &tier, const QString &key, const QString &effort) {
    QList<TierEntry> entries = tierList(tier);
    for (TierEntry &entry : entries) if (entry.key == key) entry.effort = effort;
    setTierList(tier, entries);
}
bool inAnyList(const QString &key) {
    for (const QString &tier : tierIds())
        for (const TierEntry &entry : tierList(tier))
            if (entry.key == key) return true;
    return false;
}
QString listEffortFor(const QString &key) {
    for (const QString &tier : tierIds())
        for (const TierEntry &entry : tierList(tier))
            if (entry.key == key) return entry.effort;
    return QString();
}
// ----- what the box shows of each list (card #MDL1, design 5.3) --------------------------------

QStringList boxClasses() {
    // The design's order, and `lite` is not among them: it is never a pane mode, so the box has
    // never had a row for it and it gets no cutoff of its own.
    return {QStringLiteral("high"), QStringLiteral("main"), QStringLiteral("flash"), QStringLiteral("local")};
}
static QString boxCutoffKey(const QString &tier) { return QStringLiteral("models/box/") + tier; }
static QString boxOffKey(const QString &tier) { return QStringLiteral("models/box_off/") + tier; }
static QString profileBoxCutoffKey(const QString &name, const QString &tier) {
    return QStringLiteral("models/profiles/") + name + QStringLiteral("/box/") + tier;
}
static QString profileBoxOffKey(const QString &name, const QString &tier) {
    return QStringLiteral("models/profiles/") + name + QStringLiteral("/box_off/") + tier;
}
int boxCutoff(const QString &tier) {
    if (!boxClasses().contains(tier)) return kBoxCutoffDefault;
    return qMax(1, QSettings().value(boxCutoffKey(tier), kBoxCutoffDefault).toInt());
}
void setBoxCutoff(const QString &tier, int rank) {
    if (!boxClasses().contains(tier)) return;
    const int clean = qMax(1, rank);
    QSettings settings;
    settings.setValue(boxCutoffKey(tier), clean);
    // Same invariant as setTierList: what the box shows belongs to the lists, so an edit made
    // while a profile is current is an edit *of* that profile.
    if (const QString name = currentProfile(); !name.isEmpty())
        settings.setValue(profileBoxCutoffKey(name, tier), clean);
}
bool boxShown(const QString &tier) {
    if (!boxClasses().contains(tier)) return true;
    return !QSettings().value(boxOffKey(tier), false).toBool();
}
void setBoxShown(const QString &tier, bool on) {
    if (!boxClasses().contains(tier)) return;
    QSettings settings;
    // Switching a class back on is the absence of the key again, so an install that never touched
    // it and one that switched it off and on look the same.
    if (on) settings.remove(boxOffKey(tier)); else settings.setValue(boxOffKey(tier), true);
    if (const QString name = currentProfile(); !name.isEmpty()) {
        if (on) settings.remove(profileBoxOffKey(name, tier));
        else settings.setValue(profileBoxOffKey(name, tier), true);
    }
}

void applyTierDefaults(const QJsonObject &lists) {
    for (const QString &tier : tierIds()) {
        QList<TierEntry> entries;
        for (const auto &value : lists.value(tier).toArray()) {
            const QJsonObject item = value.toObject();
            const QString preset = item.value(QStringLiteral("preset")).toString(), model = item.value(QStringLiteral("model")).toString();
            if (preset.isEmpty() || model.isEmpty()) continue;
            entries << TierEntry{Catalog::keyFor(preset, model), item.value(QStringLiteral("effort")).toString()};
        }
        setTierList(tier, entries);
    }
}
void clearTierLists() {
    QSettings settings;
    const QString name = currentProfile();
    for (const QString &tier : tierIds()) {
        settings.remove(tierKey(tier));
        // Same invariant as setTierList: what the current profile holds is what the lists hold.
        if (!name.isEmpty()) settings.remove(profileTierKey(name, tier));
    }
}

// ----- profiles ---------------------------------------------------------------------------------

bool validProfileName(const QString &name) {
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

QStringList profiles() { return list(kProfileOrder); }

QString currentProfile() {
    const QString name = QSettings().value(kProfile).toString();
    // A name left behind by a profile someone deleted (or an older Relay) is no profile at all:
    // answering it would make setTierList write through to a profile the page does not list.
    return profiles().contains(name) ? name : QString();
}

void saveProfile(const QString &name) {
    const QString clean = name.trimmed();
    if (!validProfileName(clean)) return;
    QSettings settings;
    for (const QString &tier : tierIds())
        settings.setValue(profileTierKey(clean, tier), settings.value(tierKey(tier)).toStringList());
    // What the box shows of each class travels with the lists (design 5.3): a profile whose main
    // list is four models deep and whose box shows three of them is one thing, not two.
    for (const QString &klass : boxClasses()) {
        settings.setValue(profileBoxCutoffKey(clean, klass), boxCutoff(klass));
        if (boxShown(klass)) settings.remove(profileBoxOffKey(clean, klass));
        else settings.setValue(profileBoxOffKey(clean, klass), true);
    }
    QStringList names = profiles();
    if (!names.contains(clean)) { names << clean; store(kProfileOrder, names); }
    settings.setValue(kProfile, clean);
}

void applyProfile(const QString &name) {
    if (!profiles().contains(name)) return;
    // Current first, so setTierList's write-through lands back in the profile it came from rather
    // than in whichever one was current a moment ago.
    QSettings().setValue(kProfile, name);
    QSettings settings;
    for (const QString &tier : tierIds()) {
        QList<TierEntry> entries;
        for (const QString &item : settings.value(profileTierKey(name, tier)).toStringList()) {
            const int last = item.lastIndexOf(QLatin1Char('|'));
            if (last <= 0) continue;
            entries << TierEntry{item.left(last), item.mid(last + 1)};
        }
        // Every tier is written, absent ones included: a profile is a whole snapshot, so a list it
        // holds nothing for is empty here too and not whatever the profile before it left behind.
        setTierList(tier, entries);
    }
    // And the same for the box: a class the profile says nothing about is back at the defaults,
    // not at whatever the profile before it showed.
    for (const QString &klass : boxClasses()) {
        setBoxCutoff(klass, settings.value(profileBoxCutoffKey(name, klass), kBoxCutoffDefault).toInt());
        setBoxShown(klass, !settings.value(profileBoxOffKey(name, klass), false).toBool());
    }
}

void renameProfile(const QString &from, const QString &to) {
    const QString clean = to.trimmed();
    QStringList names = profiles();
    if (!names.contains(from) || !validProfileName(clean) || (clean != from && names.contains(clean))) return;
    QSettings settings;
    for (const QString &tier : tierIds()) {
        settings.setValue(profileTierKey(clean, tier), settings.value(profileTierKey(from, tier)).toStringList());
        if (clean != from) settings.remove(profileTierKey(from, tier));
    }
    for (const QString &klass : boxClasses()) {
        settings.setValue(profileBoxCutoffKey(clean, klass),
                          settings.value(profileBoxCutoffKey(from, klass), kBoxCutoffDefault).toInt());
        if (settings.value(profileBoxOffKey(from, klass), false).toBool())
            settings.setValue(profileBoxOffKey(clean, klass), true);
        else
            settings.remove(profileBoxOffKey(clean, klass));
        if (clean != from) { settings.remove(profileBoxCutoffKey(from, klass)); settings.remove(profileBoxOffKey(from, klass)); }
    }
    names[names.indexOf(from)] = clean;     // renaming keeps its place in the list
    store(kProfileOrder, names);
    if (settings.value(kProfile).toString() == from) settings.setValue(kProfile, clean);
}

void deleteProfile(const QString &name) {
    QStringList names = profiles();
    if (!names.contains(name)) return;
    QSettings settings;
    for (const QString &tier : tierIds()) settings.remove(profileTierKey(name, tier));
    for (const QString &klass : boxClasses()) {
        settings.remove(profileBoxCutoffKey(name, klass));
        settings.remove(profileBoxOffKey(name, klass));
    }
    names.removeAll(name);
    store(kProfileOrder, names);
    // The lists themselves stay: this machine goes on running on what it was running on, unnamed.
    if (settings.value(kProfile).toString() == name) settings.remove(kProfile);
}

// ----- profiles on disk (owner, 2026-09-21: "allow exporting and importing profiles") -----------

static const QString kProfileDocMarker = QStringLiteral("model profiles");

// One list as the file carries it: the same {preset, model, effort} objects the worker sends as
// `tier_list_defaults`, so the two shapes never drift apart.
static QJsonArray listToJson(const QStringList &items) {
    QJsonArray rows;
    for (const QString &item : items) {
        const int last = item.lastIndexOf(QLatin1Char('|'));
        if (last <= 0) continue;
        QString preset, model;
        if (!Catalog::splitKey(item.left(last), &preset, &model)) continue;
        rows << QJsonObject{{QStringLiteral("preset"), preset}, {QStringLiteral("model"), model},
                            {QStringLiteral("effort"), item.mid(last + 1)}};
    }
    return rows;
}

QJsonObject exportProfiles(const QStringList &names) {
    const QStringList existing = profiles();
    QSettings settings;
    QJsonArray out;
    for (const QString &name : names) {
        if (!existing.contains(name)) continue;
        QJsonObject lists;
        // Every tier, empty ones included: a profile is a whole snapshot, and a reader that saw
        // "lite" missing could not tell "no lite models" from "this file predates the lite list".
        for (const QString &tier : tierIds())
            lists.insert(tier, listToJson(settings.value(profileTierKey(name, tier)).toStringList()));
        // What the box shows of each class, every class written out for the same reason the empty
        // lists are: a reader must not have to tell "two by default" from "this file is older".
        QJsonObject box;
        for (const QString &klass : boxClasses())
            box.insert(klass, QJsonObject{
                {QStringLiteral("cutoff"), qMax(1, settings.value(profileBoxCutoffKey(name, klass), kBoxCutoffDefault).toInt())},
                {QStringLiteral("shown"), !settings.value(profileBoxOffKey(name, klass), false).toBool()}});
        out << QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("lists"), lists},
                           {QStringLiteral("box"), box}};
    }
    if (out.isEmpty()) return {};
    return QJsonObject{{QStringLiteral("relay"), kProfileDocMarker},
                       {QStringLiteral("version"), 1},
                       {QStringLiteral("exported"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                       {QStringLiteral("profiles"), out}};
}

static ProfileDoc profileFromJson(const QJsonObject &object) {
    ProfileDoc doc;
    doc.name = object.value(QStringLiteral("name")).toString().trimmed();
    const QJsonObject lists = object.value(QStringLiteral("lists")).toObject();
    for (const QString &tier : tierIds()) {
        QList<TierEntry> entries;
        for (const auto &value : lists.value(tier).toArray()) {
            const QJsonObject item = value.toObject();
            QString preset = item.value(QStringLiteral("preset")).toString();
            QString model = item.value(QStringLiteral("model")).toString();
            // A hand-written file may carry the key whole, the way QSettings stores it.
            if (preset.isEmpty())
                Catalog::splitKey(item.value(QStringLiteral("key")).toString(), &preset, &model);
            if (preset.isEmpty() || model.isEmpty()) continue;
            entries << TierEntry{Catalog::keyFor(preset, model), item.value(QStringLiteral("effort")).toString()};
        }
        if (!entries.isEmpty()) doc.lists.insert(tier, entries);
    }
    const QJsonObject box = object.value(QStringLiteral("box")).toObject();
    for (const QString &klass : boxClasses()) {
        if (!box.value(klass).isObject()) continue;   // absent, or a shape we do not read: the defaults
        const QJsonObject item = box.value(klass).toObject();
        BoxSetting setting;
        if (item.value(QStringLiteral("cutoff")).isDouble())
            setting.cutoff = qMax(1, item.value(QStringLiteral("cutoff")).toInt(kBoxCutoffDefault));
        if (item.value(QStringLiteral("shown")).isBool())
            setting.shown = item.value(QStringLiteral("shown")).toBool();
        doc.box.insert(klass, setting);
    }
    return doc;
}

QList<ProfileDoc> readProfiles(const QJsonObject &document, QString *error) {
    const QList<ProfileDoc> none;
    auto fail = [&](const QString &why) { if (error) *error = why; return none; };
    if (error) error->clear();
    if (document.isEmpty())
        return fail(QStringLiteral("That file is empty, or is not JSON."));
    QJsonArray rows;
    if (document.contains(QStringLiteral("profiles"))) {
        if (document.value(QStringLiteral("relay")).toString() != kProfileDocMarker)
            return fail(QStringLiteral("That is JSON, but not a Relay model-profile file."));
        rows = document.value(QStringLiteral("profiles")).toArray();
    } else if (document.contains(QStringLiteral("lists"))) {
        rows << document;   // a lone profile, hand-written or cut out of a bigger file
    } else {
        return fail(QStringLiteral("That is JSON, but not a Relay model-profile file."));
    }
    // A newer `version` is read anyway: the shape only ever gains keys, and refusing a file a later
    // Relay wrote would be worse than importing the lists it does understand.
    QList<ProfileDoc> out;
    for (const auto &value : rows) {
        const ProfileDoc doc = profileFromJson(value.toObject());
        if (validProfileName(doc.name)) out << doc;
    }
    if (out.isEmpty())
        return fail(QStringLiteral("That file holds no profiles Relay can read."));
    return out;
}

void writeProfile(const ProfileDoc &profile) {
    const QString clean = profile.name.trimmed();
    if (!validProfileName(clean)) return;
    QSettings settings;
    for (const QString &tier : tierIds()) {
        QStringList items;
        for (const TierEntry &entry : profile.lists.value(tier))
            items << entry.key + QLatin1Char('|') + entry.effort;
        settings.setValue(profileTierKey(clean, tier), items);
    }
    for (const QString &klass : boxClasses()) {
        const BoxSetting setting = profile.box.value(klass);
        settings.setValue(profileBoxCutoffKey(clean, klass), qMax(1, setting.cutoff));
        if (setting.shown) settings.remove(profileBoxOffKey(clean, klass));
        else settings.setValue(profileBoxOffKey(clean, klass), true);
    }
    QStringList names = profiles();
    if (!names.contains(clean)) { names << clean; store(kProfileOrder, names); }
    // Importing does not change what this machine runs on - except when it lands on the profile the
    // live lists belong to, where leaving them behind would break the one-thing invariant.
    if (settings.value(kProfile).toString() == clean) applyProfile(clean);
}

int fallbackThreshold() { return qMax(1, QSettings().value(kThreshold, kDefaultThreshold).toInt()); }
void setFallbackThreshold(int count) {
    if (count == kDefaultThreshold) QSettings().remove(kThreshold);
    else QSettings().setValue(kThreshold, qMax(1, count));
}

Sort sort() { return sortFromId(QSettings().value(kSort).toString()); }
void setSort(Sort sort) {
    if (sort == Sort::Priority) QSettings().remove(kSort);
    else QSettings().setValue(kSort, sortId(sort));
}

}  // namespace curation

// ----- lists -----------------------------------------------------------------------------------

// The one rule behind `shown()`, stated once (card #MDL1 t:a10, design 5.5). There is no
// "models in the picker" checklist any more and no `models/shown`: a model a provider serves is
// a model you can pick. The single exception is an open-ended provider's long tail — OpenRouter's
// four hundred live rows — which would bury every other provider in every list it appears in. A
// tail row joins the lists the moment something says it is wanted: it is a provider's tier
// default, it is an id you typed (`models/custom`), or a tier list names it. Typing in the
// Ctrl+Alt+M dialog reaches the rest (`allUsable`), which is where the old id box went.
static bool inPickerList(const Entry &entry) {
    if (!entry.usable) return false;
    if (!entry.openEnded || !entry.tier.isEmpty() || entry.custom) return true;
    return curation::inAnyList(entry.key);
}

QList<Entry> shown(const Catalog &catalog) {
    QList<Entry> out;
    for (const QString &key : curation::ranked(catalog)) {
        const Entry *entry = catalog.find(key);
        if (entry && inPickerList(*entry)) out << *entry;
    }
    return out;
}

QList<Entry> allUsable(const Catalog &catalog) {
    QList<Entry> out;
    for (const QString &key : curation::ranked(catalog)) {
        const Entry *entry = catalog.find(key);
        if (entry && entry->usable) out << *entry;
    }
    return out;
}

double percentLeft(const Catalog &catalog, const QString &preset) {
    double best = -1;
    for (const LimitWindow &window : catalog.limits.value(preset)) {
        if (window.usedPercent < 0) continue;
        const double left = qBound(0.0, 100.0 - window.usedPercent, 100.0);
        // The window nearest to running out is the one that stops the next turn.
        best = best < 0 ? left : qMin(best, left);
    }
    return best;
}

QList<Entry> ordered(QList<Entry> entries, Sort sort, const Catalog &catalog) {
    switch (sort) {
    case Sort::Priority:
        break;   // `shown` already ranks
    case Sort::Alphabetical:
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            const int provider = a.provider.compare(b.provider, Qt::CaseInsensitive);
            return provider != 0 ? provider < 0 : a.label.compare(b.label, Qt::CaseInsensitive) < 0;
        });
        break;
    case Sort::Intelligence:
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.intelligence > b.intelligence; });
        break;
    case Sort::Speed:
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return curation::speed(a.key) > curation::speed(b.key);
        });
        break;
    case Sort::Usage:
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return curation::uses(a.key) > curation::uses(b.key);
        });
        break;
    case Sort::Remaining:
        std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b) {
            return percentLeft(catalog, a.preset) > percentLeft(catalog, b.preset);
        });
        break;
    }
    return entries;
}

qint64 exhaustedUntil(const Catalog &catalog, const QString &preset, qint64 now) {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    const QList<LimitWindow> windows = catalog.limits.value(preset);
    const bool rejected = catalog.status.value(preset) == QStringLiteral("rejected");
    // A spent window holds the preset until its reset; with several, until the last one. A reset
    // time the provider did not give (0) means "until it says otherwise".
    qint64 until = -1;
    bool unknown = false;
    for (const LimitWindow &window : windows) {
        if (window.usedPercent < 100) continue;
        if (window.resetsAt > 0 && window.resetsAt <= now) continue;   // reset already
        if (window.resetsAt <= 0) unknown = true;
        else until = qMax(until, window.resetsAt);
    }
    if (until < 0 && !unknown && rejected) {
        // The provider refuses without any window at 100 (codex's rateLimitReachedType): it lifts
        // at the latest reset it named, or when it next reports, if it named none.
        qint64 latest = 0;
        bool anyAhead = false, anyKnown = false;
        for (const LimitWindow &window : windows) {
            if (window.resetsAt <= 0) continue;
            anyKnown = true;
            if (window.resetsAt > now) { anyAhead = true; latest = qMax(latest, window.resetsAt); }
        }
        if (anyAhead) until = latest;
        else if (!anyKnown) unknown = true;
    }
    if (until < 0 && unknown) return 0;
    return until;
}

bool exhausted(const Catalog &catalog, const QString &preset, qint64 now) {
    return exhaustedUntil(catalog, preset, now) >= 0;
}

QList<Entry> live(const Catalog &catalog, qint64 now) {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    QList<Entry> out;
    for (const Entry &entry : shown(catalog))
        if (!exhausted(catalog, entry.preset, now)) out << entry;
    return out;
}

QList<Entry> liveTier(const Catalog &catalog, const QString &tier, qint64 now) {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    QList<Entry> out;
    for (const curation::TierEntry &item : curation::tierList(tier)) {
        const Entry *entry = catalog.find(item.key);
        if (entry && entry->usable && !exhausted(catalog, entry->preset, now)) out << *entry;
    }
    return out;
}

// Once the tier lists exist the main list is the order: rank 1 is Main, the rest its fallbacks.
// Before that (an install that has not seen the worker's defaults yet) the old ranked list and its
// threshold answer, so nothing is ever without a default.
Entry mainDefault(const Catalog &catalog, qint64 now) {
    const QList<Entry> list = curation::tierListsSet() ? liveTier(catalog, QStringLiteral("main"), now) : live(catalog, now);
    return list.isEmpty() ? Entry() : list.first();
}

Entry fallback(const Catalog &catalog, qint64 now) {
    const QList<Entry> list = curation::tierListsSet() ? liveTier(catalog, QStringLiteral("main"), now) : live(catalog, now);
    return list.size() < 2 ? Entry() : list.at(1);
}

QList<Entry> fallbacks(const Catalog &catalog, qint64 now) {
    if (curation::tierListsSet()) return liveTier(catalog, QStringLiteral("main"), now).mid(1);
    const QList<Entry> list = live(catalog, now);
    return list.mid(1, qMax(0, curation::fallbackThreshold() - 1));
}

// ----- one default, and /swap as a toggle (card #MDL1, rule 3) ----------------------------------

namespace {

// The main list's own level for one entry. Only the main list: the level a pane starts at is the
// one written beside rank 1 there, and a copy of the model sitting in the flash list at `low` is
// not an answer about the main agent. `listEffortFor` (every list, first hit) stays what a *pick*
// uses, where the question is "the level this model runs at when chosen".
QString mainListEffort(const QString &key) {
    for (const curation::TierEntry &item : curation::tierList(QStringLiteral("main")))
        if (item.key == key) return item.effort;
    return QString();
}

// Why there is no rank 1 to go to. Told apart because the old `/swap` said "every ranked
// subscription is exhausted" in all four cases, including the common one — the worker has not
// sent a catalog yet — where nothing is exhausted at all (design section 1.4.5).
QString noMainReason(const Catalog &catalog, qint64 now) {
    if (catalog.entries.isEmpty())
        return QStringLiteral("The model list is not ready yet: this pane's agent has not reported its providers.");
    if (shown(catalog).isEmpty())
        return QStringLiteral("No model is usable yet. Open Options › Models (/models) to add a provider key.");
    if (live(catalog, now).isEmpty())
        return QStringLiteral("No model with anything left to swap to: every ranked subscription is exhausted.");
    return QStringLiteral("No models in the main list yet: rank one in Options › Models (/models).");
}

}  // namespace

StartChoice startEntry(const Catalog &catalog, const QString &restoredPreset, const QString &restoredModel, qint64 now) {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    StartChoice choice;
    // A restored pane comes back on its own model, not on rank 1: the pick it holds was made in
    // that pane. Through resolveKey, so a guest that saved the model its CLI reported
    // ("claude-opus-5") comes back as the entry the lists name (`guest:claude|opus`).
    if (!restoredPreset.isEmpty()) {
        QString key = catalog.resolveKey(restoredPreset, restoredModel);
        // A layout saved before the model was written down, or by an older Relay: the preset's own
        // main model, else its first row — the same ladder `Pane::currentEntryKey` walks, so a
        // pane that comes back without a model lands where it would have said it was.
        if (key.isEmpty() && restoredModel.isEmpty()) {
            if (const Entry *main = catalog.tierEntry(restoredPreset, QStringLiteral("main"))) key = main->key;
            else if (const QList<Entry> rows = catalog.ofPreset(restoredPreset); !rows.isEmpty()) key = rows.first().key;
        }
        if (const Entry *entry = key.isEmpty() ? nullptr : catalog.find(key);
            entry && entry->usable && !exhausted(catalog, entry->preset, now)) {
            choice.entry = *entry;
            choice.effort = mainListEffort(entry->key);
            choice.restored = true;
            return choice;
        }
    }
    // Rank 1 of the main list, guests included (owner, 2026-09-21). An exhausted rank 1 is stepped
    // over by mainDefault, and nothing is written down, so the pane goes back to it by itself when
    // the subscription resets (design edge case 15).
    const Entry main = mainDefault(catalog, now);
    if (main.key.isEmpty()) return choice;   // empty: the caller's own ladder answers
    choice.entry = main;
    choice.effort = mainListEffort(main.key);
    return choice;
}

SwapStep swapTarget(const Catalog &catalog, const QString &currentKey, const QString &rememberedKey, qint64 now) {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    SwapStep step;
    const Entry main = mainDefault(catalog, now);
    if (main.key.isEmpty()) { step.message = noMainReason(catalog, now); return step; }

    QString currentPreset;
    Catalog::splitKey(currentKey, &currentPreset, nullptr);
    const bool spent = !currentPreset.isEmpty() && exhausted(catalog, currentPreset, now);

    // Off rank 1 — the normal case, and the one the old /swap could never come back from: remember
    // where this pane is and go to rank 1. A spent model is left behind the same way; it is worth
    // remembering, because the next /swap from rank 1 will skip it while it is still spent and
    // offer it again once it resets.
    if (spent || currentKey != main.key) {
        step.target = main;
        step.remember = currentKey;
        step.kind = SwapKind::Main;
        const Entry *from = currentKey.isEmpty() ? nullptr : catalog.find(currentKey);
        step.message = spent
            ? QStringLiteral("Swapped to %1 — rank 1 of the main list; %2 has nothing left.")
                  .arg(main.displayName(), from ? from->name : currentPreset)
            : from ? QStringLiteral("Swapped to %1 — rank 1 of the main list. /swap goes back to %2.")
                         .arg(main.displayName(), from->name)
                   : QStringLiteral("Swapped to %1 — rank 1 of the main list.").arg(main.displayName());
        return step;
    }

    // On rank 1: back to the model this pane came from. It has to still be there, still usable and
    // not spent — and not rank 1 itself, which would be a /swap that did nothing.
    if (!rememberedKey.isEmpty() && rememberedKey != main.key) {
        if (const Entry *back = catalog.find(rememberedKey);
            back && back->usable && !exhausted(catalog, back->preset, now)) {
            step.target = *back;
            step.kind = SwapKind::Back;
            step.message = QStringLiteral("Back on %1 — where this pane was. /swap returns to %2.")
                               .arg(back->displayName(), main.name);
            return step;
        }
    }
    // Nothing to come back to: rank 2, which is what /swap has always meant on rank 1.
    const Entry second = fallback(catalog, now);
    if (second.key.isEmpty()) {
        step.message = QStringLiteral("No second model in the main list: rank one in Options › Models (/models).");
        return step;
    }
    step.target = second;
    step.kind = SwapKind::Fallback;
    step.message = QStringLiteral("Swapped to %1 — rank 2 of the main list. /swap goes back to %2.")
                       .arg(second.displayName(), main.name);
    return step;
}

QString resetText(qint64 resetsAt, qint64 now) {
    if (resetsAt <= 0) return QString();
    const QDateTime resets = QDateTime::fromSecsSinceEpoch(resetsAt);
    const QDateTime at = QDateTime::fromSecsSinceEpoch(now);
    return resets.date() == at.date()
        ? resets.toString(QStringLiteral("HH:mm"))
        : resets.secsTo(at) > -7 * 86400 ? QLocale::c().dayName(resets.date().dayOfWeek(), QLocale::ShortFormat).toLower()
                                          : resets.toString(QStringLiteral("d MMM")).toLower();
}

QString limitsText(const QList<LimitWindow> &windows, qint64 now) {
    QStringList parts;
    for (const LimitWindow &window : windows) {
        if (window.usedPercent < 0) continue;
        QString part = QStringLiteral("%1 %2% left").arg(window.kind).arg(qRound(qBound(0.0, 100.0 - window.usedPercent, 100.0)));
        if (window.resetsAt > 0) part += QStringLiteral(", resets %1").arg(resetText(window.resetsAt, now));
        parts << part;
    }
    return parts.join(QStringLiteral(" · "));
}

bool matches(const Entry &entry, const QString &query) {
    // The model's name, the id the API takes, the provider and its plan. The id is in there as
    // well as the name because they differ where it matters most — "k3" finds kimi-k3, and
    // "anthropic/claude-haiku-4.5" finds the row a hand-typed OpenRouter slug made.
    const QString haystack = (entry.name + QLatin1Char(' ') + entry.model + QLatin1Char(' ') + entry.provider
                              + QLatin1Char(' ') + entry.plan).toLower();
    for (const QString &word : query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (!haystack.contains(word)) return false;
    return true;
}

// ----- groups ------------------------------------------------------------------------------------

namespace {

// Whether the preset behind this entry is a subscription rather than metered credit.
//
// The plans that exist today (presets.py, customproviders.py, localmodels.py) are "coding plan"
// and "token plan" — what you have already paid for — against "pay-as-you-go" and "standard api",
// which spend credit per call, "included" (Relay Free, which is `hosted` and ranked last on its
// own account), "custom endpoint" and a local server's name. So the test is: the preset says it
// has a plan, and the words of that plan are not the metered ones. Reading the words rather than
// listing the preset ids is what survives a provider being added: a new "<something> plan" sorts
// with the subscriptions without anyone remembering to add it here.
bool subscription(const Entry &entry) {
    if (entry.hosted || entry.local || entry.guest || entry.plan.isEmpty()) return false;
    static const QStringList metered{QStringLiteral("pay-as-you-go"), QStringLiteral("pay as you go"),
                                     QStringLiteral("payg"), QStringLiteral("api"), QStringLiteral("credit")};
    for (const QString &word : metered)
        if (entry.plan.contains(word)) return false;
    return true;
}

// Rule 2.2: a plan first (it is already paid for), then a guest harness, then the first-party
// pay-as-you-go API, then OpenRouter, then Relay Free — so credit is spent last and the included
// allowance last of all.
int accessRank(const Entry &entry) {
    if (entry.hosted) return 4;
    if (entry.guest) return 1;
    if (entry.openEnded || entry.preset == QStringLiteral("openrouter")) return 3;
    if (subscription(entry)) return 0;
    return 2;
}

// Where the user has already ranked this entry: its position in the tier lists, main first. An
// entry in no list sorts after every entry in one.
int listRank(const QString &key) {
    const QStringList tiers = curation::tierIds();      // main, high, flash, lite, local
    for (int tier = 0; tier < tiers.size(); ++tier) {
        const QList<curation::TierEntry> list = curation::tierList(tiers.at(tier));
        for (int at = 0; at < list.size(); ++at)
            if (list.at(at).key == key) return tier * 10000 + at;
    }
    return std::numeric_limits<int>::max();
}

// A local entry never groups with a cloud one even when the names match (design 3.2): "local" is a
// promise about where the text goes, not a provider, so it gets its own bucket.
QString bucketOf(const Entry &entry) {
    return entry.local ? QStringLiteral("local\x1f") + entry.name : entry.name;
}

bool liveEntry(const Catalog &catalog, const Entry &entry, qint64 now) {
    return entry.usable && !exhausted(catalog, entry.preset, now);
}

}  // namespace

Entry Group::preferred(const Catalog &catalog, qint64 now) const {
    if (entries.isEmpty()) return Entry();
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    for (const Entry &entry : entries)
        if (liveEntry(catalog, entry, now)) return entry;
    for (const Entry &entry : entries)
        if (entry.usable) return entry;
    return entries.first();
}

bool Group::spent(const Catalog &catalog, qint64 now) const {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    for (const Entry &entry : entries)
        if (liveEntry(catalog, entry, now)) return false;
    return true;
}

const Entry *Group::via(const QString &presetOrProviderText) const {
    const QString want = presetOrProviderText.trimmed().toLower();
    if (want.isEmpty()) return nullptr;
    for (const Entry &entry : entries) {
        const QString preset = entry.preset.toLower();
        if (preset == want || preset.section(QLatin1Char(':'), -1) == want) return &entry;
    }
    for (const Entry &entry : entries)
        if (entry.provider.toLower() == want) return &entry;
    for (const Entry &entry : entries)
        if (entry.provider.toLower().contains(want) || entry.preset.toLower().contains(want)) return &entry;
    return nullptr;
}

QList<Group> grouped(const Catalog &catalog, const QList<Entry> &rows, qint64 now) {
    Q_UNUSED(catalog);
    Q_UNUSED(now);
    QList<Group> out;
    QHash<QString, int> at;                 // bucket -> its index in `out`
    // The tier lists are read once per group, not once per comparison: `tierList` builds a
    // QSettings, and a sort calls its comparator O(n log n) times (the same trap as `shown`, #PPR4).
    QHash<QString, int> ranks;
    for (const Entry &entry : rows) {
        const QString bucket = bucketOf(entry);
        if (!ranks.contains(entry.key)) ranks.insert(entry.key, listRank(entry.key));
        const auto found = at.constFind(bucket);
        if (found == at.constEnd()) {
            at.insert(bucket, out.size());
            out << Group{entry.name, {entry}};
        } else {
            out[found.value()].entries << entry;
        }
    }
    for (Group &group : out) {
        if (group.entries.size() < 2) continue;
        std::stable_sort(group.entries.begin(), group.entries.end(), [&ranks](const Entry &a, const Entry &b) {
            const int rankA = ranks.value(a.key, std::numeric_limits<int>::max());
            const int rankB = ranks.value(b.key, std::numeric_limits<int>::max());
            if (rankA != rankB) return rankA < rankB;
            return accessRank(a) < accessRank(b);
        });
    }
    return out;
}

const Entry *findByName(const Catalog &catalog, const QList<Entry> &rows, const QString &text) {
    const QString query = text.trimmed();
    if (query.isEmpty()) return nullptr;
    const int at = query.lastIndexOf(QLatin1Char('@'));
    const QString wanted = (at > 0 ? query.left(at) : query).trimmed();
    const QString providerText = at > 0 ? query.mid(at + 1).trimmed() : QString();
    const QList<Group> groups = grouped(catalog, rows);
    const Group *group = nullptr;
    for (const Group &candidate : groups)
        if (candidate.name.compare(wanted, Qt::CaseInsensitive) == 0) { group = &candidate; break; }
    // A bare model id ("k3", "anthropic/claude-haiku-4.5"), or a spelling whose derived name is the
    // one a row carries.
    for (int i = 0; group == nullptr && i < groups.size(); ++i)
        for (const Entry &entry : groups.at(i).entries)
            if (entry.model.compare(wanted, Qt::CaseInsensitive) == 0
                || entry.name.compare(nameOf(wanted), Qt::CaseInsensitive) == 0) { group = &groups.at(i); break; }
    if (group == nullptr) return nullptr;
    const Entry *chosen = providerText.isEmpty() ? nullptr : group->via(providerText);
    if (chosen == nullptr && !providerText.isEmpty()) return nullptr;
    const QString key = chosen != nullptr ? chosen->key : group->preferred(catalog).key;
    if (key.isEmpty()) return nullptr;
    // `grouped` copied the entries; the answer has to point into the caller's list, not into a
    // temporary that goes out of scope with this call.
    for (const Entry &entry : rows)
        if (entry.key == key) return &entry;
    return nullptr;
}

}  // namespace relay::models
