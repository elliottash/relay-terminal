// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelCatalog.h"

#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QHash>
#include <QRandomGenerator>
#include <QSettings>
#include <QVariant>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace relay::models {

namespace {

const QString kPriority = QStringLiteral("models/priority");
const QString kAvailable = QStringLiteral("models/available");
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

// The reads a `curation::ReadScope` memoises (see ModelCatalog.h). Outside one, every read is a
// fresh QSettings lookup, as before; inside one, each key is read once. Writes drop the memo.
int g_readDepth = 0;
QHash<QString, QVariant> g_reads;

QVariant readSetting(const QString &key, const QVariant &fallback = QVariant()) {
    if (g_readDepth == 0) return QSettings().value(key, fallback);
    auto at = g_reads.constFind(key);
    if (at == g_reads.constEnd()) at = g_reads.insert(key, QSettings().value(key));
    return at->isValid() ? *at : fallback;
}

QStringList list(const QString &key) { return readSetting(key).toStringList(); }
void store(const QString &key, const QStringList &value) {
    g_reads.clear();
    if (value.isEmpty()) QSettings().remove(key);
    else QSettings().setValue(key, value);
}
QString perKey(const QString &group, const QString &key) { return QStringLiteral("models/") + group + QLatin1Char('/') + key; }

// A preset row is usable the way Pane::m_stored decides it: a stored key, a local server, a
// runnable guest harness, or Relay Free while the worker reports it available.
bool usableRow(const QJsonObject &preset) {
    const bool hosted = preset.value(QStringLiteral("hosted")).toBool();
    if (hosted) return preset.value(QStringLiteral("available")).toBool();
    return preset.value(QStringLiteral("has_stored_key")).toBool()
        || preset.value(QStringLiteral("local")).toBool()
        || preset.value(QStringLiteral("harness")).toBool()
        || (hosted && preset.value(QStringLiteral("available")).toBool());
}

// Whether the level box is greyed for this row (owner, 2026-09-21: "for no knob models, the
// effort box should be grayed out. same for relay free."). The worker states it per model row as
// `effort_fixed`, with the preset row as its fallback for a provider whose whole endpoint has no
// knob; where neither says, the same two cases are derived here, which is what an older worker
// gets. `model` may be an empty object (a preset with no catalog of its own).
bool effortFixedOf(const QJsonObject &model, const QJsonObject &preset, const QStringList &efforts, bool hosted) {
    const QString field = QStringLiteral("effort_fixed");
    if (model.contains(field)) return model.value(field).toBool();
    if (preset.contains(field)) return preset.value(field).toBool();
    return efforts.isEmpty() || hosted;
}

QList<LimitWindow> windowsOf(const QJsonObject &preset, int *resetsAvailable);
QList<LimitWindow> windowsOf(const QJsonObject &preset) {
    return windowsOf(preset, nullptr);
}

QList<LimitWindow> windowsOf(const QJsonObject &preset, int *resetsAvailable) {
    QList<LimitWindow> windows;
    // The worker's guest row carries `limits: {windows, resets_available?, status?, updated_at}`
    // (protocol 29.3): the last usage_limits event as it held it. A bare array of windows is read
    // too, with no place for the resets count.
    const QJsonValue held = preset.value(QStringLiteral("limits"));
    if (resetsAvailable) *resetsAvailable = -1;
    if (held.isObject()) {
        const QJsonValue resets = held.toObject().value(QStringLiteral("resets_available"));
        if (resets.isDouble()) *resetsAvailable = qMax(0, resets.toInt());
    }
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
    if (hosted) return name; // the service role already says Relay Free or Relay Pro
    if (provider.isEmpty()) return name;
    return name + QStringLiteral(" · ") + provider;
}

QStringList effortLadder() {
    // Codex's own list, which every other provider's is a subset of in the same order. It is an
    // order and nothing else: no picker offers a word off a model's own `efforts`, and the wire
    // carries the provider's word untouched.
    static const QStringList ladder{QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high"),
                                    QStringLiteral("xhigh"), QStringLiteral("max"), QStringLiteral("ultra")};
    return ladder;
}

int effortRank(const QString &level) { return effortLadder().indexOf(level); }

QString nearestEffort(const QStringList &levels, const QString &level) {
    if (levels.isEmpty()) return QString();
    if (levels.contains(level)) return level;
    int want = effortRank(level);
    if (want < 0) want = effortRank(QStringLiteral("high"));
    QString best;
    int bestDistance = -1;
    for (const QString &candidate : levels) {
        // A word this ladder has never heard of sits at the provider's own position in its list,
        // which keeps a provider-specific level orderable against the ones that are known.
        int rank = effortRank(candidate);
        if (rank < 0) rank = std::clamp(int(levels.indexOf(candidate)), 0, int(effortLadder().size()) - 1);
        const int distance = qAbs(rank - want);
        if (bestDistance < 0 || distance < bestDistance
            || (distance == bestDistance && rank > effortRank(best))) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

QString Entry::effortFixedReason() const {
    if (!effortFixed) return QString();
    // Relay Free first: it does have levels, and saying "no reasoning level" about a model that
    // plainly has two would read as a bug rather than as the gateway's rule.
    if (hosted) return QStringLiteral("Relay Free sets the level for you");
    return QStringLiteral("%1 has no reasoning level").arg(name.isEmpty() ? model : name);
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
    // "claude-opus-5.5", and the CLI reports the API id "claude-opus-5-5" once it is running.
    // Anthropic spells a version's dot as a dash, so a short number after a number is read as a
    // minor version on both sides (a date suffix is not), and comparing names is comparing what
    // the two spellings mean. `reportedModel` is compared as it stands too, for a row whose name
    // is itself the alias (a guest whose family nothing maps).
    static const QRegularExpression minorDash(QStringLiteral("(?<=\\d)-(\\d{1,2})(?=-|$)"));
    const auto versioned = [](QString name) { return name.toLower().replace(minorDash, QStringLiteral(".\\1")); };
    const QString wanted = versioned(nameOf(reportedModel));
    for (const Entry &entry : entries) {
        if (entry.preset != preset) continue;
        if (versioned(entry.name) == wanted
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
        // The ranking file's ruling for this provider (13.2), read once for every row it makes.
        // A worker that sends neither leaves `order` at -1, and `grouped()` falls back.
        const QString kind = str(preset, "kind");
        const int order = preset.value(QStringLiteral("order")).isDouble()
                              ? preset.value(QStringLiteral("order")).toInt() : -1;
        catalog.presetLabels.insert(id, str(preset, "label").toLower());
        int resetsAvailable = -1;
        const QList<LimitWindow> windows = windowsOf(preset, &resetsAvailable);
        if (!windows.isEmpty()) catalog.limits.insert(id, windows);
        if (resetsAvailable >= 0) catalog.resetsAvailable.insert(id, resetsAvailable);
        const QJsonObject limits = preset.value(QStringLiteral("limits")).toObject();
        const qint64 updated = limits.value(QStringLiteral("updated_at")).toVariant().toLongLong();
        if (updated > 0) catalog.limitUpdatedAt.insert(id, updated);
        else if (!preset.value(QStringLiteral("quota")).toObject().isEmpty())
            catalog.limitUpdatedAt.insert(id, QDateTime::currentSecsSinceEpoch());
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
            // A stale worker may still report its hosted provider's backing model as the name.
            // The model id is a gateway role; only the service and role belong in app surfaces.
            if (id == QStringLiteral("relay-free") || id == QStringLiteral("relay-pro")) {
                const QString role = str(row, "tier");
                const QString service = id == QStringLiteral("relay-free")
                    ? QStringLiteral("relay free") : QStringLiteral("relay pro");
                entry.name = role.isEmpty() ? service : service + QStringLiteral(" · ") + role;
            }
            entry.label = entry.name;
            entry.provider = provider;
            entry.plan = str(preset, "plan").toLower();
            entry.kind = kind;
            entry.order = order;
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
            // `effort_labels` is retired with the same card: the levels above are already the
            // provider's own words, so there is nothing left to translate and an old worker's map
            // would translate them twice. It is read by nobody and ignored here.
            entry.effortFixed = effortFixedOf(row, preset, entry.efforts, hosted);
            entry.usable = usable;
            // Guest harnesses enumerate a finite selectable catalog. Codex can ship seven or
            // more models without tier markers; treating that as an aggregator hides them all.
            entry.openEnded = !guest && models.size() > 6;
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
            entry.kind = kind; entry.order = order;
            for (const auto &level : preset.value(QStringLiteral("efforts")).toArray()) entry.efforts << level.toString();
            entry.effortFixed = effortFixedOf(QJsonObject(), preset, entry.efforts, hosted);
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
// is `low` for gpt-6-sol — and a codex model's High is `xhigh`, not its top level, which is
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

void resetPriority() { g_reads.clear(); QSettings().remove(kPriority); }

ReadScope::ReadScope() { ++g_readDepth; }
ReadScope::~ReadScope() { if (--g_readDepth == 0) g_reads.clear(); }

// ----- step 2: available (owner, 2026-09-21) ---------------------------------------------------
// The default, with nothing stored: every model of a branded provider, and of an open-ended one
// (OpenRouter's live listing) only the recommended rows — the ones the worker's own catalog names,
// which are exactly the rows that carry a `tier` (`presets.MODEL_CATALOG["openrouter"]`;
// openrouter_catalog.py sends `tier: null` on every live row) — plus an id you typed and anything
// a **terminal** list names. This was `inPickerList` until today, written into `shown()` as the
// only rule.
static bool availableByDefault(const Entry &entry) {
    if (!entry.openEnded || !entry.tier.isEmpty() || entry.custom) return true;
    return inTerminalList(entry.key);
}

// Whether the stored list says anything at all about this preset. A provider added *after* the
// list was written (step 1 happens again every time a key is added) has said nothing about its
// models, so the default applies to it rather than "nothing of it is available" — otherwise
// un-checking one model today would silently hide every model of tomorrow's provider.
static bool presetNamedIn(const QStringList &available, const QString &preset) {
    const QString prefix = preset + QLatin1Char('|');
    for (const QString &key : available)
        if (key.startsWith(prefix)) return true;
    return false;
}

QStringList availableKeys() { return list(kAvailable); }

bool isAvailable(const Entry &entry, const QStringList &available) {
    if (available.isEmpty()) return availableByDefault(entry);
    if (available.contains(entry.key)) return true;
    // The tick is the user's, ranked or not (#AVR8, owner 2026-09-22: "the model priorities page
    // needs to refresh when changing available"). A terminal list used to pin its models on, so an
    // un-tick sprang straight back and Priorities never changed. Now an un-ticked ranked model
    // keeps its place in the list — greyed there, `activeTierList` — and nothing runs it until it
    // is ticked again.
    if (!presetNamedIn(available, entry.preset)) return availableByDefault(entry);
    return false;
}

bool isAvailable(const Entry &entry) { return isAvailable(entry, availableKeys()); }

// The same answer from the key alone, for a key a list names. That is exact without a catalog: a
// listed key is available by default whatever its provider (`availableByDefault` answers true for
// anything `inTerminalList` names), so only the stored ticks can say no.
bool isAvailableKey(const QString &key, const QStringList &available) {
    if (available.isEmpty() || available.contains(key)) return true;
    QString preset, model;
    if (!Catalog::splitKey(key, &preset, &model)) return true;
    return !presetNamedIn(available, preset);
}

QList<TierEntry> activeTierList(const QString &tier) {
    QList<TierEntry> list = tierList(tier);
    // Lite is the chores' list, which the tick never governs (`inTerminalList`).
    if (!boxClasses().contains(tier)) return list;
    const QStringList available = availableKeys();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&available](const TierEntry &item) { return !isAvailableKey(item.key, available); }),
               list.end());
    return list;
}

void setAvailable(const QString &key, bool on, const Catalog &catalog) {
    QStringList keys = availableKeys();
    QString preset, model;
    const QString marker = Catalog::splitKey(key, &preset, &model) ? preset + QLatin1Char('|') : QString();
    if (keys.isEmpty()) {
        // The first change writes down today's default, so that one un-check un-checks one model
        // rather than every model the default was letting through (the same shape `setShown` had).
        for (const Entry &entry : catalog.entries)
            if (availableByDefault(entry)) keys << entry.key;
    }
    if (on) { keys.removeAll(marker); if (!keys.contains(key)) keys << key; }
    else keys.removeAll(key);
    // Un-checking the last one is a reset to the default, never an empty catalog. A marker (below)
    // is not a model, so a list of nothing but markers is that same reset.
    bool anyModel = false;
    for (const QString &each : std::as_const(keys)) anyModel = anyModel || !each.endsWith(QLatin1Char('|'));
    if (!anyModel) { g_reads.clear(); QSettings().remove(kAvailable); return; }
    // A provider whose every model has been un-ticked leaves `"<preset>|"` behind, which is not a
    // key and matches no entry — it is only there so `presetNamedIn` goes on finding the preset.
    // Without it the provider would read as one nobody has said anything about and the default —
    // every model available — would quietly come back, so a provider serving one model (a custom
    // endpoint, a local server) could never have that model un-ticked at all (card #MDL1).
    if (!on && !marker.isEmpty() && !presetNamedIn(keys, preset)) keys << marker;
    store(kAvailable, keys);
}

void resetAvailable() { g_reads.clear(); QSettings().remove(kAvailable); }

QStringList customKeys() { return list(kCustom); }

Entry addCustom(const QString &preset, const QString &model, const Catalog &catalog) {
    const QString key = Catalog::keyFor(preset, model.trimmed());
    QStringList keys = customKeys();
    if (!catalog.find(key) && !keys.contains(key)) { keys << key; store(kCustom, keys); }
    // Available at once: an id you typed is a model you asked for, and hunting for its checkbox
    // afterwards would be two steps for one. Only when a list is already stored — with nothing
    // stored the default already says so, and writing the snapshot here would turn "I added an
    // id" into "I curated every model on this machine".
    if (!availableKeys().isEmpty()) setAvailable(key, true, catalog);
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
    if (QStringList available = availableKeys(); available.removeAll(key) > 0) store(kAvailable, available);
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

int uses(const QString &key) { return readSetting(perKey(QStringLiteral("uses"), key), 0).toInt(); }

double speed(const QString &key) { return readSetting(perKey(QStringLiteral("speed"), key), 0.0).toDouble(); }

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
static TierEntry decodeTierItem(QString item, int position) {
    int rank = position;
    const int marker = item.lastIndexOf(QStringLiteral("|rank="));
    if (marker > 0) {
        bool ok = false;
        const int saved = item.mid(marker + 6).toInt(&ok);
        if (ok && saved > 0 && saved <= 1000) {
            rank = saved;
            item.truncate(marker);
        }
    }
    const int last = item.lastIndexOf(QLatin1Char('|'));
    if (last <= 0) return {};
    return {item.left(last), item.mid(last + 1), rank};
}
QList<TierEntry> tierList(const QString &tier) {
    QList<TierEntry> out;
    for (const QString &item : list(tierKey(tier))) {
        TierEntry entry = decodeTierItem(item, out.size() + 1);
        QString preset, model;
        if (Catalog::splitKey(entry.key, &preset, &model)) out << entry;
    }
    return out;
}
void setTierList(const QString &tier, const QList<TierEntry> &entries) {
    QStringList items;
    for (int i = 0; i < entries.size(); ++i) {
        const TierEntry &entry = entries.at(i);
        items << entry.key + QLatin1Char('|') + entry.effort
                     + QStringLiteral("|rank=%1").arg(entry.rank > 0 ? entry.rank : i + 1);
    }
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
void setTierRank(const QString &tier, const QString &key, int rank) {
    if (rank < 1 || rank > 1000) return;
    QList<TierEntry> entries = tierList(tier);
    for (TierEntry &entry : entries) if (entry.key == key) entry.rank = rank;
    std::stable_sort(entries.begin(), entries.end(), [](const TierEntry &a, const TierEntry &b) {
        return a.rank < b.rank;
    });
    setTierList(tier, entries);
}
bool inAnyList(const QString &key) {
    for (const QString &tier : tierIds())
        for (const TierEntry &entry : tierList(tier))
            if (entry.key == key) return true;
    return false;
}
// The same question asked of the four classes a *pane* can run in — `boxClasses()`: high, main,
// flash, local. It is `inAnyList` less `lite`, and the difference is the whole of the owner's
// 2026-09-21 report: "it seems like i cant disable gemini flash lite. just to say -- this tab is
// only for terminal agents, so gemini flash lite should be optional." Membership of the lite list
// used to pin a model available (`isAvailable`), so the one model the built-in lite list names
// could never be un-ticked. Lite is not a pane mode — the box has no lite row (design 5.3) and
// nothing a person types goes to it — so what the lite list holds is a statement about the chores,
// not about what this machine offers a terminal agent. The chores read `models/tier/lite` straight
// (`Pane::tiersObject`), never `shown()`, so un-ticking a lite model takes it out of the pane's
// filter and leaves the chores exactly where they were.
bool inTerminalList(const QString &key) {
    for (const QString &tier : boxClasses())
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
    return qMax(1, readSetting(boxCutoffKey(tier), kBoxCutoffDefault).toInt());
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
    return !readSetting(boxOffKey(tier), false).toBool();
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
            TierEntry entry = decodeTierItem(item, entries.size() + 1);
            if (!entry.key.isEmpty()) entries << entry;
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
        const TierEntry entry = decodeTierItem(item, rows.size() + 1);
        QString preset, model;
        if (!Catalog::splitKey(entry.key, &preset, &model)) continue;
        QJsonObject row{{QStringLiteral("preset"), preset}, {QStringLiteral("model"), model},
                        {QStringLiteral("effort"), entry.effort}};
        if (entry.rank != rows.size() + 1) row.insert(QStringLiteral("rank"), entry.rank);
        rows << row;
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
            const int saved = item.value(QStringLiteral("rank")).toInt();
            entries << TierEntry{Catalog::keyFor(preset, model), item.value(QStringLiteral("effort")).toString(),
                                 saved > 0 && saved <= 1000 ? saved : entries.size() + 1};
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
        const auto entries = profile.lists.value(tier);
        for (int i = 0; i < entries.size(); ++i) {
            const TierEntry &entry = entries.at(i);
            items << entry.key + QLatin1Char('|') + entry.effort
                         + QStringLiteral("|rank=%1").arg(entry.rank > 0 ? entry.rank : i + 1);
        }
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

Sort sort() { return sortFromId(readSetting(kSort).toString()); }
void setSort(Sort sort) {
    if (sort == Sort::Priority) QSettings().remove(kSort);
    else QSettings().setValue(kSort, sortId(sort));
}

}  // namespace curation

// ----- lists -----------------------------------------------------------------------------------

// Relay Free's own lite role, and anything shaped like it (card #MDL1, owner 2026-09-21: "and
// relay lite shouldnt show up"). The gateway exposes one pseudo-model per role — `relay-main`,
// `relay-flash`, `relay-lite` — and clamps each to that role's own ceiling, so `relay-lite` is not
// a model at all: it is the lite chore lane, named so the lite list has something to hold. Nothing
// a terminal agent does runs on it, so no surface a terminal agent picks from draws it.
//
// The rule is read off the row rather than off the name, so a second hosted lane needs no edit
// here: a **hosted** entry whose ranking class is `lite` and nothing else (`Entry::tier`, the one
// class the worker's catalog names it a default for). Everybody else's lite-classed models —
// gemini-3.5-flash-lite, gpt-6-luna, claude-haiku-4.5 — are real models a pane can be put on,
// so they stay, available by default and the user's to un-tick (`inTerminalList`).
bool liteOnlyRole(const Entry &entry) {
    return entry.hosted && entry.tier == QStringLiteral("lite");
}

// The one rule behind `shown()`, stated once (card #MDL1, design 5.5 and 5.7): a usable entry
// that is **available** — step 2 of the owner's four. With nothing un-checked that is every model
// of a branded provider plus an open-ended one's recommended rows, which is exactly the rule t:a10
// wrote here by hand; `curation::isAvailable` is now that rule as the *default* of a setting, so a
// model can be taken out ("i probably want to uncheck sonnet and haiku and gpt 5.5") and an
// OpenRouter row can be put in, which is what "for openrouter, you have to select specific models"
// needs. The long tail is still behind typing, which is where `allUsable` is read.
static bool inPickerList(const Entry &entry, const QStringList &available) {
    return entry.usable && !liteOnlyRole(entry) && curation::isAvailable(entry, available);
}

QList<Entry> shown(const Catalog &catalog) {
    QList<Entry> out;
    // The list is read once, not once per entry: a sort calls its comparator O(n log n) times and
    // every read would be a QSettings lookup (the same trap as #PPR4).
    const QStringList available = curation::availableKeys();
    for (const QString &key : curation::ranked(catalog)) {
        const Entry *entry = catalog.find(key);
        if (entry && inPickerList(*entry, available)) out << *entry;
    }
    return out;
}

QList<Entry> allUsable(const Catalog &catalog) {
    QList<Entry> out;
    for (const QString &key : curation::ranked(catalog)) {
        const Entry *entry = catalog.find(key);
        if (entry && entry->usable && !liteOnlyRole(*entry)) out << *entry;
    }
    return out;
}

QList<Entry> curatable(const Catalog &catalog) {
    QList<Entry> out;
    const QStringList available = curation::availableKeys();
    for (const QString &key : curation::ranked(catalog)) {
        const Entry *entry = catalog.find(key);
        if (!entry || !entry->usable || liteOnlyRole(*entry)) continue;
        // Available, or available by default and un-checked: both are rows the `all` tab draws —
        // the second greyed, with its box empty, so it can be ticked again. An open-ended
        // provider's long tail is neither, and stays behind typing.
        if (curation::isAvailable(*entry, available) || curation::isAvailable(*entry, QStringList()))
            out << *entry;
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
    for (const curation::TierEntry &item : curation::activeTierList(tier)) {
        const Entry *entry = catalog.find(item.key);
        if (entry && entry->usable && !exhausted(catalog, entry->preset, now)) out << *entry;
    }
    return out;
}

Entry drawTier(const Catalog &catalog, const QString &tier, qint64 now, double unitDraw) {
    if (now <= 0) now = QDateTime::currentSecsSinceEpoch();
    struct Candidate { Entry entry; double weight; };
    QList<Candidate> peers;
    int bestRank = std::numeric_limits<int>::max();
    const QList<curation::TierEntry> list = curation::activeTierList(tier);
    for (int i = 0; i < list.size(); ++i) {
        const auto &item = list.at(i);
        const int rank = item.rank > 0 ? item.rank : i + 1;
        if (rank > bestRank) continue;
        const Entry *entry = catalog.find(item.key);
        if (!entry || !entry->usable || exhausted(catalog, entry->preset, now)) continue;
        if (rank < bestRank) { peers.clear(); bestRank = rank; }
        double weight = 1.0;
        const qint64 updated = catalog.limitUpdatedAt.value(entry->preset);
        // A quota report older than half an hour is no evidence of what is left now.
        if (updated > 0 && updated <= now && now - updated <= 1800) {
            double remaining = 1.0;
            double urgency = 0.0;
            bool known = false;
            for (const LimitWindow &window : catalog.limits.value(entry->preset)) {
                if (window.usedPercent < 0 || window.resetsAt <= now) continue;
                known = true;
                remaining = qMin(remaining, qBound(0.0, (100.0 - window.usedPercent) / 100.0, 1.0));
                const double hours = double(window.resetsAt - now) / 3600.0;
                urgency = qMax(urgency, std::exp(-hours / 24.0));
            }
            if (known) weight += 4.0 * remaining * urgency;
        }
        peers << Candidate{*entry, weight};
    }
    if (peers.isEmpty()) return {};
    if (peers.size() == 1) return peers.first().entry;
    if (unitDraw < 0 || unitDraw >= 1) unitDraw = QRandomGenerator::global()->generateDouble();
    double total = 0;
    for (const Candidate &peer : peers) total += peer.weight;
    double position = unitDraw * total;
    for (const Candidate &peer : peers) {
        if (position < peer.weight) return peer.entry;
        position -= peer.weight;
    }
    return peers.last().entry;
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
    // ("claude-opus-5-5") comes back as the entry the lists name (`guest:claude|opus`).
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
    const Entry main = curation::tierListsSet() ? drawTier(catalog, QStringLiteral("main"), now)
                                               : mainDefault(catalog, now);
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

QString limitsText(const QList<LimitWindow> &windows, qint64 now, int resetsAvailable) {
    QStringList parts;
    for (const LimitWindow &window : windows) {
        if (window.usedPercent < 0) continue;
        QString part = QStringLiteral("%1 %2% left").arg(window.kind).arg(qRound(qBound(0.0, 100.0 - window.usedPercent, 100.0)));
        if (window.resetsAt > 0) part += QStringLiteral(", resets %1").arg(resetText(window.resetsAt, now));
        parts << part;
    }
    // The banked usage resets, the figure the CLI's /usage panel counts down (protocol 29.3):
    // shown only when there is at least one, so a provider that never reports the figure and one
    // that reports none left read the same.
    if (resetsAvailable > 0)
        parts << QStringLiteral("%1 usage reset%2").arg(resetsAvailable).arg(resetsAvailable == 1 ? QString() : QStringLiteral("s"));
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

// Rule 2.2 as this file used to hold it: a plan first (it is already paid for), then a guest
// harness, then the first-party pay-as-you-go API, then OpenRouter, then Relay Free — so credit is
// spent last and the included allowance last of all.
//
// Since card #MDL1 that ruling lives in backend/relay_core/model-ranking.md, which the owner edits,
// and reaches every row as `kind` and `order` (13.2). This stays as the fallback for a row that
// carried neither — an older worker — so the picker never loses its order, and it is deliberately
// not kept in step with the file by hand: `providerBefore` below prefers the file wherever there
// is one, which is every row this worker sends.
int accessRank(const Entry &entry) {
    if (entry.hosted) return 4;
    if (entry.guest) return 1;
    if (entry.openEnded || entry.preset == QStringLiteral("openrouter")) return 3;
    if (subscription(entry)) return 0;
    return 2;
}

// Which of two providers serving the same model comes first. The ranking file decides whenever
// both rows carry its `order` — one number, so a band's whole shape is the owner's — and the
// hard-coded `accessRank` decides only when at least one row is missing it, because the two are on
// different scales and must never be compared against each other.
bool providerBefore(const Entry &a, const Entry &b) {
    if (a.order >= 0 && b.order >= 0) {
        if (a.order != b.order) return a.order < b.order;
        return false;                       // the same rank: stable_sort keeps the row order
    }
    return accessRank(a) < accessRank(b);
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
            return providerBefore(a, b);
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
