// SPDX-License-Identifier: AGPL-3.0-or-later
// Card #05J2: the settings bundle — export allow-list, per-key merge rules,
// conflict planning, staged apply with a timestamped backup, and validation of
// model ids, shortcuts and local model endpoints. Every case starts from a
// cleared profile under an isolated XDG_CONFIG_HOME.

#include <QtTest/QtTest>

#include "SettingsExport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

using namespace relay::settingsexport;

namespace {

// Accepts anything starting with "openrouter/" — a stand-in for this
// installation's model catalog.
bool knownModelHook(const QString &id) { return id.startsWith(QStringLiteral("openrouter/")); }

bool knownActionHook(const QString &id) { return id == QStringLiteral("app.palette") || id == QStringLiteral("agent.new"); }

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(file.readAll());
}

void clearProfile()
{
    QSettings settings;
    settings.clear();
    settings.sync();
    // The whole $XDG_CONFIG_HOME/relay tree (switchboard cards, themes, the
    // local-models file, global instructions) plus the GUI tree that holds
    // keybindings.json and the backups.
    QDir(qEnvironmentVariable("XDG_CONFIG_HOME") + QStringLiteral("/relay")).removeRecursively();
    const QString config = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QFile::remove(QDir(config).filePath(QStringLiteral("keybindings.json")));
    QDir(QDir(config).filePath(QStringLiteral("backups"))).removeRecursively();
}

bool writeText(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(content.toUtf8());
    return file.commit();
}

PlanItem *findItem(ImportPlan &plan, const QString &id)
{
    for (PlanItem &item : plan.items)
        if (item.id == id) return &item;
    return nullptr;
}

QJsonObject bundleWith(const QJsonObject &settings = {})
{
    QJsonObject bundle;
    bundle.insert(QStringLiteral("format"), QStringLiteral("relay-settings"));
    bundle.insert(QStringLiteral("version"), 1);
    bundle.insert(QStringLiteral("settings"), settings);
    return bundle;
}

} // namespace

class SettingsExportTest : public QObject {
    Q_OBJECT

public:
    SettingsExportTest()
    {
        // Isolate the profile before the first QSettings or QStandardPaths use.
        const QString root = QDir::temp().filePath(QStringLiteral("relay-settings-export-test"));
        QDir(root).removeRecursively();
        QDir().mkpath(root);
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(root));
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminal"));
        QCoreApplication::setApplicationName(QStringLiteral("relay"));
    }

private slots:
    void init() { clearProfile(); }

    void allowListCoversFamiliesAndBlocksMachineKeys();
    void exportSkipsDefaultsAndState();
    void roundTripRestoresNonDefaults();
    void incomingOverDefaultMergesAndIdenticalIsAuto();
    void conflictBlocksApplyUntilResolved();
    void unresolvedConflictAppliesNothing();
    void hotkeyRoundTripAndValidation();
    void aliasMergeAndConflict();
    void endpointValidationAndMerge();
    void unknownModelNeedsAttention();
    void backupRestoresPreviousValues();
    void readBundleRejectsWrongFormat();
};

void SettingsExportTest::allowListCoversFamiliesAndBlocksMachineKeys()
{
    QVERIFY(exportableKey(QStringLiteral("agent/effort")));
    QVERIFY(exportableKey(QStringLiteral("security/clipboard_write")));
    QVERIFY(exportableKey(QStringLiteral("theme/name")));
    QVERIFY(exportableKey(QStringLiteral("roles/Deep/model")));
    QVERIFY(exportableKey(QStringLiteral("tiers/Pro")));
    QVERIFY(exportableKey(QStringLiteral("models/priority")));
    QVERIFY(exportableKey(QStringLiteral("models/profiles/cheap")));
    QVERIFY(!exportableKey(QStringLiteral("agent/cli")));            // machine-specific path
    QVERIFY(!exportableKey(QStringLiteral("agent/shell_memory_max"))); // memory limit
    QVERIFY(!exportableKey(QStringLiteral("isolation/agent_memory_max")));
    QVERIFY(!exportableKey(QStringLiteral("remote/pairing")));        // pairing identity
    QVERIFY(!exportableKey(QStringLiteral("models/recent")));         // usage state
    QVERIFY(!exportableKey(QStringLiteral("models/available")));      // availability state
    QVERIFY(!exportableKey(QStringLiteral("nope/made_up")));

    QVariant fallback;
    QString label;
    QVERIFY(exportableKey(QStringLiteral("roles/Deep/model"), &fallback, &label));
    QCOMPARE(fallback.toString(), QString());
    QVERIFY(label.contains(QStringLiteral("Deep")));
}

void SettingsExportTest::exportSkipsDefaultsAndState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("agent/effort"), QStringLiteral("high")); // the default
    settings.setValue(QStringLiteral("agent/show_tool_output"), true);         // a non-default
    settings.setValue(QStringLiteral("models/recent"), QStringList{QStringLiteral("x")});
    settings.setValue(QStringLiteral("remote/pairing"), QStringLiteral("secret"));
    settings.setValue(QStringLiteral("isolation/agent_memory_max"), QStringLiteral("8G"));
    settings.sync();

    const QJsonObject bundle = collectBundle({});
    const QJsonObject out = bundle.value(QStringLiteral("settings")).toObject();
    QVERIFY(!out.contains(QStringLiteral("agent/effort")));
    QVERIFY(!out.contains(QStringLiteral("models/recent")));
    QVERIFY(!out.contains(QStringLiteral("remote/pairing")));
    QVERIFY(!out.contains(QStringLiteral("isolation/agent_memory_max")));
    QCOMPARE(out.value(QStringLiteral("agent/show_tool_output")).toBool(), true);
    QCOMPARE(bundle.value(QStringLiteral("format")).toString(), QStringLiteral("relay-settings"));
    QCOMPARE(bundle.value(QStringLiteral("version")).toInt(), 1);
}

void SettingsExportTest::roundTripRestoresNonDefaults()
{
    QSettings settings;
    settings.setValue(QStringLiteral("agent/effort"), QStringLiteral("max"));
    settings.setValue(QStringLiteral("security/clipboard_write"), true);
    settings.setValue(QStringLiteral("agent/show_tool_output"), true);
    settings.setValue(QStringLiteral("roles/Deep/model"), QStringLiteral("openrouter/x-1"));
    settings.setValue(QStringLiteral("tiers/Free"), QStringList{QStringLiteral("openrouter/x-1"), QStringLiteral("openrouter/x-2")});
    settings.sync();

    writeText(globalAliasesDir() + QStringLiteral("/standup.md"), QStringLiteral("Stand-up notes"));
    QJsonObject endpoint;
    endpoint.insert(QStringLiteral("id"), QStringLiteral("llama-box"));
    endpoint.insert(QStringLiteral("server"), QStringLiteral("llamacpp"));
    endpoint.insert(QStringLiteral("base_url"), QStringLiteral("http://127.0.0.1:8080/v1"));
    endpoint.insert(QStringLiteral("model"), QStringLiteral("qwen"));
    QJsonArray endpoints{endpoint};
    writeText(localModelsFile(), QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                           {QStringLiteral("endpoints"), endpoints}})
                                       .toJson());
    writeText(keybindingsFile(), QStringLiteral("{\"version\":1,\"preset\":\"vscode\",\"bindings\":{\"app.palette\":[\"Ctrl+P\"]}}\n"));
    writeText(themeFileFor(QStringLiteral("sunset")), QStringLiteral("# a custom theme\n"));

    settings.setValue(QStringLiteral("theme/name"), QStringLiteral("sunset"));
    settings.sync();

    Hooks hooks;
    hooks.isKnownModel = knownModelHook;
    hooks.isKnownAction = knownActionHook;
    const QJsonObject bundle = collectBundle({});
    QVERIFY(bundle.value(QStringLiteral("aliases")).toArray().size() == 1);
    QVERIFY(bundle.value(QStringLiteral("endpoints")).toArray().size() == 1);
    QCOMPARE(bundle.value(QStringLiteral("hotkeys")).toObject().value(QStringLiteral("preset")).toString(), QStringLiteral("vscode"));
    QVERIFY(!bundle.value(QStringLiteral("themes")).toArray().isEmpty());

    // A second machine: everything back to defaults.
    clearProfile();
    ImportPlan plan = planImport(bundle, QStringLiteral("bundle.json"), hooks);
    QVERIFY(plan.error.isEmpty());
    QVERIFY(!plan.hasConflicts());
    QVERIFY(!plan.hasUnresolvedConflicts());
    QVERIFY(!plan.hasUnacceptedAttention());
    for (const PlanItem &item : plan.items)
        QCOMPARE(int(item.status), int(ItemStatus::Auto));

    const ApplyResult result = applyImport(plan);
    QVERIFY2(result.ok, qUtf8Printable(result.error));
    QVERIFY(QFile::exists(result.backupPath));

    QSettings after;
    QCOMPARE(after.value(QStringLiteral("agent/effort")).toString(), QStringLiteral("max"));
    QCOMPARE(after.value(QStringLiteral("security/clipboard_write")).toBool(), true);
    QCOMPARE(after.value(QStringLiteral("roles/Deep/model")).toString(), QStringLiteral("openrouter/x-1"));
    QCOMPARE(after.value(QStringLiteral("tiers/Free")).toStringList().size(), 2);
    QCOMPARE(after.value(QStringLiteral("theme/name")).toString(), QStringLiteral("sunset"));
    QVERIFY(QFile::exists(themeFileFor(QStringLiteral("sunset"))));
    QVERIFY(QFile::exists(globalAliasesDir() + QStringLiteral("/standup.md")));
    const QJsonObject localModels = QJsonDocument::fromJson(
                                          QByteArray(readFile(localModelsFile()).toUtf8()))
                                          .object();
    QCOMPARE(localModels.value(QStringLiteral("endpoints")).toArray().size(), 1);
    const QJsonObject kb = QJsonDocument::fromJson(QByteArray(readFile(keybindingsFile()).toUtf8())).object();
    QCOMPARE(kb.value(QStringLiteral("preset")).toString(), QStringLiteral("vscode"));
    QVERIFY(!kb.value(QStringLiteral("bindings")).toObject().value(QStringLiteral("app.palette")).toArray().isEmpty());
}

void SettingsExportTest::incomingOverDefaultMergesAndIdenticalIsAuto()
{
    QSettings settings;
    settings.setValue(QStringLiteral("agent/effort"), QStringLiteral("max"));
    settings.sync();

    QJsonObject in = bundleWith();
    QJsonObject values;
    values.insert(QStringLiteral("agent/effort"), QStringLiteral("max")); // identical
    values.insert(QStringLiteral("agent/show_tool_output"), true);        // incoming non-default over default
    values.insert(QStringLiteral("theme/name"), QString());               // incoming is the default: keep current
    in.insert(QStringLiteral("settings"), values);

    ImportPlan plan = planImport(in, QStringLiteral("b.json"));
    QVERIFY(plan.error.isEmpty());
    PlanItem *identical = findItem(plan, QStringLiteral("agent/effort"));
    QCOMPARE(int(identical->status), int(ItemStatus::Auto));
    PlanItem *fills = findItem(plan, QStringLiteral("agent/show_tool_output"));
    QCOMPARE(int(fills->status), int(ItemStatus::Auto));
    PlanItem *defaultIncoming = findItem(plan, QStringLiteral("theme/name"));
    QCOMPARE(int(defaultIncoming->status), int(ItemStatus::Auto));
    QVERIFY(!plan.hasConflicts());
}

void SettingsExportTest::conflictBlocksApplyUntilResolved()
{
    QSettings settings;
    settings.setValue(QStringLiteral("agent/effort"), QStringLiteral("max"));
    settings.setValue(QStringLiteral("agent/show_tool_output"), true);
    settings.sync();

    QJsonObject values;
    values.insert(QStringLiteral("agent/effort"), QStringLiteral("low"));    // both non-default, differ
    values.insert(QStringLiteral("agent/show_tool_output"), true);           // identical
    ImportPlan plan = planImport(bundleWith(values), QStringLiteral("b.json"));
    PlanItem *conflict = findItem(plan, QStringLiteral("agent/effort"));
    QCOMPARE(int(conflict->status), int(ItemStatus::Conflict));
    QCOMPARE(conflict->currentDisplay, QStringLiteral("max"));
    QCOMPARE(conflict->incomingDisplay, QStringLiteral("low"));
    QVERIFY(plan.hasConflicts());
    QVERIFY(plan.hasUnresolvedConflicts());

    ApplyResult result = applyImport(plan);
    QVERIFY(!result.ok);
    QVERIFY(result.applied.isEmpty());
    QVERIFY(QSettings().value(QStringLiteral("agent/effort")).toString() == QStringLiteral("max"));

    conflict->resolution = Resolution::KeepCurrent;
    QVERIFY(!plan.hasUnresolvedConflicts());
    result = applyImport(plan);
    QVERIFY2(result.ok, qUtf8Printable(result.error));
    QCOMPARE(QSettings().value(QStringLiteral("agent/effort")).toString(), QStringLiteral("max"));
}

void SettingsExportTest::unresolvedConflictAppliesNothing()
{
    QSettings settings;
    settings.setValue(QStringLiteral("agent/effort"), QStringLiteral("max"));
    settings.sync();

    QJsonObject values;
    values.insert(QStringLiteral("agent/effort"), QStringLiteral("low"));
    values.insert(QStringLiteral("agent/show_tool_output"), true); // would merge automatically
    ImportPlan plan = planImport(bundleWith(values), QStringLiteral("b.json"));
    const ApplyResult result = applyImport(plan);
    QVERIFY(!result.ok);
    // Nothing was applied — not even the automatic merge.
    QSettings after;
    QVERIFY(!after.contains(QStringLiteral("agent/show_tool_output")));
    QCOMPARE(after.value(QStringLiteral("agent/effort")).toString(), QStringLiteral("max"));
}

void SettingsExportTest::hotkeyRoundTripAndValidation()
{
    Hooks hooks;
    hooks.isKnownAction = knownActionHook;

    writeText(keybindingsFile(),
              QStringLiteral("{\"version\":1,\"bindings\":{\"app.palette\":[\"Ctrl+P\"],\"ghost.action\":[\"Ctrl+9\"]}}\n"));
    const QJsonObject bundle = collectBundle({});
    const QJsonObject hotkeys = bundle.value(QStringLiteral("hotkeys")).toObject();
    QVERIFY(!hotkeys.value(QStringLiteral("bindings")).toObject().value(QStringLiteral("app.palette")).toArray().isEmpty());

    clearProfile();
    ImportPlan plan = planImport(bundle, QStringLiteral("b.json"), hooks);
    PlanItem *bound = findItem(plan, QStringLiteral("app.palette"));
    QCOMPARE(int(bound->status), int(ItemStatus::Auto));
    PlanItem *ghost = findItem(plan, QStringLiteral("ghost.action"));
    QCOMPARE(int(ghost->status), int(ItemStatus::Skip));
    QVERIFY(ghost->reason.contains(QStringLiteral("action")));
    QVERIFY(applyImport(plan).ok);
    QVERIFY(QSettings().allKeys().isEmpty()); // nothing leaked into QSettings
    const QJsonObject kb = QJsonDocument::fromJson(QByteArray(readFile(keybindingsFile()).toUtf8())).object();
    QVERIFY(!kb.value(QStringLiteral("bindings")).toObject().value(QStringLiteral("app.palette")).toArray().isEmpty());

    // A shortcut no build can parse is skipped, not applied.
    QJsonObject badBindings;
    badBindings.insert(QStringLiteral("agent.new"), QStringLiteral("Not A Real Key"));
    QJsonObject bad = bundle;
    bad.insert(QStringLiteral("hotkeys"), QJsonObject{{QStringLiteral("bindings"), badBindings}});
    clearProfile();
    ImportPlan badPlan = planImport(bad, QStringLiteral("b.json"), hooks);
    PlanItem *skipped = findItem(badPlan, QStringLiteral("agent.new"));
    QCOMPARE(int(skipped->status), int(ItemStatus::Skip));
    QVERIFY(skipped->reason.contains(QStringLiteral("shortcut")));
}

void SettingsExportTest::aliasMergeAndConflict()
{
    writeText(globalAliasesDir() + QStringLiteral("/standup.md"), QStringLiteral("Old text"));
    writeText(globalAliasesDir() + QStringLiteral("/same.md"), QStringLiteral("Same"));
    const QJsonObject bundle = collectBundle({});

    clearProfile();
    writeText(globalAliasesDir() + QStringLiteral("/standup.md"), QStringLiteral("Local text"));
    writeText(globalAliasesDir() + QStringLiteral("/same.md"), QStringLiteral("Same"));

    ImportPlan plan = planImport(bundle, QStringLiteral("b.json"));
    PlanItem *conflict = findItem(plan, QStringLiteral("standup"));
    QCOMPARE(int(conflict->status), int(ItemStatus::Conflict));
    PlanItem *identical = findItem(plan, QStringLiteral("same"));
    QCOMPARE(int(identical->status), int(ItemStatus::Auto));

    conflict->resolution = Resolution::UseImported;
    const ApplyResult result = applyImport(plan);
    QVERIFY2(result.ok, qUtf8Printable(result.error));
    QCOMPARE(readFile(globalAliasesDir() + QStringLiteral("/standup.md")).simplified(), QStringLiteral("Old text"));
}

void SettingsExportTest::endpointValidationAndMerge()
{
    QJsonObject remote;
    remote.insert(QStringLiteral("id"), QStringLiteral("far"));
    remote.insert(QStringLiteral("server"), QStringLiteral("ollama"));
    remote.insert(QStringLiteral("base_url"), QStringLiteral("http://example.com:11434"));
    QJsonObject creds;
    creds.insert(QStringLiteral("id"), QStringLiteral("cred"));
    creds.insert(QStringLiteral("server"), QStringLiteral("ollama"));
    creds.insert(QStringLiteral("base_url"), QStringLiteral("http://user:pw@127.0.0.1:11434"));
    QJsonObject good;
    good.insert(QStringLiteral("id"), QStringLiteral("box"));
    good.insert(QStringLiteral("server"), QStringLiteral("llamacpp"));
    good.insert(QStringLiteral("base_url"), QStringLiteral("http://localhost:8080/v1"));
    good.insert(QStringLiteral("model"), QStringLiteral("qwen"));

    QJsonObject bundle = bundleWith();
    bundle.insert(QStringLiteral("endpoints"), QJsonArray{remote, creds, good});
    ImportPlan plan = planImport(bundle, QStringLiteral("b.json"));
    QCOMPARE(int(findItem(plan, QStringLiteral("far"))->status), int(ItemStatus::Skip));
    QCOMPARE(int(findItem(plan, QStringLiteral("cred"))->status), int(ItemStatus::Skip));
    QCOMPARE(int(findItem(plan, QStringLiteral("box"))->status), int(ItemStatus::Auto));

    const ApplyResult result = applyImport(plan);
    QVERIFY2(result.ok, qUtf8Printable(result.error));
    const QJsonObject stored = QJsonDocument::fromJson(QByteArray(readFile(localModelsFile()).toUtf8())).object();
    QCOMPARE(stored.value(QStringLiteral("endpoints")).toArray().size(), 1);
    QCOMPARE(stored.value(QStringLiteral("endpoints")).toArray().first().toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("box"));
}

void SettingsExportTest::unknownModelNeedsAttention()
{
    Hooks hooks;
    hooks.isKnownModel = knownModelHook;

    QJsonObject values;
    values.insert(QStringLiteral("roles/Deep/model"), QStringLiteral("notknown/gone"));
    ImportPlan plan = planImport(bundleWith(values), QStringLiteral("b.json"), hooks);
    PlanItem *item = findItem(plan, QStringLiteral("roles/Deep/model"));
    QCOMPARE(int(item->status), int(ItemStatus::Attention));
    QVERIFY(item->reason.contains(QStringLiteral("notknown/gone")));
    QVERIFY(plan.hasUnacceptedAttention());
    ApplyResult result = applyImport(plan);
    QVERIFY(!result.ok);
    QVERIFY(!QSettings().contains(QStringLiteral("roles/Deep/model")));

    item->resolution = Resolution::UseImported;
    result = applyImport(plan);
    QVERIFY2(result.ok, qUtf8Printable(result.error));
    QCOMPARE(QSettings().value(QStringLiteral("roles/Deep/model")).toString(), QStringLiteral("notknown/gone"));
}

void SettingsExportTest::backupRestoresPreviousValues()
{
    QSettings settings;
    settings.setValue(QStringLiteral("agent/effort"), QStringLiteral("max"));
    settings.sync();

    QJsonObject values;
    values.insert(QStringLiteral("agent/effort"), QStringLiteral("low"));
    ImportPlan plan = planImport(bundleWith(values), QStringLiteral("b.json"));
    findItem(plan, QStringLiteral("agent/effort"))->resolution = Resolution::UseImported;
    const ApplyResult result = applyImport(plan);
    QVERIFY2(result.ok, qUtf8Printable(result.error));
    QCOMPARE(QSettings().value(QStringLiteral("agent/effort")).toString(), QStringLiteral("low"));

    QString error;
    QVERIFY(restoreBackup(result.backupPath, &error));
    QCOMPARE(QSettings().value(QStringLiteral("agent/effort")).toString(), QStringLiteral("max"));
}

void SettingsExportTest::readBundleRejectsWrongFormat()
{
    QString error;
    QVERIFY(readBundle(QStringLiteral("/nonexistent/bundle.json"), &error).isEmpty());
    QVERIFY(!error.isEmpty());

    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath(QStringLiteral("relay-bad-bundle.json"));
    writeText(path, QStringLiteral("{\"format\":\"other\",\"version\":1}"));
    QVERIFY(readBundle(path, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("not a Relay settings bundle")));
    writeText(path, QStringLiteral("{\"format\":\"relay-settings\",\"version\":99}"));
    QVERIFY(readBundle(path, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("version")));
    QFile::remove(path);
}

// readFile() lives in the top anonymous namespace so every member above sees it.

QTEST_GUILESS_MAIN(SettingsExportTest)
#include "settingsexport_test.moc"
