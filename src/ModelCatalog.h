// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// relay::models — the one model catalog behind every picker and the Models page (owner, 2026-09-20).
//
// Until today a "model" in the picker was a provider plan ("kimi code · k3"): one row per preset
// with a key, and the models a provider actually serves lived in the tier table and a free-text
// box. The worker now sends a per-model catalog on every preset row (`models`, protocol 13), and
// this module turns those rows into one flat list of *entries* — a provider and a model id, with
// the reasoning levels the model takes — plus what the user has said about them:
//
//   models/shown      keys the picker shows; absent = every usable entry (so a fresh install
//                     changes nothing, and an all-unchecked list can never empty the picker)
//   models/priority   keys in rank order; rank 1 is Main (new panes), rank 2 the fallback /swap
//                     goes to; unlisted keys follow in the default order
//   models/custom     keys the user typed by hand ("add model by id"); they are entries like any
//   models/favorites  keys pinned to the top of the picker (opencode's Favorites)
//   models/recent     keys picked most recently, newest first, at most ten (opencode's Recent)
//   models/sort       the picker's sort: priority | alpha | intelligence | speed | usage | remaining
//   models/effort/K   the reasoning level remembered for one entry (opencode's per-model variant)
//   models/uses/K     how many turns ran on it, for the usage sort
//   models/speed/K    output tokens per second, a running average, for the speed sort
//
// Keys are "<preset>|<model>". A preset id can hold ":" (guest:claude, local:foo) and a model id
// can hold "/" (deepseek/deepseek-v4.1-flash); neither holds "|".
//
// Nothing here draws anything and nothing talks to the worker: it reads the `presets` array a pane
// already holds and QSettings, which is what makes tests/modelcatalog_test.cpp possible.
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace relay::models {

struct Entry {
    QString key;          // "<preset>|<model>"
    QString preset;       // "glm-coding", "guest:claude", "local:spark", "relay-free"
    QString model;        // the id the API or CLI takes
    QString label;        // lower-case display name of the model: "glm-5.3 flash", "opus"
    QString provider;     // lower-case provider: "z.ai (glm)", "claude code"
    QString plan;         // lower-case plan: "coding plan"; empty when the preset has none
    QString tier;         // the tier this is the provider's default for: main | flash | lite | ""
    QStringList efforts;  // reasoning levels the model accepts; empty = no knob
    int intelligence = -1;   // the owner's ruling (presets.INTELLIGENCE); -1 unknown
    QString openrouter;      // the same model's OpenRouter slug, when it has one (presets.OPENROUTER_TWINS)
    // What each reasoning level is called by the provider it is sent to (owner, 2026-09-20:
    // "for codex planning you pick xhigh, not max"): Relay stores its own four levels and
    // shows these. Empty for a row that sent none; `effortLabel` falls back to the level.
    QHash<QString, QString> effortLabels;
    QString effortLabel(const QString &level) const { return effortLabels.value(level, level); }
    bool usable = false;     // a stored key, a local server, a runnable harness, Relay Free available
    bool openEnded = false;  // the provider lists more than a few models (OpenRouter's live list)
    bool guest = false, local = false, hosted = false, custom = false;
    // "<label> · <provider>" — one line for a row, a status bar, a tooltip.
    QString displayName() const;
};

// One subscription window a provider reported: "5h" or "weekly", how much is spent, when it resets.
struct LimitWindow {
    QString kind;
    double usedPercent = -1;   // 0..100; -1 unknown
    qint64 resetsAt = 0;       // unix seconds; 0 unknown
};

struct Catalog {
    QList<Entry> entries;                 // every model of every preset, in the worker's order
    QHash<QString, QList<LimitWindow>> limits;   // by preset id, from `limits` / Relay Free's `quota`
    // By preset id, the provider's own verdict on the next turn as the last report carried it:
    // "allowed" | "allowed_warning" | "rejected" (usage_limits.status, protocol 29.3). Absent when
    // the provider only gave figures.
    QHash<QString, QString> status;
    QHash<QString, QString> presetLabels; // by preset id, the row's own lower-case label

    const Entry *find(const QString &key) const;
    QList<Entry> ofPreset(const QString &preset) const;
    QStringList presets() const;          // ids in order, once each
    // The preset's row for a tier ("main" → the model the provider serves as Main), or null.
    const Entry *tierEntry(const QString &preset, const QString &tier) const;

    static QString keyFor(const QString &preset, const QString &model);
    static bool splitKey(const QString &key, QString *preset, QString *model);
};

// The catalog from the worker's `presets` event, plus the entries the user added by id. A preset
// row with no `models` (an older worker, a local server whose probe listed nothing) still yields
// one entry: the row's own `model`.
Catalog catalogFrom(const QJsonArray &presets);

enum class Sort { Priority, Alphabetical, Intelligence, Speed, Usage, Remaining };
QString sortId(Sort sort);
Sort sortFromId(const QString &id);          // unknown → Priority
QString sortLabel(Sort sort);                // lower-case, for the picker's menu
QList<Sort> allSorts();

// What the user has said, in QSettings. Every reader tolerates an absent key.
namespace curation {
QStringList shownKeys();
bool isShown(const Entry &entry);                                   // absent list → usable entries, minus an open-ended provider's long tail
// The same answer against a key list the caller has already read. `shownKeys()` builds a
// QSettings, which re-stats the whole XDG search path, and the catalog has hundreds of entries on
// an OpenRouter key — asking per entry is what `shown()` below used to do (card #PPR4).
bool isShown(const Entry &entry, const QStringList &shown);
void setShown(const QString &key, bool on, const Catalog &catalog);  // first change materialises the list
void resetShown();

QStringList priority();
// Keys in rank order for this catalog: the explicit list first (dropping keys that no longer
// exist), then the default order — the default provider's main and flash, then each other usable
// preset's main, then the rest as the worker listed them.
QStringList ranked(const Catalog &catalog);
void move(const QString &key, int delta, const Catalog &catalog);    // delta -1 up, +1 down
void setRank(const QString &key, int rank, const Catalog &catalog);
void resetPriority();

QStringList customKeys();
Entry addCustom(const QString &preset, const QString &model, const Catalog &catalog);
void removeCustom(const QString &key);

QStringList favorites();
bool isFavorite(const QString &key);
void toggleFavorite(const QString &key);

QStringList recent();
void noteUse(const QString &key);   // recent (capped at ten) and the uses counter
int uses(const QString &key);
double speed(const QString &key);   // tokens per second, 0 unknown
void noteSpeed(const QString &key, double tokensPerSecond);

QString effortFor(const QString &key);
void setEffortFor(const QString &key, const QString &level);

// Models the user wants tried on OpenRouter when their own provider fails (owner, 2026-09-20: off
// by default — nobody wants surprise pay-as-you-go GPT-6 calls — and on per model, "glm 5.3
// flash for example"). models/openrouter_fallback is the list of entry keys; what the worker
// gets is their model ids.
QStringList openrouterFallbackKeys();
bool openrouterFallback(const QString &key);
void setOpenrouterFallback(const QString &key, bool on);
QStringList openrouterFallbackModels();   // model ids, for the request option

// The providers group's order (owner, 2026-09-20): the order they were added in, then dragged.
// `noteProviders` appends any listed id not yet in the order, so the first time a provider appears
// with a key is its place; `moveProviderBefore` is a drop.
QStringList providerOrder();
void noteProviders(const QStringList &listedIds);
void moveProviderBefore(const QString &id, const QString &beforeId);   // beforeId empty = to the end

// The five tier lists (owner, 2026-09-20), which replaced the single priority list and its line:
// main, high, flash, lite, local. Each is an ordered list of entries, a model and the reasoning
// level it runs at there. Rank 1 is what the tier runs on; the rest are its fallbacks, in order. A
// model in no list is only ever used when picked by hand. Stored as models/tier/<tier>, a list of
// "<preset>|<model>|<level>" (level may be empty: the model's own default).
struct TierEntry {
    QString key;      // "<preset>|<model>"
    QString effort;   // a Relay level, or empty
};
QStringList tierIds();                                   // main, high, flash, lite, local — page order
QString tierLabel(const QString &tier);                  // "main models"
bool tierListsSet();                                     // whether any list has been stored
QList<TierEntry> tierList(const QString &tier);
void setTierList(const QString &tier, const QList<TierEntry> &entries);
void addToTier(const QString &tier, const QString &key, const QString &effort = QString());
void removeFromTier(const QString &tier, const QString &key);
void moveInTier(const QString &tier, const QString &key, int toIndex);
void setTierEffort(const QString &tier, const QString &key, const QString &effort);
// The level a model runs at when picked: the main list's entry for it, else the first other list
// that names it, else empty (the pane keeps its own level, moved to one the model offers).
QString listEffortFor(const QString &key);
// Apply one of the worker's `tier_list_defaults` ({tier: [{preset, model, effort}]}).
void applyTierDefaults(const QJsonObject &lists);
void clearTierLists();

// A provider whose checkbox on Options › Models is off: every model hidden and the group folded.
QStringList collapsedProviders();
bool isCollapsed(const QString &preset);
void setCollapsed(const QString &preset, bool on);

// The fallback threshold (owner, 2026-09-20): a line in the priority list. Rank 1 is Main; every
// model above the line after it is a fallback, in order — the second model is the main fallback,
// the third the next, as many as you want. `fallbackThreshold` is how many models are above the
// line (2 by default: Main and one fallback); a ranked list shorter than that stops early.
int fallbackThreshold();
void setFallbackThreshold(int count);

Sort sort();
void setSort(Sort sort);
}  // namespace curation

// The entries the picker offers: usable, shown, in rank order.
QList<Entry> shown(const Catalog &catalog);
// The same list under one sort. Every sort is stable over `entries`' order, so ties keep rank.
QList<Entry> ordered(QList<Entry> entries, Sort sort, const Catalog &catalog);

// An exhausted subscription (owner, 2026-09-20: "grayed-out and skipped in the priority until
// it's restored"): a window the provider reports fully spent (used_percent >= 100), or the
// provider's own `status: "rejected"`, while its reset time is still ahead or unknown. The moment
// `resets_at` has passed the preset is live again without waiting for a fresh report. `now` is
// unix seconds; 0 means the clock. `exhaustedUntil` is -1 when the preset is live, 0 when it is
// exhausted with no reset time known, else the unix second it comes back.
qint64 exhaustedUntil(const Catalog &catalog, const QString &preset, qint64 now = 0);
bool exhausted(const Catalog &catalog, const QString &preset, qint64 now = 0);
// The shown list without the exhausted presets: what the priority actually runs on.
QList<Entry> live(const Catalog &catalog, qint64 now = 0);
// Rank 1 and rank 2 of the live list — an exhausted entry keeps its row but the next live one
// takes its place — or an empty Entry (key.isEmpty()) when there is none.
Entry mainDefault(const Catalog &catalog, qint64 now = 0);
Entry fallback(const Catalog &catalog, qint64 now = 0);
// Ranks 2 … threshold of the live list, in order: the failover chain the worker is sent.
QList<Entry> fallbacks(const Catalog &catalog, qint64 now = 0);
// A tier's list as live catalog entries, in order: usable and not exhausted.
QList<Entry> liveTier(const Catalog &catalog, const QString &tier, qint64 now = 0);
// The best "percent left" over a preset's windows, or -1 with no figures.
double percentLeft(const Catalog &catalog, const QString &preset);
// When a reset lands, in the words the limits line uses: today's "14:30", "tue" within a week,
// else "23 sep". Empty when `resetsAt` is unknown (0).
QString resetText(qint64 resetsAt, qint64 now);
// "5h 62% left, resets 14:30 · weekly 40% left, resets tue" — empty with no figures. `now` is
// unix seconds, for the wording of the reset time (today's hour, else a weekday).
QString limitsText(const QList<LimitWindow> &windows, qint64 now);
// Filter as opencode does: a substring match over label, model id and provider, case-insensitive,
// every word of the query somewhere in the row.
bool matches(const Entry &entry, const QString &query);

}  // namespace relay::models
