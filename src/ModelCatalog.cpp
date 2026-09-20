// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ModelCatalog.h"

#include <QDateTime>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QSet>

#include <algorithm>

namespace relay::models {

namespace {

const QString kShown = QStringLiteral("models/shown");
const QString kPriority = QStringLiteral("models/priority");
const QString kCustom = QStringLiteral("models/custom");
const QString kFavorites = QStringLiteral("models/favorites");
const QString kRecent = QStringLiteral("models/recent");
const QString kSort = QStringLiteral("models/sort");
const QString kOpenrouter = QStringLiteral("models/openrouter_fallback");
const QString kCollapsed = QStringLiteral("models/collapsed");
const QString kProviderOrder = QStringLiteral("models/provider_order");
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
    const QJsonArray limits = preset.value(QStringLiteral("limits")).toArray();
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

QString Entry::displayName() const {
    if (provider.isEmpty()) return label;
    return label + QStringLiteral(" · ") + provider;
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

        QJsonArray models = preset.value(QStringLiteral("models")).toArray();
        if (models.isEmpty()) {
            // No catalog: the row's own model is the one entry, so nothing the pane could switch
            // to today is lost. A guest with no list yet (codex's scan still running) has no model
            // id either; its entry is the guest itself, which the pane switches to as before.
            QJsonObject own{{QStringLiteral("id"), str(preset, "model")},
                            {QStringLiteral("label"), str(preset, "model").isEmpty() ? str(preset, "label").toLower() : str(preset, "model").toLower()},
                            {QStringLiteral("efforts"), preset.value(QStringLiteral("efforts"))}};
            models << own;
        }
        for (const auto &item : std::as_const(models)) {
            const QJsonObject row = item.toObject();
            Entry entry;
            entry.preset = id;
            entry.model = str(row, "id");
            entry.key = Catalog::keyFor(id, entry.model);
            entry.label = str(row, "label").isEmpty() ? entry.model.toLower() : str(row, "label").toLower();
            entry.provider = provider;
            entry.plan = str(preset, "plan").toLower();
            entry.tier = str(row, "tier");
            for (const auto &level : row.value(QStringLiteral("efforts")).toArray()) entry.efforts << level.toString();
            entry.intelligence = row.value(QStringLiteral("intelligence")).isDouble() ? row.value(QStringLiteral("intelligence")).toInt() : -1;
            entry.openrouter = str(row, "openrouter");
            entry.usable = usable;
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
            entry.label = own.toLower(); entry.provider = provider; entry.plan = str(preset, "plan").toLower();
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
        entry.key = key; entry.model = model; entry.label = model.toLower(); entry.tier.clear();
        entry.intelligence = -1; entry.custom = true;
        catalog.entries << entry;
    }
    return catalog;
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

QStringList shownKeys() { return list(kShown); }

bool isShown(const Entry &entry) {
    const QStringList keys = shownKeys();
    if (keys.isEmpty()) return entry.usable;
    return entry.usable && keys.contains(entry.key);
}

void setShown(const QString &key, bool on, const Catalog &catalog) {
    QStringList keys = shownKeys();
    if (keys.isEmpty()) {
        // First change: write down today's default so the one un-check does not hide everything.
        for (const Entry &entry : catalog.entries)
            if (entry.usable) keys << entry.key;
    }
    if (on && !keys.contains(key)) keys << key;
    if (!on) keys.removeAll(key);
    // Un-checking the last one is a reset to "everything", never an empty picker.
    if (keys.isEmpty()) { QSettings().remove(kShown); return; }
    store(kShown, keys);
}

void resetShown() { QSettings().remove(kShown); }

QStringList priority() { return list(kPriority); }

QStringList ranked(const Catalog &catalog) {
    QStringList out;
    for (const QString &key : priority())
        if (catalog.find(key) && !out.contains(key)) out << key;
    // The default order behind the explicit list.
    const QString defaultPreset = QSettings().value(QStringLiteral("provider/preset")).toString();
    auto add = [&](const Entry *entry) { if (entry && !out.contains(entry->key)) out << entry->key; };
    if (!defaultPreset.isEmpty()) {
        add(catalog.tierEntry(defaultPreset, QStringLiteral("main")));
        add(catalog.tierEntry(defaultPreset, QStringLiteral("flash")));
    }
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
    // Shown at once: adding a model and then hunting for its checkbox would be two steps for one.
    setShown(key, true, catalog);
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
    QStringList shown = shownKeys();
    shown.removeAll(key);
    store(kShown, shown);
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

QString effortFor(const QString &key) { return QSettings().value(perKey(QStringLiteral("effort"), key)).toString(); }

void setEffortFor(const QString &key, const QString &level) {
    if (key.isEmpty()) return;
    if (level.isEmpty()) QSettings().remove(perKey(QStringLiteral("effort"), key));
    else QSettings().setValue(perKey(QStringLiteral("effort"), key), level);
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

QStringList collapsedProviders() { return list(kCollapsed); }
bool isCollapsed(const QString &preset) { return collapsedProviders().contains(preset); }
void setCollapsed(const QString &preset, bool on) {
    QStringList keys = collapsedProviders();
    keys.removeAll(preset);
    if (on) keys << preset;
    store(kCollapsed, keys);
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

QList<Entry> shown(const Catalog &catalog) {
    QList<Entry> out;
    for (const QString &key : curation::ranked(catalog)) {
        const Entry *entry = catalog.find(key);
        if (entry && curation::isShown(*entry)) out << *entry;
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

Entry mainDefault(const Catalog &catalog) {
    const QList<Entry> list = shown(catalog);
    return list.isEmpty() ? Entry() : list.first();
}

Entry fallback(const Catalog &catalog) {
    const QList<Entry> list = shown(catalog);
    return list.size() < 2 ? Entry() : list.at(1);
}

QList<Entry> fallbacks(const Catalog &catalog) {
    const QList<Entry> list = shown(catalog);
    return list.mid(1, qMax(0, curation::fallbackThreshold() - 1));
}

QString limitsText(const QList<LimitWindow> &windows, qint64 now) {
    QStringList parts;
    for (const LimitWindow &window : windows) {
        if (window.usedPercent < 0) continue;
        QString part = QStringLiteral("%1 %2% left").arg(window.kind).arg(qRound(qBound(0.0, 100.0 - window.usedPercent, 100.0)));
        if (window.resetsAt > 0) {
            const QDateTime resets = QDateTime::fromSecsSinceEpoch(window.resetsAt);
            const QDateTime at = QDateTime::fromSecsSinceEpoch(now);
            const QString when = resets.date() == at.date()
                ? resets.toString(QStringLiteral("HH:mm"))
                : resets.secsTo(at) > -7 * 86400 ? QLocale::c().dayName(resets.date().dayOfWeek(), QLocale::ShortFormat).toLower()
                                                  : resets.toString(QStringLiteral("d MMM")).toLower();
            part += QStringLiteral(", resets %1").arg(when);
        }
        parts << part;
    }
    return parts.join(QStringLiteral(" · "));
}

bool matches(const Entry &entry, const QString &query) {
    const QString haystack = (entry.label + QLatin1Char(' ') + entry.model + QLatin1Char(' ') + entry.provider
                              + QLatin1Char(' ') + entry.plan).toLower();
    for (const QString &word : query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (!haystack.contains(word)) return false;
    return true;
}

}  // namespace relay::models
