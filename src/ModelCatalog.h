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
//   models/priority   the single rank order the five tier lists replaced (owner, 2026-09-20).
//                     Nothing writes it any more, and once any tier list is stored `ranked()`
//                     stops reading it: a stale list from before the lists existed must not
//                     re-order what Options › Models now shows. Kept only so an install that has
//                     never seen the tier lists keeps the order it had (card #MDL1).
//   models/available  step 2 of the four (owner, 2026-09-21): the models that exist for the lists,
//                     the box and its filter at all. Absent = the default in `curation` below.
//                     Per machine, never per profile.
//   models/custom     keys the user typed by hand ("add model by id"); they are entries like any
//   models/favorites  keys pinned to the top of the picker (opencode's Favorites)
//   models/recent     keys picked most recently, newest first, at most ten (opencode's Recent)
//   models/sort       the picker's sort: priority | alpha | intelligence | speed | usage | remaining
//   models/uses/K     how many turns ran on it, for the usage sort
//   models/speed/K    output tokens per second, a running average, for the speed sort
//
// Retired with card #MDL1 t:a10, and ignored rather than migrated where an old settings file
// still holds them: `models/shown` (the "models in the picker" checklist) and `models/collapsed`
// (its per-provider fold). They are *not* `models/available`: the checklist said what the pickers
// listed out of everything a provider serves, and its per-provider fold is a widget's state.
// `models/available` says what exists for the lists and the box at all, it is edited in the
// Ctrl+Alt+M dialog rather than on Options › Models, and its default is not "everything".
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
    // What a person reads, and the only name this model has (card #MDL1, rule 1): lower-case, no
    // spaces, no vendor prefix — "glm-5.3-flash", "claude-opus-5.5" for Claude Code's `opus`,
    // "gpt-6-sol" whether it comes from codex, the OpenAI API or "openai/gpt-6-sol" on
    // OpenRouter. The worker computes it (`presets.model_name`) and sends it as the row's `name`;
    // a row without one — a custom id, an older worker — gets `nameOf(model)`.
    QString name;
    // The same string. It was the prettified "glm-5.3 flash" until #MDL1 and is kept so the call
    // sites that print a row still compile and now print the name.
    QString label;
    QString provider;     // lower-case provider: "z.ai (glm)", "claude code"
    QString plan;         // lower-case plan: "coding plan"; empty when the preset has none
    // How this provider is reached, and where it sorts against the others serving the same model:
    // its row of backend/relay_core/model-ranking.md, which the owner edits, sent per preset
    // (protocol 13.2). `kind` is plan | harness | api | router | free and `order` is the
    // tie-break, lower first. `grouped()` sorts a fold by `order` instead of keeping a second copy
    // of rule 2.2 here; `order` is -1 on a row that carried none — an older worker — and then the
    // hard-coded shape in `accessRank` decides, exactly as it did before this pair existed.
    QString kind;
    int order = -1;
    QString tier;         // the tier this is the provider's default for: main | flash | lite | ""
    // The reasoning levels this *model* takes, in the provider's own order and the provider's own
    // words (owner, 2026-09-21: "i want the effort options in relay to be determined by the model
    // … so xhigh shows up for codex for example"). Codex serves low medium high xhigh max ultra,
    // Claude Code stops at xhigh, Kimi has three, Relay Free two. Empty means the model has no
    // reasoning knob at all. Relay no longer keeps a vocabulary of its own: this list is the
    // vocabulary, the wire carries the word as it stands here, and the worker validates it per
    // model. `effortLadder()` below only says which of two words is the higher one.
    QStringList efforts;
    // What the provider says this model's own default level is (the worker's `default_effort`: a
    // guest's `default_reasoning_level`, a cloud model's tier extra read back), and the level it
    // starts at in each list — `tier_effort`, `{main, high, flash, lite}` — for the row's `+ add a
    // model…` button (card #TKN7). Both in the provider's own words; empty where it states none,
    // and on a worker that sends neither (then `tierStartEffort` reads the same rules off the row).
    QString defaultEffort;
    QHash<QString, QString> tierEffort;
    int intelligence = -1;   // the owner's ruling (presets.INTELLIGENCE); -1 unknown
    QString openrouter;      // the same model's OpenRouter slug, when it has one (presets.OPENROUTER_TWINS)
    // Whether the level box is greyed for this model (owner, 2026-09-21: "for no knob models, the
    // effort box should be grayed out. same for relay free."). The worker sends `effort_fixed` per
    // model row; until it does, the same two cases are derived from the row itself — a model with
    // no levels, and Relay Free, where the gateway picks the level for the role whatever the pane
    // asks for. A fixed model still has `efforts` when the provider reports them: they are what the
    // greyed box shows, not something the pane may change.
    bool effortFixed = false;
    // One sentence for the greyed box's tooltip and for what Alt+E and `/effort` say instead of
    // opening; empty when the level is the pane's to set.
    QString effortFixedReason() const;
    bool usable = false;     // a stored key, a local server, a runnable harness, Relay Free available
    bool openEnded = false;  // the provider lists more than a few models (OpenRouter's live list)
    bool guest = false, local = false, hosted = false, custom = false;
    // "<name> · <provider>" — one line for a row, a status bar, a tooltip.
    QString displayName() const;
};

// The last step of the naming rule, for a model id nothing else knows anything about: everything
// up to the last "/" removed, a leading "~" removed (OpenRouter's moving aliases are written
// `~openai/gpt-sol-latest`), lower-cased, whitespace turned into "-". The worker's `name` wins
// wherever there is one; this is what a hand-typed id and an older worker's row get, and it is the
// same derivation, so `openai/gpt-6-sol` typed by hand folds into the existing gpt-6-sol row.
QString nameOf(const QString &modelId);

// ----- reasoning levels: the model's list is the vocabulary (card #MDL1, 2026-09-21) ------------
//
// Relay used to own four levels — low, medium, high, max — and every picker validated against
// them, which is why a codex pane could not be put on `xhigh` or `ultra` and why a Kimi pane
// offered a `medium` that was the same request as `high`. The levels are the provider's now
// (`Entry::efforts`). Nothing validates against the ladder below: it is only an order, so that
// "is xhigh above or below max" has an answer when a pane carrying one model's level moves to a
// model that does not take it.

// Every level word Relay has seen, lowest first. Codex's own list is the longest and each other
// provider's is a subset of it in the same order, which is what makes one ladder enough.
QStringList effortLadder();
// Where `level` sits on it, -1 for a word the ladder has never heard of.
int effortRank(const QString &level);
// The level of `levels` that a stored one lands on when the model does not take it: the model's
// top when the stored level is above all of them, its lowest when below, and otherwise the nearest
// by ladder position with ties going up — so `xhigh` on a model serving low/medium/high/max lands
// on `max`, not `high`. `levels` empty (a model with no knob) answers empty, and a word off the
// ladder is treated as `high`, which is where a pane sits by default.
QString nearestEffort(const QStringList &levels, const QString &level);

// One subscription window a provider reported: "5h" or "weekly", how much is spent, when it resets.
struct LimitWindow {
    QString kind;
    double usedPercent = -1;   // 0..100; -1 unknown
    qint64 resetsAt = 0;       // unix seconds; 0 unknown
};

struct Catalog {
    QList<Entry> entries;                 // every model of every preset, in the worker's order
    QHash<QString, QList<LimitWindow>> limits;   // by preset id, from `limits` / Relay Free's `quota`
    QHash<QString, qint64> limitUpdatedAt; // unix seconds of the last quota report, when known
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
    // The key of this preset's entry for a model the worker has just reported running (card #MDL1,
    // rule 3): its model id if the preset lists it, else the entry whose *name* is that model's
    // name — so `guest:claude` reporting `claude-opus-5-5` resolves to `guest:claude|opus`, and the
    // pane's key, the "current" mark, recents and usage counts all see one model. Empty when this
    // preset has no such entry: the caller decides what that means, and no key is invented.
    QString resolveKey(const QString &preset, const QString &reportedModel) const;

    static QString keyFor(const QString &preset, const QString &model);
    static bool splitKey(const QString &key, QString *preset, QString *model);
};

// The catalog from the worker's `presets` event, plus the entries the user added by id. A preset
// row with no `models` (an older worker, a local server whose probe listed nothing) still yields
// one entry: the row's own `model`.
Catalog catalogFrom(const QJsonArray &presets);

// The level a model starts at when the user adds it to one of the lists by hand (Options › Models'
// `+ add a model…`, card #TKN7): the row's own `tier_effort` for that list, which the worker
// computes once (`presets.tier_start_efforts` — Main is the provider's own default, High the level
// a plan turn uses, Flash and Lite the lowest). A row that carries none — an older worker — falls
// back to the same three rules read off the entry itself, except the guest's own High rule, which
// only the worker can know. Empty means the entry carries no level: the model's own default.
QString tierStartEffort(const Entry &entry, const QString &tier);

enum class Sort { Priority, Alphabetical, Intelligence, Speed, Usage, Remaining };
QString sortId(Sort sort);
Sort sortFromId(const QString &id);          // unknown → Priority
QString sortLabel(Sort sort);                // lower-case, for the picker's menu
QList<Sort> allSorts();

// What the user has said, in QSettings. Every reader tolerates an absent key.
namespace curation {

// One redraw's worth of settings reads, memoised. A picker redraw asks the same few keys —
// the available ticks, the lists, favorites, speeds — once per model, and every ask was a fresh
// QSettings lookup: ~2,000 of them per keystroke on OpenRouter's catalog, which is what froze the
// models pane while typing (owner, 2026-09-22). While a scope lives each key is read once; any
// write through this namespace drops the memo. Hold one for a read-only pass, never across edits.
class ReadScope {
public:
    ReadScope();
    ~ReadScope();
    ReadScope(const ReadScope &) = delete;
    ReadScope &operator=(const ReadScope &) = delete;
};
QStringList priority();
// Keys in rank order for this catalog: the tier lists first, main leading (dropping keys that no
// longer exist), then — only on an install that has stored no tier list at all — whatever
// `models/priority` still holds, then the default order: each usable preset's main, then the rest
// as the worker listed them. It reads no `provider/preset` (card #MDL1, rule 3): that key is the
// last provider *some pane* switched to, and letting it re-order every picker is how "which model
// is selected first" stopped being answerable.
QStringList ranked(const Catalog &catalog);
void move(const QString &key, int delta, const Catalog &catalog);    // delta -1 up, +1 down
void setRank(const QString &key, int rank, const Catalog &catalog);
void resetPriority();

QStringList customKeys();
Entry addCustom(const QString &preset, const QString &model, const Catalog &catalog);
void removeCustom(const QString &key);

// ----- step 2 of four: which models are *available* (owner, 2026-09-21) -------------------------
// > "there need to be 4 steps of model availability: 1 add provider, 2 add model as available,
// >  3 add model to priority list, 4 include model in box picker. we currently only have 1, 3, 4.
// >  and its step 2 that determines the models available in the text filter."
//
// Step 2 came back with this card, after t:a10 retired `models/shown` on the reading that "a model
// a provider serves is a model you can pick". It is not the old checklist: that one decided what
// the *pickers* listed, this one decides what exists at all — the lists (step 3), the box (step 4)
// and the box's typed filter all read it, and a model that is not available is in none of them.
//
//   models/available   the keys that are available. Absent — the ordinary case — is the DEFAULT
//                      below, which is why nothing is written until the first uncheck.
//
// The default, in the owner's words: "for branded providers, all models are included by default
// and you can uncheck them (eg i probably want to uncheck sonnet and haiku and gpt 5.5). but then
// for openrouter, you have to select specific models -- and maybe there are some recommended ones
// by default, deepseek 4.1 and gemini 3.8 flash for example." So:
//
//   * a branded (non-open-ended) provider: every model it serves;
//   * an open-ended one (`Entry::openEnded` — OpenRouter's four hundred live rows): only the
//     recommended ones, which are the rows the worker's own catalog names (`presets.MODEL_CATALOG`
//     for `openrouter`: deepseek/deepseek-v4.1-flash, google/gemini-3.8-flash,
//     google/gemini-3.5-flash-lite — they are the rows that carry a `tier`; the live listing's do
//     not), plus anything a tier list names and anything you typed yourself (`models/custom`).
//
// **Per machine, not per profile.** A profile is the five lists and nothing else (see `profiles()`
// below): which models this machine can reach is not a thing to swap between "AI work" and "admin
// work", and a profile that hid half a provider would read as the provider being broken.
//
// The tick is the user's even for a model a list ranks (#AVR8, 2026-09-22): an un-ticked ranked
// model keeps its rank, is drawn greyed on Priorities, and is skipped by everything that runs a
// list — `activeTierList` below. It used to pin the tick on, so an un-tick never took.
QStringList availableKeys();
// The list read once, for a caller walking a whole catalog (the same trap as `shown`, #PPR4).
bool isAvailable(const Entry &entry, const QStringList &available);
bool isAvailable(const Entry &entry);
// Check or un-check one. The first change snapshots today's default, exactly as `setShown` did, so
// one un-check never empties the picker; un-checking the last one is a reset to the default rather
// than "nothing is available". `catalog` is what the snapshot is taken over.
void setAvailable(const QString &key, bool on, const Catalog &catalog);
void resetAvailable();

QStringList favorites();
bool isFavorite(const QString &key);
void toggleFavorite(const QString &key);

QStringList recent();
void noteUse(const QString &key);   // recent (capped at ten) and the uses counter
int uses(const QString &key);
double speed(const QString &key);   // tokens per second, 0 unknown
void noteSpeed(const QString &key, double tokensPerSecond);

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
    int rank = 0;     // 1-based; 0 is a legacy entry whose position supplies its rank
};
QStringList tierIds();                                   // main, high, flash, lite, local — page order
QString tierLabel(const QString &tier);                  // "main models"
bool tierListsSet();                                     // whether any list has been stored
QList<TierEntry> tierList(const QString &tier);
// The list as it runs: `tierList` without the models un-ticked on Available (#AVR8). What the
// worker, the box and the defaults read; `tierList` stays the stored order that edits rewrite, so
// a model ticked again comes back at the rank it had. Lite is returned whole: the tick is about
// terminal agents, not chores.
QList<TierEntry> activeTierList(const QString &tier);
// `isAvailable` for a key some list names, without a catalog (exact for those keys).
bool isAvailableKey(const QString &key, const QStringList &available);
void setTierList(const QString &tier, const QList<TierEntry> &entries);
void addToTier(const QString &tier, const QString &key, const QString &effort = QString());
void removeFromTier(const QString &tier, const QString &key);
void moveInTier(const QString &tier, const QString &key, int toIndex);
void setTierEffort(const QString &tier, const QString &key, const QString &effort);
void setTierRank(const QString &tier, const QString &key, int rank);
// The level a model runs at when picked: the main list's entry for it, else the first other list
// that names it, else empty (the pane keeps its own level, moved to one the model offers).
QString listEffortFor(const QString &key);
// Whether any of the five lists names this key, and whether one of the four **terminal** classes
// does — `boxClasses()`: high, main, flash, local. The pair exists because `lite` is not a pane
// mode: the box has no lite row (design 5.3), the models pane is about what a terminal agent can
// run, and so a model the lite list names is still the user's to un-tick on the available tab
// (owner, 2026-09-21: "this tab is only for terminal agents, so gemini flash lite should be
// optional"). Only `inTerminalList` pins availability; the chores read `models/tier/lite` straight
// and are untouched by the tick either way.
bool inAnyList(const QString &key);
bool inTerminalList(const QString &key);

// ----- what the box shows of each list (card #MDL1, design 5.3) --------------------------------
// The Alt+M box draws a **class** per header — high, main, flash, and local where this machine
// serves one — and under it that class's list down to a **cutoff**: two rows by default. A class
// can also be switched off entirely. Both are the dialog's "show in box" column and its "show this
// class in the box" switch, and both belong to the lists, so they are stored beside them and
// travel with the profile:
//
//   models/box/<tier>      the cutoff rank — how many of that list the box draws. Absent = 2.
//   models/box_off/<tier>  true when the class is switched off. Absent = the class is shown.
//
// `lite` is never a pane mode and never appears in the box, so it has neither setting; asking for
// it answers the defaults and setting it is a no-op.
constexpr int kBoxCutoffDefault = 2;
// high, main, flash, local — the classes the box can draw, in the order it draws them.
QStringList boxClasses();
int boxCutoff(const QString &tier);              // >= 1; kBoxCutoffDefault when nothing is stored
void setBoxCutoff(const QString &tier, int rank);   // clamped at 1; writes through to the profile
bool boxShown(const QString &tier);              // true when nothing is stored
void setBoxShown(const QString &tier, bool on);
// Apply one of the worker's `tier_list_defaults` ({tier: [{preset, model, effort}]}).
void applyTierDefaults(const QJsonObject &lists);
void clearTierLists();

// ----- profiles (owner, 2026-09-20 evening) -----------------------------------------------------
// "we need model user profiles like warp for the priority lists … so I can have an 'AI work'
// profile and an 'admin work' profile that sets different model priorities." A profile is a named
// snapshot of the five tier lists and nothing else: no permissions, no MCP, no per-project binding
// (Warp's profiles carry all three; Relay's five lists are the thing the owner asked to name).
//
//   models/profiles/<name>/tier/<tier>   that profile's copy of one list, same format as the live one
//   models/profile_order                 the names, in creation order (the page's order)
//   models/profile                       the name that is current; absent = none, the lists are unnamed
//
// The live lists and the current profile are *one thing*: editing a list while a profile is current
// writes through to it, so there is no "unsaved changes" state to explain or lose. Switching is
// therefore lossless in both directions, which is what makes it safe to do from `/profile`.
QStringList profiles();                  // names, in creation order
QString currentProfile();                // "" when the lists belong to no profile
void saveProfile(const QString &name);   // the live lists → that profile; it becomes current
void applyProfile(const QString &name);  // that profile's lists → the live lists; it becomes current
// No-ops when `from` is not a profile, `to` is empty or invalid, or `to` is already taken — the
// page checks first and says so, and a silent overwrite of the other profile is never what was meant.
void renameProfile(const QString &from, const QString &to);
// The live lists are left exactly as they are: a profile is a name for them, and deleting the name
// is not a reason to change what this machine runs on. Current becomes "".
void deleteProfile(const QString &name);
// Whether this is a usable profile name: non-empty, no "/" or "\" (they would make QSettings groups).
bool validProfileName(const QString &name);

// ----- profiles on disk (owner, 2026-09-21: "allow exporting and importing profiles") -----------
// A profile leaves this machine as JSON, so one can be mailed to a colleague, kept in a dotfiles
// repo or carried to a second machine:
//
//   {"relay": "model profiles", "version": 1, "exported": "<ISO 8601>",
//    "profiles": [{"name": "AI work",
//                  "lists": {"main": [{"preset": "glm-coding", "model": "glm-5.3", "effort": "max"}]},
//                  "box": {"main": {"cutoff": 3, "shown": true}}}]}
//
// The list entries are the same {preset, model, effort} objects the worker sends as
// `tier_list_defaults`, so a default the worker computed and a profile the user exported read the
// same. A model the importing machine has no provider for is kept, not dropped: the file may well
// arrive before the key does, and an entry nothing can run is skipped at failover time anyway.
// What the box draws of one class: how far down its list, and whether it is drawn at all. The
// defaults here are the same "absent key" the settings have, so a profile written before the box
// had classes imports as "two per class, all four on".
struct BoxSetting {
    int cutoff = kBoxCutoffDefault;
    bool shown = true;
    bool operator==(const BoxSetting &other) const { return cutoff == other.cutoff && shown == other.shown; }
};
struct ProfileDoc {
    QString name;
    QHash<QString, QList<TierEntry>> lists;   // tier id -> its entries, in order; absent = empty
    // class id -> what the box shows of it (`"box"` in the file); absent = the defaults above.
    QHash<QString, BoxSetting> box;
};
// The document for those profiles, in the order given; names that are not profiles are skipped.
QJsonObject exportProfiles(const QStringList &names);
// The profiles a document holds, in file order. On a document that is not one of ours the list is
// empty and `*error` (when given) says why, in a sentence fit to show. A lone {"name", "lists"}
// object is read too, so a hand-written one-profile file works.
QList<ProfileDoc> readProfiles(const QJsonObject &document, QString *error = nullptr);
// Store it under its name, creating or replacing. It does not become current - importing must not
// change what this machine is running on - but replacing the *current* profile does move the live
// lists onto it, because the lists and the current profile are one thing.
void writeProfile(const ProfileDoc &profile);

// The fallback threshold (owner, 2026-09-20): a line in the priority list. Rank 1 is Main; every
// model above the line after it is a fallback, in order — the second model is the main fallback,
// the third the next, as many as you want. `fallbackThreshold` is how many models are above the
// line (2 by default: Main and one fallback); a ranked list shorter than that stops early.
int fallbackThreshold();
void setFallbackThreshold(int count);

Sort sort();
void setSort(Sort sort);
}  // namespace curation

// The entries the picker offers, in rank order: every usable entry that is **available**
// (`curation::isAvailable`, step 2 of the four). With nothing un-checked that is every model of a
// branded provider plus an open-ended one's recommended rows, which is the rule t:a10 wrote here
// by hand; since 2026-09-21 it is the default of a setting rather than the only rule, because
// "you can uncheck them (eg i probably want to uncheck sonnet and haiku and gpt 5.5)".
QList<Entry> shown(const Catalog &catalog);
// Every usable entry in rank order — `shown()` plus what it holds back (an open-ended provider's
// long tail, and anything un-checked). The Ctrl+Alt+M dialog's filter searches this for "more from
// openrouter", and `/model <name>` falls back to it: typing a name is asking for that model.
QList<Entry> allUsable(const Catalog &catalog);
// The rows step 2 is *edited* on — the `all` tab of the dialog: every usable entry that is
// available, plus every one that would be available by default and has been un-checked. An
// un-checked row therefore stays in the tab, greyed, with its box there to tick again; an
// open-ended provider's long tail is still behind typing, because it is neither.
QList<Entry> curatable(const Catalog &catalog);
// Relay Free's own chore lane: a **hosted** row whose one ranking class is `lite` — `relay-lite`
// today. The gateway clamps each of its three pseudo-models to that role's ceiling, so this one is
// the lite chore lane and not a model a pane can be put on; `shown`, `allUsable` and `curatable`
// all leave it out, which is every surface a terminal agent picks from (owner, 2026-09-21: "and
// relay lite shouldnt show up"). The lite *list* still holds it and the chores still run on it.
bool liteOnlyRole(const Entry &entry);
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
// One draw from the best live rank. `unitDraw` in [0, 1) is injectable for deterministic tests;
// a negative value uses the process RNG. Missing or stale quota figures give equal weights.
Entry drawTier(const Catalog &catalog, const QString &tier, qint64 now = 0, double unitDraw = -1);

// ----- one default, and /swap as a toggle (card #MDL1, rule 3) ----------------------------------
// > A pane runs on rank 1 of the main list until you pick something else *in that pane*.
//
// Both answers are computed here, out of a catalog and two strings, so the rule is one place and
// tests/modelcatalog_test.cpp can state it. Nothing below reads the pane, writes a setting or
// touches the worker.

// What a pane starts on. `entry.key` empty means "this catalog cannot answer": the caller falls
// back to its own ladder (the saved preset, the worker's `warp_default`, the first stored key —
// Pane's `presets` handler), which is the only thing that works on an install whose worker has
// not sent a catalog yet.
struct StartChoice {
    Entry entry;
    // The level to start at: the main list's own level for that entry, empty where it has none —
    // then the pane keeps `agent/effort`, the global default, exactly as before.
    QString effort;
    // Whether this is the pane's *own* saved entry coming back (a restored pane) rather than rank
    // 1 of the main list. The caller says so differently and does not re-announce a default.
    bool restored = false;
};
// `restoredPreset`/`restoredModel` are what a restored pane saved, empty for a new one. The saved
// entry wins while it is still usable and not exhausted — resolved through `Catalog::resolveKey`,
// so a guest that saved `claude-opus-5-5` comes back as `guest:claude|opus`. Otherwise rank 1 of the
// main list, **guests included** (owner, 2026-09-21: a harness ranked first is what a new pane
// starts on; the harness process starts on the first turn, which is the caller's half of it).
StartChoice startEntry(const Catalog &catalog, const QString &restoredPreset, const QString &restoredModel,
                       qint64 now = 0);

// Which kind of step /swap took, for the sentence and for the caller's own bookkeeping.
enum class SwapKind {
    None,       // nowhere to go; `SwapStep::message` says why
    Main,       // off rank 1 → rank 1, remembering where the pane was
    Back,       // on rank 1 → the model this pane came from
    Fallback    // on rank 1 with nothing remembered → rank 2
};
struct SwapStep {
    Entry target;         // where the pane goes; `key` empty when kind is None
    QString remember;     // what the pane should remember afterwards; empty clears the memory
    SwapKind kind = SwapKind::None;
    QString message;      // always set: the sentence to put on the status line
};
// /swap as a toggle with memory. `currentKey` is the pane's entry key (resolved through
// `resolveKey`, so a guest's reported model is its list entry) and `rememberedKey` is what the
// last swap off rank 1 stored — both may be empty. Off rank 1 it remembers where the pane is and
// goes to rank 1; on rank 1 it goes back to the remembered model, or to rank 2 when there is
// none. A pane whose own subscription is spent goes to the first live entry whatever rank it held.
SwapStep swapTarget(const Catalog &catalog, const QString &currentKey, const QString &rememberedKey,
                    qint64 now = 0);
// The best "percent left" over a preset's windows, or -1 with no figures.
double percentLeft(const Catalog &catalog, const QString &preset);
// When a reset lands, in the words the limits line uses: today's "14:30", "tue" within a week,
// else "23 sep". Empty when `resetsAt` is unknown (0).
QString resetText(qint64 resetsAt, qint64 now);
// "5h 62% left, resets 14:30 · weekly 40% left, resets tue" — empty with no figures. `now` is
// unix seconds, for the wording of the reset time (today's hour, else a weekday).
QString limitsText(const QList<LimitWindow> &windows, qint64 now);
// Filter as opencode does: a substring match over the model's name, its id, its provider and its
// plan, case-insensitive, every word of the query somewhere in the row. The provider is in the
// haystack on purpose (design edge case 11): typing "openrouter" finds the row and names the entry
// to pick out of its group.
bool matches(const Entry &entry, const QString &query);

// ----- groups: the small picker shows a model once (card #MDL1, rule 2) -------------------------
// One name, one row. `gpt-6-sol` is served by Codex, the OpenAI API and OpenRouter; the box and
// the Ctrl+Alt+M picker show it once and the row says which provider it will use. Options › Models
// and the tier lists keep working on entries, because that is where the order between providers is
// expressed.
struct Group {
    QString name;          // the shared `Entry::name`; the row's text
    QList<Entry> entries;  // the providers that serve it, in preference order (see `grouped`)

    // The entry picking this row runs: the first that is usable and not exhausted, else the first
    // usable one, else the first — a group is never empty and always answers with something, so a
    // caller never has to special-case a row it just drew. `now` is unix seconds; 0 means the clock.
    Entry preferred(const Catalog &catalog, qint64 now = 0) const;
    // Whether the row is greyed: no entry in it is usable and unexhausted. A subscription running
    // out therefore does not remove the row — the next provider in it takes the turn — and only
    // when every one is spent does the row go grey (rule 2, "the payoff").
    bool spent(const Catalog &catalog, qint64 now = 0) const;
    // The entry a "via" names: `/model gpt-6-sol@openrouter`, or clicking a provider in the
    // picker's via list. Matches a preset id ("guest:codex" and plain "codex" both), or the
    // provider's words ("openrouter", "claude code"). Null when this group has no such entry; the
    // pointer is into `entries` and lives as long as the group.
    const Entry *via(const QString &presetOrProviderText) const;
};

// `rows` folded into one group per name — callers pass `shown(catalog)`, `live(catalog)` or any
// ordered list they already have, and the groups come out in the order each name first appeared,
// so the rank order the caller handed in survives.
//
// Inside a group the entries are in *preference* order: (a) where the user already ranked them, by
// position in the tier lists with main first; (b) then the kind of access — a subscription or plan
// first, then a guest harness, then a first-party pay-as-you-go API, then OpenRouter, then Relay
// Free; (c) stable otherwise, so the caller's order breaks the remaining ties. A local entry never
// joins a group with a non-local one (design section 3.2: "local" is a promise about where the text
// goes), so it gets its own group even when the name matches.
//
// `now` is accepted for symmetry with the rest of this header and because a caller has one to
// hand; the order does not depend on the clock — which entry is *live* does, and that is
// `preferred` and `spent`.
QList<Group> grouped(const Catalog &catalog, const QList<Entry> &rows, qint64 now = 0);

// The entry `text` names, for `/model <name>` and `/model <name>@<provider>`: the group whose name
// it is (case-insensitive; a bare model id is understood too), then that group's `via` for the part
// after "@", or its `preferred` without one. Null when no row matches. The pointer is into `rows`,
// which the caller owns.
const Entry *findByName(const Catalog &catalog, const QList<Entry> &rows, const QString &text);

}  // namespace relay::models
