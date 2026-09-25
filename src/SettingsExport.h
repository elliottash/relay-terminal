// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QJsonObject>
#include <QJsonValue>

#include <functional>

// Settings export/import (#05J2). One versioned JSON bundle for non-secret Relay
// preferences, shared by the Options "Export settings…" / "Import settings…"
// dialogs, the command palette and the headless `relay --export-settings` /
// `relay --import-settings` CLI paths.
//
// Scope (owner decision on the card): customized Agent/Security/appearance/
// theme/provider/Switchboard settings, model role assignments and provider or
// model priorities, custom hotkeys from keybindings.json, global aliases, and —
// only when explicitly opted in — global memories and instructions. Local model
// endpoint URLs ride along after validation. API keys, tokens, remote identity
// and pinned devices, usage/recent/availability state, machine-specific paths
// and memory limits are never exported.
//
// Import merges per QSettings key / model role / hotkey action / alias /
// endpoint. Identical values and "incoming non-default over local default"
// merge automatically; differing non-defaults are conflicts that the review
// screen must resolve (Keep current / Use imported) before anything is applied.
// Nothing is written while a conflict is unresolved, and a failed backup or
// write leaves the profile exactly as it was.

namespace relay::settingsexport {

// ---------------------------------------------------------------------------
// Registry of exportable preference keys
// ---------------------------------------------------------------------------

struct SettingSpec {
    QString key;      // QSettings key, e.g. "agent/effort"
    QVariant fallback; // the default the UI falls back to when the key is absent
    QString label;     // human label for the review screen
};

// The static part of the allow-list. Keys not listed here and not covered by a
// dynamic family (roles/<role>/..., tiers/<tier>, models/..., keybindings.json,
// aliases, memories, local endpoints) are never exported.
const QList<SettingSpec> &registry();

// Resolves a QSettings key against the static registry and the dynamic
// families. Returns the effective fallback (invalid variant when the family
// member simply has no default) and sets `label` when a spec matches.
// Returns false when the key is not exportable at all.
bool exportableKey(const QString &key, QVariant *fallback = nullptr, QString *label = nullptr);

// True for keys this feature must never move between machines: anything that
// names this host, this pairing, or live agent state.
bool neverExported(const QString &key);

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

struct ExportOptions {
    // Global memories ($XDG_CONFIG_HOME/relay/switchboard/memory/*.md) and the
    // global instructions file ($XDG_CONFIG_HOME/relay/relay.md) are only
    // included when the user opts in.
    bool includeMemories = false;
};

// Collects the bundle from the current profile. Paths follow the backend's
// conventions (XDG_CONFIG_HOME, RELAY_* overrides are honored by Qt's
// AppConfigLocation for the GUI settings; the switchboard tree is read from
// XDG_CONFIG_HOME exactly like backend/relay_core/aliases.py).
QJsonObject collectBundle(const ExportOptions &options);

// Reads and validates a bundle file: right format, right version. Returns a
// null object and fills `error` when the file is not a usable bundle.
QJsonObject readBundle(const QString &path, QString *error = nullptr);

bool writeBundleToFile(const QJsonObject &bundle, const QString &path, QString *error = nullptr);

// ---------------------------------------------------------------------------
// Import planning
// ---------------------------------------------------------------------------

enum class ItemKind {
    Setting,      // one QSettings key (or a roles/<role>/... / tiers/... / models/... member)
    Hotkey,       // one action binding from keybindings.json
    HotkeyMeta,   // keybindings.json preset / program_keys
    Theme,        // a user theme file plus the theme/name choice
    Alias,        // one global alias card
    Memory,       // one global memory card (opt-in export)
    Instruction,  // the global instructions file (opt-in export)
    Endpoint,     // one local model endpoint
};

enum class ItemStatus {
    Auto,       // merges without asking (identical, or incoming non-default over a local default)
    Conflict,   // both sides carry a different non-default value: the review screen must choose
    Attention,  // valid but unverifiable here (e.g. a model id not known to this install)
    Skip,       // never applied; reported with a reason (unknown action, bad shortcut, bad endpoint)
};

enum class Resolution { Unresolved, KeepCurrent, UseImported };

struct PlanItem {
    ItemKind kind = ItemKind::Setting;
    QString id;      // settings key / action id / alias name / endpoint id / theme id / "instructions"
    QString label;   // what the review screen shows
    ItemStatus status = ItemStatus::Auto;
    QString reason;  // why this item is Skip / Attention
    QString currentDisplay; // pretty-printed local value ("(default)" when unset)
    QString incomingDisplay;
    QJsonObject incoming;   // raw payload used when the item is applied
    Resolution resolution = Resolution::Unresolved; // meaningful for Conflict / Attention items
};

struct Hooks {
    // Validates model identifiers against this installation's catalog. Null or
    // a false return marks model-bearing items Attention instead of silently
    // applying an id this build cannot serve.
    std::function<bool(const QString &modelId)> isKnownModel;
    // Validates hotkey action ids against this build's action catalog. Null
    // accepts every action id (tests); a false return marks the item Skip.
    std::function<bool(const QString &actionId)> isKnownAction;
};

struct ImportPlan {
    QString sourcePath;
    QList<PlanItem> items;
    QString error; // non-empty: the bundle was refused, nothing else is meaningful

    int count(ItemStatus status) const;
    bool hasConflicts() const { return count(ItemStatus::Conflict) > 0; }
    // Conflicts the review screen has not answered yet.
    bool hasUnresolvedConflicts() const;
    // Bulk resolution for the remaining conflicts.
    void resolveAllConflicts(Resolution resolution);
    // Attention items the user has not explicitly accepted; they apply nothing.
    bool hasUnacceptedAttention() const;
};

// Compares the bundle against the current profile and produces per-item
// decisions. Reads the profile; changes nothing.
ImportPlan planImport(const QJsonObject &bundle, const QString &sourcePath,
                      const Hooks &hooks = {});

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

struct ApplyResult {
    bool ok = false;
    QString error;
    QString backupPath;     // timestamped backup written before anything changed
    QStringList applied;   // ids of applied items
    QStringList skipped;   // ids of Skip items reported, not applied
};

// Applies a plan. Refuses (ok=false, profile untouched) while any conflict is
// unresolved or an Attention item is unaccepted. Writes a timestamped backup
// first; any failure restores that backup and leaves the profile unchanged.
ApplyResult applyImport(const ImportPlan &plan);

// Restores a backup written by applyImport().
bool restoreBackup(const QString &backupPath, QString *error = nullptr);

// Helpers shared with the dialogs / CLI.
QString switchboardRoot();          // $XDG_CONFIG_HOME/relay/switchboard
QString globalAliasesDir();         // .../switchboard/aliases
QString globalMemoriesDir();        // .../switchboard/memory
QString globalInstructionsFile();   // $XDG_CONFIG_HOME/relay/relay.md
QString localModelsFile();          // $XDG_CONFIG_HOME/relay/local-models.json
QString keybindingsFile();          // <AppConfigLocation>/keybindings.json
QString backupDir();                // <AppConfigLocation>/backups
QString themeFileFor(const QString &themeId); // $XDG_CONFIG_HOME/relay/themes/<id>.toml

// Mirrors backend/relay_core/localmodels.py validation so an endpoint that the
// destination worker would reject is reported instead of silently applied.
struct EndpointCheck {
    bool ok = false;
    QString reason;
    QString id;
    QJsonObject stored; // normalized stored dict
};
EndpointCheck validateEndpoint(const QJsonObject &endpoint);

} // namespace relay::settingsexport
