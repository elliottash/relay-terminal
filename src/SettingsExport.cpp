// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsExport.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace relay::settingsexport {

namespace {

constexpr const char *kFormat = "relay-settings";
constexpr int kVersion = 1;
constexpr const char *kBackupFormat = "relay-settings-backup";

QString configHome()
{
    // Mirrors backend/relay_core/aliases.py: $XDG_CONFIG_HOME or ~/.config.
    const QString env = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!env.isEmpty()) return env;
    return QDir::home().filePath(QStringLiteral(".config"));
}

QString valueText(const QVariant &value)
{
    switch (value.type()) {
    case QVariant::Bool: return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QVariant::StringList: return value.toStringList().join(QChar('|'));
    case QVariant::List: {
        QStringList parts;
        for (const QVariant &v : value.toList()) parts << valueText(v);
        return parts.join(QChar('|'));
    }
    default: return value.toString();
    }
}

bool valuesEqual(const QVariant &a, const QVariant &b)
{
    if (a.type() == b.type()) return a == b;
    return valueText(a) == valueText(b);
}

QJsonValue variantToJson(const QVariant &value)
{
    switch (value.type()) {
    case QVariant::Bool: return value.toBool();
    case QVariant::Int:
    case QVariant::LongLong: return value.toInt();
    case QVariant::Double: return value.toDouble();
    case QVariant::StringList: return QJsonArray::fromStringList(value.toStringList());
    default: return value.toString();
    }
}

QVariant jsonToVariant(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool: return value.toBool();
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (d == qint64(d)) return static_cast<int>(d);
        return d;
    }
    case QJsonValue::Array: {
        QStringList parts;
        for (const QJsonValue &v : value.toArray()) parts << v.toString();
        return parts;
    }
    case QJsonValue::String: return value.toString();
    default: return {};
    }
}

// Reads the local user keybinding overrides as data (preset, program_keys,
// bindings). Absent file = stock relay preset, everything default.
QJsonObject readKeybindingsState()
{
    QJsonObject state;
    state.insert(QStringLiteral("preset"), QStringLiteral("relay"));
    state.insert(QStringLiteral("program_keys"), QStringLiteral("shift-only"));
    state.insert(QStringLiteral("bindings"), QJsonObject());
    QFile file(keybindingsFile());
    if (!file.open(QIODevice::ReadOnly)) return state;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.isEmpty()) return state;
    if (root.contains(QStringLiteral("preset"))) state.insert(QStringLiteral("preset"), root.value(QStringLiteral("preset")));
    if (root.contains(QStringLiteral("program_keys"))) state.insert(QStringLiteral("program_keys"), root.value(QStringLiteral("program_keys")));
    if (root.value(QStringLiteral("bindings")).isObject()) state.insert(QStringLiteral("bindings"), root.value(QStringLiteral("bindings")));
    return state;
}

QJsonArray collectMarkdownCards(const QString &dirPath, QString *error = nullptr)
{
    QJsonArray cards;
    QDir dir(dirPath);
    if (!dir.exists()) return cards;
    const QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : entries) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error && error->isEmpty()) *error = QStringLiteral("Cannot read %1").arg(info.absoluteFilePath());
            continue;
        }
        QJsonObject card;
        card.insert(QStringLiteral("name"), info.completeBaseName());
        card.insert(QStringLiteral("content"), QString::fromUtf8(file.readAll()));
        cards.append(card);
    }
    return cards;
}

bool writeTextFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(content.toUtf8());
    if (content.endsWith(QChar('\n'))) {
        // keep as written
    } else {
        file.write("\n");
    }
    return file.commit();
}

QStringList sortNames(const QJsonObject &obj)
{
    QStringList keys;
    for (auto it = obj.begin(); it != obj.end(); ++it) keys << it.key();
    keys.sort();
    return keys;
}

// Keys whose value is a model identifier or a list of them. Imported values
// are validated against this install and become Attention items otherwise.
bool keyCarriesModelIds(const QString &key)
{
    static const QStringList explicitKeys = {
        QStringLiteral("models/custom"),
        QStringLiteral("models/favorites"),
        QStringLiteral("models/priority"),
        QStringLiteral("models/fallback"),
        QStringLiteral("models/fallbacks"),
        QStringLiteral("models/openrouter_fallback"),
        QStringLiteral("models/sort"),
    };
    if (explicitKeys.contains(key)) return true;
    if (key.startsWith(QStringLiteral("models/profiles/"))) return true;
    if (key.startsWith(QStringLiteral("tiers/"))) return true;
    if (key.startsWith(QStringLiteral("roles/")) && key.endsWith(QStringLiteral("/model"))) return true;
    return false;
}

QStringList modelIdsIn(const QJsonValue &value)
{
    QStringList ids;
    if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) if (!v.toString().isEmpty()) ids << v.toString();
    } else if (!value.toString().isEmpty()) {
        ids << value.toString();
    }
    ids.removeAll(QString());
    return ids;
}

} // namespace

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

QString switchboardRoot() { return QDir(configHome()).filePath(QStringLiteral("relay/switchboard")); }
QString globalAliasesDir() { return QDir(switchboardRoot()).filePath(QStringLiteral("aliases")); }
QString globalMemoriesDir() { return QDir(switchboardRoot()).filePath(QStringLiteral("memory")); }
QString globalInstructionsFile() { return QDir(configHome()).filePath(QStringLiteral("relay/relay.md")); }
QString localModelsFile() { return QDir(configHome()).filePath(QStringLiteral("relay/local-models.json")); }
QString keybindingsFile()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(base).filePath(QStringLiteral("keybindings.json"));
}
QString backupDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("backups"));
}
QString themeFileFor(const QString &themeId)
{
    return QDir(configHome()).filePath(QStringLiteral("relay/themes/%1.toml").arg(themeId));
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

const QList<SettingSpec> &registry()
{
    using S = SettingSpec;
    static const QList<SettingSpec> specs = {
        // General
        {QStringLiteral("input/default"), QStringLiteral("auto"), QStringLiteral("Default model picker")},
        {QStringLiteral("recap/away"), true, QStringLiteral("Show the away recap")},
        {QStringLiteral("files/single_click"), true, QStringLiteral("Open cards with a single click")},
        {QStringLiteral("notifications/desktop"), true, QStringLiteral("Desktop notifications")},
        // Appearance & theme
        {QStringLiteral("appearance/font_size"), 11, QStringLiteral("Interface font size")},
        {QStringLiteral("appearance/pane_usage"), true, QStringLiteral("Show usage meters in pane cards")},
        {QStringLiteral("appearance/auto_dim"), false, QStringLiteral("Dim background panes")},
        {QStringLiteral("appearance/auto_dim_active"), false, QStringLiteral("Dim all panes, brighten focused")},
        {QStringLiteral("appearance/focus_mode"), false, QStringLiteral("Focus mode")},
        {QStringLiteral("theme/name"), QString(), QStringLiteral("Theme")},
        {QStringLiteral("theme/per_tab"), true, QStringLiteral("Per-tab themes")},
        {QStringLiteral("theme/commands_set_default"), true, QStringLiteral("Theme commands set the default theme")},
        {QStringLiteral("theme/new_tab_new_theme"), false, QStringLiteral("New tabs follow the theme")},
        {QStringLiteral("theme/randomize_new_tab"), false, QStringLiteral("Random theme for new tabs")},
        {QStringLiteral("window/native_frame"), false, QStringLiteral("Native window frame")},
        {QStringLiteral("windows/restore"), true, QStringLiteral("Restore tabs on launch")},
        // Terminal
        {QStringLiteral("terminal/copy_on_select"), false, QStringLiteral("Copy on select")},
        {QStringLiteral("terminal/colour_links"), true, QStringLiteral("Colour ANSI links")},
        {QStringLiteral("terminal/shell_integration"), false, QStringLiteral("Shell integration")},
        {QStringLiteral("terminal/persistLocal"), true, QStringLiteral("Persist local sessions")},
        {QStringLiteral("terminal/echo_band"), QStringLiteral("channel"), QStringLiteral("Echo band")},
        // Agent
        {QStringLiteral("agent/app_writes"), true, QStringLiteral("Agent can write through the app")},
        {QStringLiteral("agent/prompt_profile"), QStringLiteral("auto"), QStringLiteral("Prompt profile")},
        {QStringLiteral("agent/effort"), QStringLiteral("high"), QStringLiteral("Reasoning effort")},
        {QStringLiteral("agent/show_tool_output"), false, QStringLiteral("Show tool output")},
        {QStringLiteral("agent/clear_tool_results"), true, QStringLiteral("Clear tool results between steps")},
        {QStringLiteral("agent/compact_over_enabled"), false, QStringLiteral("Compact session over the limit")},
        {QStringLiteral("agent/compact_over_tokens"), 256000, QStringLiteral("Compact-over token budget")},
        {QStringLiteral("agent/cross_pane"), true, QStringLiteral("Cross-pane work")},
        {QStringLiteral("agent/failover"), true, QStringLiteral("Model failover")},
        {QStringLiteral("agent/failover_hosted"), false, QStringLiteral("Fail over to hosted models")},
        {QStringLiteral("agent/max_auto_turns"), 50, QStringLiteral("Max auto turns")},
        {QStringLiteral("agent/max_steps"), 500, QStringLiteral("Max steps per session")},
        {QStringLiteral("agent/max_tool_calls"), 2000, QStringLiteral("Max tool calls per session")},
        {QStringLiteral("agent/panes_flash"), false, QStringLiteral("Flash panes when a step lands")},
        {QStringLiteral("agent/show_thinking"), true, QStringLiteral("Show thinking")},
        {QStringLiteral("agent/stall_timeout_s"), 60, QStringLiteral("Stall timeout (seconds)")},
        {QStringLiteral("agent/first_token_timeout_s"), 0, QStringLiteral("First-token timeout (seconds)")},
        {QStringLiteral("agent/terminal_context"), QStringLiteral("automatic"), QStringLiteral("Terminal context")},
        {QStringLiteral("agent/terminal_handoff"), QStringLiteral("agent"), QStringLiteral("Terminal handoff")},
        {QStringLiteral("agent/audit_requests"), false, QStringLiteral("Audit agent requests")},
        {QStringLiteral("control/default"), QStringLiteral("agent"), QStringLiteral("Default control delegation")},
        {QStringLiteral("board/signals_auto_work"), true, QStringLiteral("Switchboard signals become work automatically")},
        {QStringLiteral("skills/exclude"), QStringList(), QStringLiteral("Excluded skills")},
        {QStringLiteral("skills/exclude_text"), QString(), QStringLiteral("Excluded skills (text)")},
        {QStringLiteral("qa/verification"), QStringLiteral("ask"), QStringLiteral("QA verification policy")},
        // Security
        {QStringLiteral("security/clipboard_write"), false, QStringLiteral("Agent clipboard writes")},
        {QStringLiteral("security/unattended_full_tools"), true, QStringLiteral("Unattended sessions keep full tools")},
        {QStringLiteral("security/command_denylist"), QStringList(), QStringLiteral("Blocked commands")},
        {QStringLiteral("security/secret_patterns"), QStringList(), QStringLiteral("Secret redaction patterns")},
        {QStringLiteral("security/approvals_ask"), QStringList(), QStringLiteral("Capabilities that always ask")},
        // Switchboard & suggestions
        {QStringLiteral("suggestions/next_command"), true, QStringLiteral("Suggest the next command")},
        {QStringLiteral("suggestions/next_prompt"), true, QStringLiteral("Suggest the next prompt")},
        // Voice
        {QStringLiteral("voice/enabled"), true, QStringLiteral("Voice mode")},
        {QStringLiteral("speech/auto_read"), false, QStringLiteral("Read replies aloud")},
        // Privacy
        {QStringLiteral("instructions/project_auto"), true, QStringLiteral("Offer project instructions")},
        {QStringLiteral("sessions/index_guests"), true, QStringLiteral("Index guest sessions")},
        {QStringLiteral("sessions/search_shell"), false, QStringLiteral("Search shell output")},
        {QStringLiteral("memory/import_guests"), true, QStringLiteral("Offer guest memories")},
        // Models / providers
        {QStringLiteral("models/custom"), QStringList(), QStringLiteral("Custom model ids")},
        {QStringLiteral("models/favorites"), QStringList(), QStringLiteral("Favourite models")},
        {QStringLiteral("models/priority"), QStringList(), QStringLiteral("Provider / model priority")},
        {QStringLiteral("models/provider_order"), QStringList(), QStringLiteral("Provider order")},
        {QStringLiteral("models/sort"), QString(), QStringLiteral("Model picker sort")},
        {QStringLiteral("models/profile"), QString(), QStringLiteral("Selected model profile")},
        {QStringLiteral("models/profile_order"), QStringList(), QStringLiteral("Model profile order")},
        {QStringLiteral("models/fallback"), QString(), QStringLiteral("Fallback model")},
        {QStringLiteral("models/fallbacks"), QStringList(), QStringLiteral("Fallback model chain")},
        {QStringLiteral("models/fallback_threshold"), 0, QStringLiteral("Fallback threshold")},
        {QStringLiteral("models/openrouter_fallback"), QStringList(), QStringLiteral("OpenRouter fallbacks")},
    };
    return specs;
}

bool neverExported(const QString &key)
{
    static const QStringList prefixes = {
        QStringLiteral("remote/"),          // pairing identity, pinned devices
        QStringLiteral("url_handler/"),     // machine state
        QStringLiteral("sessions/panes"),   // live pane state
        QStringLiteral("isolation/"),       // per-machine resource limits
        QStringLiteral("geometry"),
    };
    static const QStringList exact = {
        QStringLiteral("models/recent"),       // usage state
        QStringLiteral("models/available"),    // availability state
        QStringLiteral("security/approvals_chosen"), // onboarding state
        QStringLiteral("agent/cli"),
        QStringLiteral("agent/cli_secondary"),
        QStringLiteral("agent/models_json"),
        QStringLiteral("agent/extra_paths"),
        QStringLiteral("agent/plans_dir"),
        QStringLiteral("terminal/login_shell"),
    };
    for (const QString &p : prefixes)
        if (key.startsWith(p)) return true;
    if (exact.contains(key)) return true;
    return key.contains(QStringLiteral("memory_max"));
}

bool exportableKey(const QString &key, QVariant *fallback, QString *label)
{
    if (neverExported(key)) return false;
    for (const SettingSpec &spec : registry()) {
        if (spec.key == key) {
            if (fallback) *fallback = spec.fallback;
            if (label) *label = spec.label;
            return true;
        }
    }
    // Dynamic families.
    static const QStringList roleFields = {QStringLiteral("preset"), QStringLiteral("model"), QStringLiteral("effort")};
    if (key.startsWith(QStringLiteral("roles/"))) {
        const QStringList parts = key.split(QChar('/'));
        if (parts.size() == 3 && roleFields.contains(parts.at(2))) {
            if (fallback) *fallback = QString();
            if (label) *label = QStringLiteral("Role %1: %2").arg(parts.at(1), parts.at(2));
            return true;
        }
        return false;
    }
    if (key.startsWith(QStringLiteral("tiers/")) && key.count(QChar('/')) == 1) {
        if (fallback) *fallback = QStringList();
        if (label) *label = QStringLiteral("Tier %1").arg(key.mid(6));
        return true;
    }
    if (key == QStringLiteral("models/profiles/").left(16) || key.startsWith(QStringLiteral("models/profiles/"))) {
        if (fallback) *fallback = QStringList();
        if (label) *label = QStringLiteral("Model profile %1").arg(key.mid(16));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------

QJsonObject collectBundle(const ExportOptions &options)
{
    QJsonObject bundle;
    bundle.insert(QStringLiteral("format"), kFormat);
    bundle.insert(QStringLiteral("version"), kVersion);
    bundle.insert(QStringLiteral("exported_at"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSettings settings;
    const QStringList allKeys = settings.allKeys();

    // Static registry keys + role / tier / models families, present-and-
    // non-default only. Presence is the non-default convention for every row
    // the Options UI writes ("a reset forgets the key").
    QJsonObject outSettings;
    for (const QString &key : allKeys) {
        QVariant fallback;
        if (!exportableKey(key, &fallback)) continue;
        if (fallback.isValid() && valuesEqual(settings.value(key), fallback)) continue;
        outSettings.insert(key, variantToJson(settings.value(key)));
    }
    bundle.insert(QStringLiteral("settings"), outSettings);

    // Custom hotkeys: the user bindings object from keybindings.json plus the
    // preset and program-keys mode when they differ from stock.
    const QJsonObject kb = readKeybindingsState();
    QJsonObject hotkeys;
    const QJsonObject bindings = kb.value(QStringLiteral("bindings")).toObject();
    if (!bindings.isEmpty()) hotkeys.insert(QStringLiteral("bindings"), bindings);
    if (kb.value(QStringLiteral("preset")).toString() != QStringLiteral("relay"))
        hotkeys.insert(QStringLiteral("preset"), kb.value(QStringLiteral("preset")));
    if (kb.value(QStringLiteral("program_keys")).toString() != QStringLiteral("shift-only"))
        hotkeys.insert(QStringLiteral("program_keys"), kb.value(QStringLiteral("program_keys")));
    bundle.insert(QStringLiteral("hotkeys"), hotkeys);

    // Custom user theme file, when the selected theme is a local one.
    QJsonArray themes;
    const QString themeId = settings.value(QStringLiteral("theme/name")).toString();
    if (!themeId.isEmpty()) {
        const QString themePath = themeFileFor(themeId);
        QFile themeFile(themePath);
        if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonObject theme;
            theme.insert(QStringLiteral("id"), themeId);
            theme.insert(QStringLiteral("content"), QString::fromUtf8(themeFile.readAll()));
            themes.append(theme);
        }
    }
    bundle.insert(QStringLiteral("themes"), themes);

    // Global aliases.
    bundle.insert(QStringLiteral("aliases"), collectMarkdownCards(globalAliasesDir()));

    // Local model endpoints (validated loopback URLs only; credential-ish and
    // availability-ish fields are stripped).
    QJsonArray endpoints;
    QFile localFile(localModelsFile());
    if (localFile.open(QIODevice::ReadOnly)) {
        const QJsonArray stored = QJsonDocument::fromJson(localFile.readAll())
                                      .object().value(QStringLiteral("endpoints")).toArray();
        for (const QJsonValue &v : stored) {
            QJsonObject ep = v.toObject();
            for (auto it = ep.begin(); it != ep.end();) {
                const QString k = it.key();
                const QString lower = k.toLower();
                if (lower.contains(QStringLiteral("key")) || lower.contains(QStringLiteral("token"))
                    || lower.contains(QStringLiteral("secret")) || lower.contains(QStringLiteral("available"))
                    || lower.contains(QStringLiteral("last_")) || lower.contains(QStringLiteral("error")))
                    it = ep.erase(it);
                else
                    ++it;
            }
            if (validateEndpoint(ep).ok) endpoints.append(ep);
        }
    }
    bundle.insert(QStringLiteral("endpoints"), endpoints);

    // Opt-in: global memories and the global instructions file.
    if (options.includeMemories) {
        bundle.insert(QStringLiteral("memories"), collectMarkdownCards(globalMemoriesDir()));
        QFile instructions(globalInstructionsFile());
        if (instructions.open(QIODevice::ReadOnly | QIODevice::Text))
            bundle.insert(QStringLiteral("instructions"), QString::fromUtf8(instructions.readAll()));
    }
    return bundle;
}

bool writeBundleToFile(const QJsonObject &bundle, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(bundle).toJson());
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Cannot save %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QJsonObject readBundle(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != kFormat) {
        if (error) *error = QStringLiteral("%1 is not a Relay settings bundle").arg(path);
        return {};
    }
    if (root.value(QStringLiteral("version")).toInt() != kVersion) {
        if (error) *error = QStringLiteral("Unsupported bundle version in %1").arg(path);
        return {};
    }
    if (!root.value(QStringLiteral("settings")).isObject() && !root.value(QStringLiteral("hotkeys")).isObject()
        && !root.value(QStringLiteral("aliases")).isArray() && !root.value(QStringLiteral("endpoints")).isArray()) {
        if (error) *error = QStringLiteral("%1 carries no preferences").arg(path);
        return {};
    }
    return root;
}

// ---------------------------------------------------------------------------
// Import planning
// ---------------------------------------------------------------------------

int ImportPlan::count(ItemStatus status) const
{
    int n = 0;
    for (const PlanItem &item : items)
        if (item.status == status) ++n;
    return n;
}

bool ImportPlan::hasUnresolvedConflicts() const
{
    for (const PlanItem &item : items)
        if (item.status == ItemStatus::Conflict && item.resolution == Resolution::Unresolved) return true;
    return false;
}

bool ImportPlan::hasUnacceptedAttention() const
{
    for (const PlanItem &item : items)
        if (item.status == ItemStatus::Attention && item.resolution != Resolution::UseImported) return true;
    return false;
}

void ImportPlan::resolveAllConflicts(Resolution resolution)
{
    for (PlanItem &item : items) {
        if (item.status == ItemStatus::Conflict) item.resolution = resolution;
        if (item.status == ItemStatus::Attention && resolution == Resolution::UseImported)
            item.resolution = Resolution::UseImported;
    }
}

ImportPlan planImport(const QJsonObject &bundle, const QString &sourcePath, const Hooks &hooks)
{
    ImportPlan plan;
    plan.sourcePath = sourcePath;
    if (bundle.isEmpty()) {
        plan.error = QStringLiteral("Not a usable Relay settings bundle");
        return plan;
    }

    QSettings settings;
    const QJsonObject kbState = readKeybindingsState();
    const QJsonArray bundleEndpoints = bundle.value(QStringLiteral("endpoints")).toArray();

    // --- plain settings -------------------------------------------------
    const QJsonObject incomingSettings = bundle.value(QStringLiteral("settings")).toObject();
    for (const QString &key : sortNames(incomingSettings)) {
        const QJsonValue incomingRaw = incomingSettings.value(key);
        const QVariant incoming = jsonToVariant(incomingRaw);

        PlanItem item;
        item.kind = ItemKind::Setting;
        item.id = key;
        item.incoming = QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), incomingRaw}};

        QVariant fallback;
        QString label;
        if (!exportableKey(key, &fallback, &label)) {
            item.status = ItemStatus::Skip;
            item.reason = QStringLiteral("Not a preference key on this build");
            plan.items.append(item);
            continue;
        }
        item.label = label;

        const bool present = settings.contains(key);
        const QVariant current = present ? settings.value(key) : fallback;
        item.currentDisplay = (!present || valuesEqual(current, fallback)) && fallback.isValid()
                                  ? QStringLiteral("(default)")
                                  : valueText(current);
        item.incomingDisplay = fallback.isValid() && valuesEqual(incoming, fallback)
                                   ? QStringLiteral("(default)")
                                   : valueText(incoming);

        // Incoming is the default: nothing to bring over a local default, and
        // never overwrite a local customization with the default.
        if (fallback.isValid() && valuesEqual(incoming, fallback)) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("Incoming matches the default; kept current");
            plan.items.append(item);
            continue;
        }
        // Local default (or unset): the incoming non-default fills in.
        const bool currentIsDefault = !present || !fallback.isValid() || valuesEqual(current, fallback);
        if (currentIsDefault || valuesEqual(current, incoming)) {
            item.status = ItemStatus::Auto;
            item.reason = currentIsDefault ? QStringLiteral("Not customized locally")
                                           : QStringLiteral("Identical on both sides");
            // Model ids must be known to this install even when the item would
            // merge automatically; unverifiable ids need attention, never a
            // silent apply.
            if (keyCarriesModelIds(key) && hooks.isKnownModel) {
                const QStringList ids = modelIdsIn(incomingRaw);
                QStringList unknown;
                for (const QString &id : ids)
                    if (!hooks.isKnownModel(id)) unknown << id;
                if (!unknown.isEmpty()) {
                    item.status = ItemStatus::Attention;
                    item.reason = QStringLiteral("Unknown model id on this install: %1").arg(unknown.join(QStringLiteral(", ")));
                    item.resolution = Resolution::Unresolved;
                }
            }
            plan.items.append(item);
            continue;
        }

        item.status = ItemStatus::Conflict;
        // Model ids must be known to this install, or the item needs attention.
        if (keyCarriesModelIds(key) && hooks.isKnownModel) {
            const QStringList ids = modelIdsIn(incomingRaw);
            if (!ids.isEmpty() && !std::all_of(ids.cbegin(), ids.cend(), hooks.isKnownModel)) {
                QStringList unknown;
                for (const QString &id : ids)
                    if (!hooks.isKnownModel(id)) unknown << id;
                item.status = ItemStatus::Attention;
                item.reason = QStringLiteral("Unknown model id on this install: %1").arg(unknown.join(QStringLiteral(", ")));
                item.resolution = Resolution::Unresolved;
            }
        }
        plan.items.append(item);
    }

    // --- hotkeys ----------------------------------------------------------
    const QJsonObject hotkeys = bundle.value(QStringLiteral("hotkeys")).toObject();
    const QJsonObject localBindings = kbState.value(QStringLiteral("bindings")).toObject();
    const QJsonObject incomingBindings = hotkeys.value(QStringLiteral("bindings")).toObject();
    for (const QString &action : sortNames(incomingBindings)) {
        PlanItem item;
        item.kind = ItemKind::Hotkey;
        item.id = action;
        item.label = QStringLiteral("Shortcut: %1").arg(action);
        item.incoming = QJsonObject{{QStringLiteral("action"), action},
                                    {QStringLiteral("keys"), incomingBindings.value(action)}};

        if (hooks.isKnownAction && !hooks.isKnownAction(action)) {
            item.status = ItemStatus::Skip;
            item.reason = QStringLiteral("No such action on this build");
            plan.items.append(item);
            continue;
        }
        const QStringList keys = modelIdsIn(incomingBindings.value(action));
        bool readable = !keys.isEmpty();
        for (const QString &k : keys) {
            const QKeySequence seq(k);
            if (seq.isEmpty() || seq.toString(QKeySequence::NativeText).isEmpty()) readable = false;
        }
        if (!readable) {
            item.status = ItemStatus::Skip;
            item.reason = QStringLiteral("Unreadable shortcut");
            plan.items.append(item);
            continue;
        }
        if (localBindings.contains(action)) {
            const QJsonValue current = localBindings.value(action);
            if (current == incomingBindings.value(action)) {
                item.status = ItemStatus::Auto;
                item.reason = QStringLiteral("Identical on both sides");
            } else {
                item.status = ItemStatus::Conflict;
                item.currentDisplay = QJsonDocument(current.toArray()).toJson(QJsonDocument::Compact);
                item.incomingDisplay = QJsonDocument(incomingBindings.value(action).toArray()).toJson(QJsonDocument::Compact);
            }
        } else {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("Customized only in the bundle");
            item.currentDisplay = QStringLiteral("(default)");
        }
        item.incomingDisplay = QJsonDocument(incomingBindings.value(action).toArray()).toJson(QJsonDocument::Compact);
        plan.items.append(item);
    }
    const struct { const char *field; const char *stock; const char *title; } meta[] = {
        {"preset", "relay", "Keyboard preset"},
        {"program_keys", "shift-only", "Program-keys mode"},
    };
    for (const auto &m : meta) {
        const QJsonValue incoming = hotkeys.value(QLatin1String(m.field));
        if (incoming.isUndefined()) continue;
        PlanItem item;
        item.kind = ItemKind::HotkeyMeta;
        item.id = QLatin1String(m.field);
        item.label = QLatin1String(m.title);
        item.incoming = QJsonObject{{QStringLiteral("field"), QLatin1String(m.field)}, {QStringLiteral("value"), incoming}};
        const QString currentText = kbState.value(QLatin1String(m.field)).toString();
        if (currentText == incoming.toString() || currentText == QLatin1String(m.stock)) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("Customized only in the bundle");
            item.currentDisplay = QStringLiteral("(default)");
        } else {
            item.status = ItemStatus::Conflict;
            item.currentDisplay = currentText;
            item.incomingDisplay = incoming.toString();
        }
        item.incomingDisplay = incoming.toString();
        plan.items.append(item);
    }

    // --- theme file -------------------------------------------------------
    for (const QJsonValue &v : bundle.value(QStringLiteral("themes")).toArray()) {
        const QJsonObject theme = v.toObject();
        const QString id = theme.value(QStringLiteral("id")).toString();
        const QString content = theme.value(QStringLiteral("content")).toString();
        if (id.isEmpty() || id.contains(QChar('/')) || id.contains(QStringLiteral(".."))) continue;
        PlanItem item;
        item.kind = ItemKind::Theme;
        item.id = id;
        item.label = QStringLiteral("Theme file: %1").arg(id);
        item.incoming = theme;
        QFile local(themeFileFor(id));
        if (!local.exists()) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("New on this install");
            item.currentDisplay = QStringLiteral("(absent)");
        } else if (local.open(QIODevice::ReadOnly | QIODevice::Text)
                   && QString::fromUtf8(local.readAll()) == content) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("Identical on both sides");
        } else {
            item.status = ItemStatus::Conflict;
            item.currentDisplay = QStringLiteral("(differs)");
            item.incomingDisplay = QStringLiteral("(imported version)");
        }
        plan.items.append(item);
    }

    // --- aliases / memories -------------------------------------------------
    const struct { const char *field; ItemKind kind; const QString dir; const char *title; } cards[] = {
        {"aliases", ItemKind::Alias, globalAliasesDir(), "Alias"},
        {"memories", ItemKind::Memory, globalMemoriesDir(), "Memory"},
    };
    for (const auto &c : cards) {
        const QJsonArray incomingCards = bundle.value(QLatin1String(c.field)).toArray();
        for (const QJsonValue &v : incomingCards) {
            const QJsonObject card = v.toObject();
            const QString name = card.value(QStringLiteral("name")).toString();
            const QString content = card.value(QStringLiteral("content")).toString();
            if (name.isEmpty() || name.contains(QChar('/')) || name.contains(QStringLiteral(".."))) continue;
            PlanItem item;
            item.kind = c.kind;
            item.id = name;
            item.label = QStringLiteral("%1: %2").arg(QLatin1String(c.title), name);
            item.incoming = card;
            const QString localPath = QDir(c.dir).filePath(name + QStringLiteral(".md"));
            QFile local(localPath);
            if (!local.exists()) {
                item.status = ItemStatus::Auto;
                item.reason = QStringLiteral("New on this install");
                item.currentDisplay = QStringLiteral("(absent)");
            } else if (local.open(QIODevice::ReadOnly | QIODevice::Text)
                       && QString::fromUtf8(local.readAll()) == content) {
                item.status = ItemStatus::Auto;
                item.reason = QStringLiteral("Identical on both sides");
            } else {
                item.status = ItemStatus::Conflict;
                item.currentDisplay = QStringLiteral("(differs)");
                item.incomingDisplay = QStringLiteral("(imported version)");
            }
            plan.items.append(item);
        }
    }

    // --- instructions -------------------------------------------------------
    if (bundle.contains(QStringLiteral("instructions"))) {
        PlanItem item;
        item.kind = ItemKind::Instruction;
        item.id = QStringLiteral("instructions");
        item.label = QStringLiteral("Global instructions (relay.md)");
        QJsonObject payload;
        payload.insert(QStringLiteral("content"), bundle.value(QStringLiteral("instructions")).toString());
        item.incoming = payload;
        QFile local(globalInstructionsFile());
        if (!local.exists()) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("New on this install");
            item.currentDisplay = QStringLiteral("(absent)");
        } else if (local.open(QIODevice::ReadOnly | QIODevice::Text)
                   && QString::fromUtf8(local.readAll()) == payload.value(QStringLiteral("content")).toString()) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("Identical on both sides");
        } else {
            item.status = ItemStatus::Conflict;
            item.currentDisplay = QStringLiteral("(differs)");
            item.incomingDisplay = QStringLiteral("(imported version)");
        }
        plan.items.append(item);
    }

    // --- endpoints ------------------------------------------------------------
    const QJsonArray localEndpoints = [ ] {
        QJsonArray arr;
        QFile file(localModelsFile());
        if (file.open(QIODevice::ReadOnly))
            arr = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("endpoints")).toArray();
        return arr;
    }();
    const auto endpointById = [&localEndpoints](const QString &id) -> QJsonObject {
        for (const QJsonValue &v : localEndpoints)
            if (v.toObject().value(QStringLiteral("id")).toString() == id) return v.toObject();
        return {};
    };
    for (const QJsonValue &v : bundleEndpoints) {
        const QJsonObject stored = v.toObject();
        const EndpointCheck check = validateEndpoint(stored);
        PlanItem item;
        item.kind = ItemKind::Endpoint;
        item.id = check.id;
        item.label = QStringLiteral("Local endpoint: %1").arg(check.id);
        item.incomingDisplay = stored.value(QStringLiteral("base_url")).toString();
        item.incoming = QJsonObject{{QStringLiteral("stored"), stored}};
        if (!check.ok) {
            item.status = ItemStatus::Skip;
            item.reason = check.reason;
            plan.items.append(item);
            continue;
        }
        const QJsonObject current = endpointById(check.id);
        if (current.isEmpty()) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("New on this install");
            item.currentDisplay = QStringLiteral("(absent)");
        } else if (current == stored) {
            item.status = ItemStatus::Auto;
            item.reason = QStringLiteral("Identical on both sides");
        } else {
            item.status = ItemStatus::Conflict;
            item.currentDisplay = current.value(QStringLiteral("base_url")).toString();
        }
        plan.items.append(item);
    }

    return plan;
}

// ---------------------------------------------------------------------------
// Endpoint validation (mirror of backend/relay_core/localmodels.py)
// ---------------------------------------------------------------------------

EndpointCheck validateEndpoint(const QJsonObject &endpoint)
{
    EndpointCheck out;
    const QString id = endpoint.value(QStringLiteral("id")).toString(endpoint.value(QStringLiteral("slug")).toString());
    out.id = id;
    if (id.isEmpty()) {
        out.reason = QStringLiteral("Endpoint has no id");
        return out;
    }
    const QString server = endpoint.value(QStringLiteral("server")).toString();
    static const QStringList servers = {QStringLiteral("llamacpp"), QStringLiteral("ollama"),
                                        QStringLiteral("lmstudio"), QStringLiteral("vllm"),
                                        QStringLiteral("openai-compatible")};
    if (!servers.contains(server)) {
        out.reason = QStringLiteral("Unknown local server type");
        return out;
    }
    const QString baseUrl = endpoint.value(QStringLiteral("base_url")).toString();
    const QUrl url(baseUrl);
    if (!url.isValid() || url.scheme() != QStringLiteral("http")) {
        out.reason = QStringLiteral("Base URL must be a loopback http:// URL");
        return out;
    }
    const QString host = url.host();
    if (host != QStringLiteral("localhost") && host != QStringLiteral("127.0.0.1")
        && host != QStringLiteral("::1") && !host.endsWith(QStringLiteral(".localhost"))) {
        out.reason = QStringLiteral("Base URL must be a loopback http:// URL");
        return out;
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty() || url.hasQuery() || url.hasFragment()) {
        out.reason = QStringLiteral("Endpoint URLs cannot carry credentials, query or fragment");
        return out;
    }
    if (!endpoint.value(QStringLiteral("model")).toString().isEmpty()
        && endpoint.value(QStringLiteral("model")).toString().contains(QChar('\n'))) {
        out.reason = QStringLiteral("Model names must not contain newlines");
        return out;
    }
    out.ok = true;
    out.stored = endpoint;
    return out;
}

// ---------------------------------------------------------------------------
// Apply
// ---------------------------------------------------------------------------

namespace {

constexpr const char *kAbsent = "__absent__";

QJsonObject readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeJsonFileAtomic(const QString &path, const QJsonObject &root)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(QJsonDocument(root).toJson());
    return file.commit();
}

QJsonObject snapshotFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {{QStringLiteral("present"), false}};
    return {{QStringLiteral("present"), true},
            {QStringLiteral("content"), QString::fromUtf8(file.readAll())}};
}

bool restoreFile(const QString &path, const QJsonObject &snapshot, QString *error)
{
    if (!snapshot.value(QStringLiteral("present")).toBool()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error) *error = QStringLiteral("Could not restore (remove) %1").arg(path);
            return false;
        }
        return true;
    }
    if (!writeTextFile(path, snapshot.value(QStringLiteral("content")).toString())) {
        if (error) *error = QStringLiteral("Could not restore %1").arg(path);
        return false;
    }
    return true;
}

} // namespace

ApplyResult applyImport(const ImportPlan &plan)
{
    ApplyResult result;
    if (!plan.error.isEmpty()) {
        result.error = plan.error;
        return result;
    }
    if (plan.hasUnresolvedConflicts()) {
        result.error = QStringLiteral("%1 unresolved conflict(s): nothing was changed")
                           .arg(plan.count(ItemStatus::Conflict));
        return result;
    }
    if (plan.hasUnacceptedAttention()) {
        result.error = QStringLiteral("Import has items needing attention that were not accepted");
        return result;
    }

    // Resolve every item into something that will actually be written.
    QList<PlanItem> effective;
    for (const PlanItem &item : plan.items) {
        if (item.status == ItemStatus::Skip) {
            result.skipped << item.id;
            continue;
        }
        if (item.status == ItemStatus::Conflict && item.resolution == Resolution::KeepCurrent) continue;
        if (item.status == ItemStatus::Attention && item.resolution != Resolution::UseImported) continue;
        effective << item;
    }

    // 1. Snapshot everything this import could touch, then write the backup.
    QSettings settings;
    QJsonObject settingsBackup;
    QJsonObject keybindingsBackup;
    QJsonObject localModelsBackup;
    QJsonObject filesBackup;
    const QString keybindingsPath = keybindingsFile();
    const QString localModelsPath = localModelsFile();

    auto noteSettings = [&settingsBackup, &settings](const QString &key) {
        if (settingsBackup.contains(key)) return;
        QJsonObject snap;
        snap.insert(QStringLiteral("present"), settings.contains(key));
        if (settings.contains(key)) snap.insert(QStringLiteral("value"), variantToJson(settings.value(key)));
        settingsBackup.insert(key, snap);
    };
    auto noteFile = [&filesBackup](const QString &path) {
        if (!filesBackup.contains(path)) filesBackup.insert(path, snapshotFile(path));
    };

    bool touchesKeybindings = false;
    bool touchesLocalModels = false;
    for (const PlanItem &item : effective) {
        switch (item.kind) {
        case ItemKind::Setting: noteSettings(item.id); break;
        case ItemKind::Hotkey:
        case ItemKind::HotkeyMeta: touchesKeybindings = true; break;
        case ItemKind::Endpoint: touchesLocalModels = true; break;
        case ItemKind::Theme: {
            const QString path = themeFileFor(item.id);
            noteFile(path);
            noteSettings(QStringLiteral("theme/name"));
            break;
        }
        case ItemKind::Alias: noteFile(QDir(globalAliasesDir()).filePath(item.id + QStringLiteral(".md"))); break;
        case ItemKind::Memory: noteFile(QDir(globalMemoriesDir()).filePath(item.id + QStringLiteral(".md"))); break;
        case ItemKind::Instruction: noteFile(globalInstructionsFile()); break;
        }
    }
    if (touchesKeybindings) {
        QFile kb(keybindingsPath);
        keybindingsBackup = kb.exists() ? snapshotFile(keybindingsPath) : QJsonObject{{QStringLiteral("present"), false}};
    }
    if (touchesLocalModels) {
        QFile lm(localModelsPath);
        localModelsBackup = lm.exists() ? snapshotFile(localModelsPath) : QJsonObject{{QStringLiteral("present"), false}};
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backupPath = QDir(backupDir()).filePath(QStringLiteral("settings-backup-%1.json").arg(stamp));
    const QJsonObject backup = {
        {QStringLiteral("format"), kBackupFormat},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("settings"), settingsBackup},
        {QStringLiteral("files"), filesBackup},
        {QStringLiteral("keybindings"), keybindingsBackup.isEmpty() ? QJsonValue(kAbsent) : QJsonValue(keybindingsBackup)},
        {QStringLiteral("local_models"), localModelsBackup.isEmpty() ? QJsonValue(kAbsent) : QJsonValue(localModelsBackup)},
    };
    QDir().mkpath(backupDir());
    if ((!settingsBackup.isEmpty() || !filesBackup.isEmpty() || !keybindingsBackup.isEmpty()
         || !localModelsBackup.isEmpty())
        && !writeJsonFileAtomic(backupPath, backup)) {
        result.error = QStringLiteral("Could not write the backup %1; profile untouched").arg(backupPath);
        return result;
    }

    // 2. Apply. Any failure rolls everything back from the backup.
    auto fail = [&](const QString &why) -> ApplyResult {
        QString restoreError;
        restoreBackup(backupPath, &restoreError);
        result.error = why;
        if (!restoreError.isEmpty()) result.error += QStringLiteral(" (rollback: %1)").arg(restoreError);
        result.backupPath = backupPath;
        return result;
    };

    for (const PlanItem &item : effective) {
        if (item.kind != ItemKind::Setting) continue;
        const QJsonObject payload = item.incoming;
        settings.setValue(payload.value(QStringLiteral("key")).toString(item.id),
                          jsonToVariant(payload.value(QStringLiteral("value"))));
        result.applied << item.id;
    }
    if (!settingsBackup.isEmpty()) settings.sync();
    if (settings.status() != QSettings::NoError) return fail(QStringLiteral("Writing settings failed"));

    if (touchesKeybindings) {
        QJsonObject kb = readJsonFile(keybindingsPath);
        if (kb.isEmpty()) {
            kb = QJsonObject{{QStringLiteral("version"), 1},
                             {QStringLiteral("preset"), QStringLiteral("relay")},
                             {QStringLiteral("program_keys"), QStringLiteral("shift-only")},
                             {QStringLiteral("bindings"), QJsonObject()}};
        }
        QJsonObject bindings = kb.value(QStringLiteral("bindings")).toObject();
        for (const PlanItem &item : effective) {
            if (item.kind == ItemKind::Hotkey) {
                bindings.insert(item.incoming.value(QStringLiteral("action")).toString(item.id),
                                item.incoming.value(QStringLiteral("keys")));
            } else if (item.kind == ItemKind::HotkeyMeta) {
                const QString field = item.incoming.value(QStringLiteral("field")).toString();
                const QJsonValue value = item.incoming.value(QStringLiteral("value"));
                const bool stock = (field == QStringLiteral("preset") && value.toString() == QStringLiteral("relay"))
                                   || (field == QStringLiteral("program_keys")
                                       && value.toString() == QStringLiteral("shift-only"));
                if (stock) kb.remove(field);
                else kb.insert(field, value);
            }
        }
        kb.insert(QStringLiteral("bindings"), bindings);
        if (!writeJsonFileAtomic(keybindingsPath, kb))
            return fail(QStringLiteral("Could not write %1").arg(keybindingsPath));
        for (const PlanItem &item : effective)
            if (item.kind == ItemKind::Hotkey || item.kind == ItemKind::HotkeyMeta) result.applied << item.id;
    }

    for (const PlanItem &item : effective) {
        QString path;
        QString content;
        if (item.kind == ItemKind::Theme) {
            path = themeFileFor(item.id);
            content = item.incoming.value(QStringLiteral("content")).toString();
        } else if (item.kind == ItemKind::Alias || item.kind == ItemKind::Memory) {
            path = QDir(item.kind == ItemKind::Alias ? globalAliasesDir() : globalMemoriesDir())
                       .filePath(item.id + QStringLiteral(".md"));
            content = item.incoming.value(QStringLiteral("content")).toString();
        } else if (item.kind == ItemKind::Instruction) {
            path = globalInstructionsFile();
            content = item.incoming.value(QStringLiteral("content")).toString();
        } else {
            continue;
        }
        if (!writeTextFile(path, content)) return fail(QStringLiteral("Could not write %1").arg(path));
        result.applied << item.id;
    }

    if (touchesLocalModels) {
        QJsonObject root = readJsonFile(localModelsPath);
        QJsonArray endpoints = root.value(QStringLiteral("endpoints")).toArray();
        QJsonArray kept;
        for (const QJsonValue &v : endpoints) {
            const QString id = v.toObject().value(QStringLiteral("id")).toString();
            bool replaced = false;
            for (const PlanItem &item : effective) {
                if (item.kind != ItemKind::Endpoint) continue;
                if (item.id == id) {
                    kept.append(item.incoming.value(QStringLiteral("stored")));
                    replaced = true;
                    break;
                }
            }
            if (!replaced) kept.append(v);
        }
        for (const PlanItem &item : effective) {
            if (item.kind != ItemKind::Endpoint) continue;
            const QString id = item.id;
            bool exists = false;
            for (const QJsonValue &v : kept)
                if (v.toObject().value(QStringLiteral("id")).toString() == id) exists = true;
            if (!exists) kept.append(item.incoming.value(QStringLiteral("stored")));
        }
        root.insert(QStringLiteral("version"),
                    root.value(QStringLiteral("version")).toInt(1));
        root.insert(QStringLiteral("endpoints"), kept);
        if (!writeJsonFileAtomic(localModelsPath, root))
            return fail(QStringLiteral("Could not write %1").arg(localModelsPath));
        for (const PlanItem &item : effective)
            if (item.kind == ItemKind::Endpoint) result.applied << item.id;
    }

    result.ok = true;
    result.backupPath = backupPath;
    return result;
}

bool restoreBackup(const QString &backupPath, QString *error)
{
    const QJsonObject backup = readJsonFile(backupPath);
    if (backup.value(QStringLiteral("format")).toString() != kBackupFormat) {
        if (error) *error = QStringLiteral("%1 is not a Relay settings backup").arg(backupPath);
        return false;
    }

    QSettings settings;
    const QJsonObject settingsBackup = backup.value(QStringLiteral("settings")).toObject();
    for (auto it = settingsBackup.begin(); it != settingsBackup.end(); ++it) {
        const QJsonObject snap = it.value().toObject();
        if (snap.value(QStringLiteral("present")).toBool())
            settings.setValue(it.key(), jsonToVariant(snap.value(QStringLiteral("value"))));
        else
            settings.remove(it.key());
    }
    settings.sync();

    const QJsonValue kb = backup.value(QStringLiteral("keybindings"));
    if (!kb.isUndefined() && !kb.toString().isEmpty()) {
        // "" (kAbsent was coerced to a string) means the file did not exist.
        if (kb.isObject()) {
            if (!writeJsonFileAtomic(keybindingsFile(), kb.toObject()) && error)
                *error = QStringLiteral("Could not restore keybindings.json");
        } else if (QFile::exists(keybindingsFile()) && !QFile::remove(keybindingsFile()) && error) {
            *error = QStringLiteral("Could not remove keybindings.json");
        }
    }
    const QJsonValue lm = backup.value(QStringLiteral("local_models"));
    if (!lm.isUndefined() && !lm.toString().isEmpty()) {
        if (lm.isObject()) {
            if (!writeJsonFileAtomic(localModelsFile(), lm.toObject()) && error)
                *error = QStringLiteral("Could not restore local-models.json");
        } else if (QFile::exists(localModelsFile()) && !QFile::remove(localModelsFile()) && error) {
            *error = QStringLiteral("Could not remove local-models.json");
        }
    }
    const QJsonObject files = backup.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
        restoreFile(it.key(), it.value().toObject(), error);
    return error == nullptr || error->isEmpty();
}

} // namespace relay::settingsexport
