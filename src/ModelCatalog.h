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
    bool usable = false;     // a stored key, a local server, a runnable harness, Relay Free available
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
bool isShown(const Entry &entry);                                   // absent list → usable entries
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

Sort sort();
void setSort(Sort sort);
}  // namespace curation

// The entries the picker offers: usable, shown, in rank order.
QList<Entry> shown(const Catalog &catalog);
// The same list under one sort. Every sort is stable over `entries`' order, so ties keep rank.
QList<Entry> ordered(QList<Entry> entries, Sort sort, const Catalog &catalog);
// Rank 1 and rank 2 of the shown list; an empty Entry (key.isEmpty()) when there is none.
Entry mainDefault(const Catalog &catalog);
Entry fallback(const Catalog &catalog);
// The best "percent left" over a preset's windows, or -1 with no figures.
double percentLeft(const Catalog &catalog, const QString &preset);
// "5h 62% left, resets 14:30 · weekly 40% left, resets tue" — empty with no figures. `now` is
// unix seconds, for the wording of the reset time (today's hour, else a weekday).
QString limitsText(const QList<LimitWindow> &windows, qint64 now);
// Filter as opencode does: a substring match over label, model id and provider, case-insensitive,
// every word of the query somewhere in the row.
bool matches(const Entry &entry, const QString &query);

}  // namespace relay::models
