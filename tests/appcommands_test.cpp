// SPDX-License-Identifier: AGPL-3.0-or-later
// The GUI side of "the agent drives the app" (card #FEJQ, protocol §30): the catalog the window
// sends its workers, one `app_command` executed, and the change log behind Undo.
//
// Everything here is driven through relay::AppCommands with catalogs built in this file, so the
// whole channel is exercised without a window: the values live in a plain struct, and a test can
// see exactly what a writer wrote and what the result said about it.
#include "AppCommands.h"
#include "Notifications.h"
#include "SettingsPane.h"

#include <QApplication>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTest>

using relay::ActionItem;
using relay::AppChange;
using relay::AppCommands;
using relay::SettingRow;
using relay::SettingsSection;

namespace {

struct State {
    bool thinking = true;
    QString theme = QStringLiteral("dark");
    QString plans;
    int steps = 50;
    QStringList ran;
    QStringList opened;
    // The window's panes, by the id a command aims with — a pane's session token, which is what
    // `who` carries for a pane agent (#AG7R group 2). `aimed` records "key@pane" for a run that
    // was aimed at one; `ran` is the unaimed path, the palette's own closure over the focused
    // pane, so which list an action lands in is the whole question these tests ask.
    QStringList panes{QStringLiteral("pane-a"), QStringLiteral("pane-b")};
    QStringList aimed;
    // One pane talking to another (#AG7R group 8): `sent` and `prefilled` record
    // "<pane>←<from>:<text>", which is the whole contract — which pane it reached, who it says it
    // came from, and whether it was submitted or only put in the box. `composerBusy` is the
    // person typing in the target pane and `paneBusy` its agent mid-turn, the two things only the
    // pane itself can know.
    QStringList sent, prefilled, renamed;
    QMap<QString, QString> names;
    bool composerBusy = false, paneBusy = false;
};

QList<SettingsSection> catalog(State *state) {
    QList<SettingsSection> sections;

    SettingsSection general;
    general.id = QStringLiteral("general");
    general.title = QStringLiteral("General");
    {
        SettingRow row;
        row.kind = SettingRow::Toggle;
        row.id = QStringLiteral("option:thinking");
        row.label = QStringLiteral("Show thinking");
        row.detail = QStringLiteral("Stream reasoning above the prompt");
        row.checked = state->thinking;
        row.onToggle = [state](bool on) { state->thinking = on; };
        general.rows << row;
    }
    {
        SettingRow row;
        row.kind = SettingRow::Number;
        row.id = QStringLiteral("option:steps");
        row.label = QStringLiteral("Step limit per turn");
        row.number = state->steps;
        row.minimum = 1;
        row.maximum = 500;
        row.onNumber = [state](int value) { state->steps = value; };
        general.rows << row;
    }
    {
        SettingRow row;
        row.kind = SettingRow::Text;
        row.id = QStringLiteral("option:plans");
        row.label = QStringLiteral("Plans folder");
        row.text = state->plans;
        row.onText = [state](const QString &text) { state->plans = text; };
        general.rows << row;
    }
    sections << general;

    SettingsSection appearance;
    appearance.id = QStringLiteral("appearance");
    appearance.title = QStringLiteral("Appearance");
    {
        SettingRow row;
        row.kind = SettingRow::Choice;
        row.id = QStringLiteral("option:theme");
        row.label = QStringLiteral("Theme");
        row.options = QStringList{QStringLiteral("dark"), QStringLiteral("light")};
        row.optionLabels = QStringList{QStringLiteral("Relay Dark"), QStringLiteral("Relay Light")};
        row.current = state->theme;
        row.onChoose = [state](const QString &id) { state->theme = id; };
        appearance.rows << row;
    }
    sections << appearance;

    SettingsSection models;
    models.id = QStringLiteral("models");
    models.title = QStringLiteral("Models");
    {
        // A key the keyring holds: marked where it is built, never guessed from the id.
        SettingRow row;
        row.kind = SettingRow::Text;
        row.id = QStringLiteral("provider:acme/key");
        row.label = QStringLiteral("acme key");
        row.text = QStringLiteral("sk-do-not-leak");
        row.secret = true;
        row.onText = [state](const QString &text) { state->ran << QStringLiteral("wrote key ") + text; };
        models.rows << row;
    }
    {
        // Test was audited and Remove was not, on one row: since #FRVM both are safe, and Remove
        // (which may ask) runs on the next turn rather than inline.
        SettingRow row;
        row.kind = SettingRow::Buttons;
        row.id = QStringLiteral("provider:acme");
        row.label = QStringLiteral("acme");
        row.buttonTexts = QStringList{QStringLiteral("test"), QStringLiteral("remove")};
        row.agentSafeButtons = QList<int>{0};
        row.onButton = [state](int index) { state->ran << QStringLiteral("button %1").arg(index); };
        models.rows << row;
    }
    {
        // Refused by name (#FRVM): pairing admits a device, which a person does (#W5N2).
        SettingRow row;
        row.kind = SettingRow::Button;
        row.id = QStringLiteral("remote.pair");
        row.label = QStringLiteral("Pair a phone");
        row.buttonText = QStringLiteral("Pair…");
        row.run = [state] { state->ran << QStringLiteral("pair"); };
        models.rows << row;
    }
    sections << models;
    return sections;
}

QList<ActionItem> actions(State *state) {
    QList<ActionItem> items;
    {
        ActionItem item;
        item.key = QStringLiteral("app.settings");
        item.section = QStringLiteral("Relay");
        item.label = QStringLiteral("Options");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("app.settings"); };
        items << item;
    }
    {
        // Opening a pane is safe and closing one is not, on two keys of the same section.
        ActionItem item;
        item.key = QStringLiteral("pane.splitRight");
        item.section = QStringLiteral("Panes");
        item.label = QStringLiteral("New pane to the right");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("pane.splitRight"); };
        items << item;
    }
    {
        ActionItem item;
        item.key = QStringLiteral("pane.close");
        item.section = QStringLiteral("Panes");
        item.label = QStringLiteral("Close pane");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("pane.close"); };
        items << item;
    }
    {
        ActionItem item;
        item.key = QStringLiteral("windows.fresh");
        item.section = QStringLiteral("Relay");
        item.label = QStringLiteral("Start a fresh window set");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("windows.fresh"); };
        items << item;
    }
    {
        // Pane-scoped and safe since #AG7R group 2: the input mode decides where the next thing
        // the person types goes, and it is one click back.
        ActionItem item;
        item.key = QStringLiteral("input.toggle");
        item.section = QStringLiteral("Agent");
        item.label = QStringLiteral("Toggle terminal / agent input");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("input.toggle"); };
        items << item;
    }
    {
        // The Model submenu and one of its children, which is how `model:<id>` reaches the
        // catalog at all: the id is a stored preset, so the children are built from what the
        // pane has and there is no fixed set of keys to name.
        ActionItem item;
        item.key = QStringLiteral("menu:model");
        item.section = QStringLiteral("Agent");
        item.label = QStringLiteral("Model");
        item.children = [state] {
            ActionItem child;
            child.key = QStringLiteral("model:local/bonsai");
            child.section = QStringLiteral("Model");
            child.label = QStringLiteral("Bonsai 2 27B");
            child.run = [state] { state->ran << QStringLiteral("model:local/bonsai"); };
            return QList<ActionItem>{child};
        };
        items << item;
    }
    {
        // Safe, but it writes: re-reading theme.json puts an edit made since the last read into
        // effect, so this one stays behind the writes toggle while opening a pane no longer does
        // (#AG7R group 7).
        ActionItem item;
        item.key = QStringLiteral("theme.reload");
        item.section = QStringLiteral("Relay");
        item.label = QStringLiteral("Reload themes");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("theme.reload"); };
        items << item;
    }
    {
        // #AG7R group 5: allowed by the owner and *not* undoable, which is the case the
        // notification had to grow a sentence for.
        ActionItem item;
        item.key = QStringLiteral("agent.compact");
        item.section = QStringLiteral("Agent");
        item.label = QStringLiteral("Compact conversation");
        item.detail = QStringLiteral("Summarize the conversation so far");
        item.agentSafe = relay::appcommands::actionIsAudited(item.key);
        item.run = [state] { state->ran << QStringLiteral("agent.compact"); };
        items << item;
    }
    return items;
}

// The keybinding registry as AppCommands sees it (src/Keymap.h in the app): a key it knows, and
// nothing else. Two of these are in the safe table and one is not, which is how the three
// answers — ran, `not_agent_safe`, `unknown_action` — are told apart.
QString registryLabel(const QString &key) {
    static const QMap<QString, QString> registered = {
        {QStringLiteral("pane.focusLeft"), QStringLiteral("Focus pane to the left")},
        {QStringLiteral("help.shortcuts"), QStringLiteral("Actions: every action and the keys it answers to")},
        // Registered and *not* in the safe table, which is the third answer. It was
        // `agent.interrupt` until 2026-09-20, when the owner's #AG7R group 5 answer made that one
        // safe — so the example moved to a key that is still off rather than the assertion being
        // relaxed. It was `agent.provider` until 2026-09-21, when card #MDL1 retired that action
        // with the dialog it opened; `keybindings.clearOverrides` is named in `refusedByTheOwner`
        // and so is off because somebody decided, not by omission.
        {QStringLiteral("keybindings.clearOverrides"), QStringLiteral("Clear my keybinding overrides")},
        {QStringLiteral("agent.interrupt"), QStringLiteral("Interrupt the agent")},
    };
    return registered.value(key);
}

QJsonObject rowOf(const QJsonObject &app, const QString &key, const QString &id) {
    for (const auto &value : app.value(key).toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(key == QStringLiteral("options") ? QStringLiteral("id") : QStringLiteral("key")).toString() == id)
            return entry;
    }
    return {};
}

}  // namespace

class AppCommandsTests : public QObject {
    Q_OBJECT

private:
    State state;
    bool writes = true;
    AppCommands app;

    QJsonObject run(const QJsonObject &command, const QString &who = QStringLiteral("pane")) {
        return app.execute(command, who);
    }

    // One `send_prompt` or `prefill_prompt` (#AG7R group 8). Here and not among the slots below:
    // everything in a `private Q_SLOTS:` section is a test case.
    QJsonObject prompt(const QString &command, const QString &pane, const QString &text,
                       const QString &who = QStringLiteral("pane-a")) {
        static int serial = 0;
        return run({{QStringLiteral("id"), QStringLiteral("p%1").arg(++serial)},
                    {QStringLiteral("command"), command},
                    {QStringLiteral("pane"), pane},
                    {QStringLiteral("text"), text}}, who);
    }


private Q_SLOTS:
    void init() {
        state = State();
        writes = true;
        app = AppCommands();
        app.sections = [this] { return catalog(&state); };
        app.actions = [this] { return actions(&state); };
        app.writesEnabled = [this] { return writes; };
        app.registryLabel = [](const QString &key) { return registryLabel(key); };
        app.runRegistryAction = [this](const QString &key) { state.ran << QStringLiteral("registry:") + key; };
        app.paneExists = [this](const QString &id) { return state.panes.contains(id); };
        app.runActionAt = [this](const QString &key, const QString &id) {
            if (!state.panes.contains(id)) return false;
            state.aimed << key + QLatin1Char('@') + id;
            return true;
        };
        app.panes = [this] {
            QJsonArray rows;
            for (const QString &id : state.panes)
                rows.append(QJsonObject{{QStringLiteral("id"), id},
                                        {QStringLiteral("title"), QStringLiteral("~/src ") + id},
                                        {QStringLiteral("focused"), id == state.panes.first()}});
            return rows;
        };
        app.deliverPrompt = [this](const QJsonObject &command, bool *queued, QString *error) {
            const QString pane = command.value(QStringLiteral("pane")).toString();
            if (!state.panes.contains(pane)) { if (error) *error = QStringLiteral("unknown_pane"); return false; }
            const bool send = command.value(QStringLiteral("send")).toBool();
            // A pre-fill never overwrites what the person has typed there (Pane::takeAgentPrompt).
            if (!send && state.composerBusy) { if (error) *error = QStringLiteral("busy"); return false; }
            if (queued) *queued = send && state.paneBusy;
            (send ? state.sent : state.prefilled)
                << pane + QStringLiteral("<") + command.value(QStringLiteral("from")).toString()
                   + QLatin1Char(':') + command.value(QStringLiteral("text")).toString();
            return true;
        };
        app.renameTarget = [this](const QJsonObject &command, QString *previous, QString *error) {
            const QString pane = command.value(QStringLiteral("pane")).toString();
            if (!state.panes.contains(pane)) { if (error) *error = QStringLiteral("unknown_pane"); return false; }
            const QString key = command.value(QStringLiteral("what")).toString() + QLatin1Char(':') + pane;
            if (previous) *previous = state.names.value(key);
            state.names.insert(key, command.value(QStringLiteral("name")).toString());
            state.renamed << key + QLatin1Char('=') + command.value(QStringLiteral("name")).toString();
            return true;
        };
        app.openTarget = [this](const QJsonObject &command, QString *error) {
            const QString target = command.value(QStringLiteral("target")).toString();
            if (target != QStringLiteral("options") && target != QStringLiteral("sessions")) {
                if (error) *error = QStringLiteral("unknown_target");
                return false;
            }
            state.opened << target + QLatin1Char(':') + command.value(QStringLiteral("row")).toString();
            return true;
        };
        relay::SettingsPane::forgetAgentChanges();
        relay::NotificationCenter::instance().clear();
    }

    // ----- the catalog (§30.2) ------------------------------------------------------------------

    void theCatalogCarriesTheTabTheToggleAndEveryRow() {
        const QJsonObject block = app.catalog(QStringLiteral("tab-7"));
        QCOMPARE(block.value(QStringLiteral("tab")).toString(), QStringLiteral("tab-7"));
        QVERIFY(block.value(QStringLiteral("writes_enabled")).toBool());

        const QJsonObject thinking = rowOf(block, QStringLiteral("options"), QStringLiteral("option:thinking"));
        QCOMPARE(thinking.value(QStringLiteral("kind")).toString(), QStringLiteral("toggle"));
        QCOMPARE(thinking.value(QStringLiteral("section")).toString(), QStringLiteral("general"));
        QCOMPARE(thinking.value(QStringLiteral("section_label")).toString(), QStringLiteral("General"));
        QVERIFY(thinking.value(QStringLiteral("settable")).toBool());
        QCOMPARE(thinking.value(QStringLiteral("value")).toBool(), true);

        const QJsonObject theme = rowOf(block, QStringLiteral("options"), QStringLiteral("option:theme"));
        QCOMPARE(theme.value(QStringLiteral("choices")).toArray().size(), 2);
        QCOMPARE(theme.value(QStringLiteral("choices")).toArray().at(1).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("light"));

        const QJsonObject steps = rowOf(block, QStringLiteral("options"), QStringLiteral("option:steps"));
        QCOMPARE(steps.value(QStringLiteral("min")).toInt(), 1);
        QCOMPARE(steps.value(QStringLiteral("max")).toInt(), 500);
    }

    void aSecretRowIsListedWithoutItsValueAndIsNotSettable() {
        const QJsonObject key = rowOf(app.catalog(QStringLiteral("tab-7")), QStringLiteral("options"), QStringLiteral("provider:acme/key"));
        QVERIFY(!key.isEmpty());                                    // it is listed: the agent can say where it is
        QVERIFY(key.value(QStringLiteral("secret")).toBool());
        QVERIFY(!key.value(QStringLiteral("settable")).toBool());
        QVERIFY(!key.contains(QStringLiteral("value")));            // and the key itself never crosses
        QVERIFY(!QJsonDocument(app.catalog(QStringLiteral("tab-7"))).toJson().contains("sk-do-not-leak"));
    }

    void buttonRowsAreListedAsActionsSafeUnlessRefusedByName() {
        const QJsonObject block = app.catalog(QStringLiteral("tab-7"));
        const QJsonObject buttons = rowOf(block, QStringLiteral("options"), QStringLiteral("provider:acme"));
        QCOMPARE(buttons.value(QStringLiteral("kind")).toString(), QStringLiteral("buttons"));
        QVERIFY(!buttons.value(QStringLiteral("settable")).toBool());

        const QString test = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 0);
        const QString remove = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 1);
        QVERIFY(rowOf(block, QStringLiteral("actions"), test).value(QStringLiteral("agent_safe")).toBool());
        QVERIFY(rowOf(block, QStringLiteral("actions"), remove).value(QStringLiteral("agent_safe")).toBool());
        const QString pair = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("remote.pair"));
        QVERIFY(!rowOf(block, QStringLiteral("actions"), pair).isEmpty());
        QVERIFY(!rowOf(block, QStringLiteral("actions"), pair).value(QStringLiteral("agent_safe")).toBool());
    }

    // Owner, 2026-09-24 (#FRVM): "by default agents, should be able to control relay". An action
    // nobody named is safe — including one added after the table was written — and only the
    // refused ones are not.
    void agentSafeIsOnUnlessRefused() {
        const QJsonObject block = app.catalog(QStringLiteral("tab-7"));
        QVERIFY(rowOf(block, QStringLiteral("actions"), QStringLiteral("app.settings")).value(QStringLiteral("agent_safe")).toBool());
        QVERIFY(rowOf(block, QStringLiteral("actions"), QStringLiteral("windows.fresh")).value(QStringLiteral("agent_safe")).toBool());
        QVERIFY(!rowOf(block, QStringLiteral("actions"), QStringLiteral("pane.close")).value(QStringLiteral("agent_safe")).toBool());
        QVERIFY(relay::appcommands::actionIsAgentSafe(QStringLiteral("some.actionAddedTomorrow")));
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("some.actionAddedTomorrow")));
    }

    void theToggleRidesOnTheCatalog() {
        writes = false;
        QVERIFY(!app.catalog(QStringLiteral("tab-7")).value(QStringLiteral("writes_enabled")).toBool());
    }

    // ----- set_option (§30.3) -------------------------------------------------------------------

    void settingARowInvokesTheWriterAndAnswersWithBeforeAndAfter() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("set_option")},
                                        {QStringLiteral("row"), QStringLiteral("option:thinking")},
                                        {QStringLiteral("value"), false}});
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("id")).toString(), QStringLiteral("r1"));
        QCOMPARE(state.thinking, false);
        QCOMPARE(result.value(QStringLiteral("previous")).toBool(), true);
        QCOMPARE(result.value(QStringLiteral("value")).toBool(), false);
        QVERIFY(!result.value(QStringLiteral("change_id")).toString().isEmpty());

        // The log has it, and so has the notification centre, with the way back on it.
        QCOMPARE(app.changes().size(), 1);
        const AppChange change = app.changes().first();
        QCOMPARE(change.key, QStringLiteral("option:thinking"));
        QCOMPARE(change.who, QStringLiteral("pane"));
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().title, QStringLiteral("Agent changed Show thinking"));
        QCOMPARE(notes.first().body, QStringLiteral("on → off"));
        QCOMPARE(notes.first().actionLabel, QStringLiteral("Undo"));
    }

    void theValueComesBackFromTheRowNotFromTheRequest() {
        // The writer normalises what it was given; the result must say what the setting is now.
        app.sections = [this] {
            QList<SettingsSection> sections = catalog(&state);
            for (SettingsSection &section : sections)
                for (SettingRow &row : section.rows)
                    if (row.id == QStringLiteral("option:plans"))
                        row.onText = [this](const QString &text) { state.plans = text.trimmed().toLower(); };
            return sections;
        };
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("set_option")},
                                        {QStringLiteral("row"), QStringLiteral("option:plans")},
                                        {QStringLiteral("value"), QStringLiteral("  /Tmp/Plans  ")}});
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("value")).toString(), QStringLiteral("/tmp/plans"));
    }

    void aSecretRowANonValueRowAndAnUnknownRowAreRefused() {
        const QJsonObject secret = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("set_option")},
                                        {QStringLiteral("row"), QStringLiteral("provider:acme/key")},
                                        {QStringLiteral("value"), QStringLiteral("sk-new")}});
        QVERIFY(!secret.value(QStringLiteral("ok")).toBool());
        QCOMPARE(secret.value(QStringLiteral("error")).toString(), QStringLiteral("secret"));
        QVERIFY(state.ran.isEmpty());

        const QJsonObject button = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                        {QStringLiteral("command"), QStringLiteral("set_option")},
                                        {QStringLiteral("row"), QStringLiteral("provider:acme")},
                                        {QStringLiteral("value"), true}});
        QCOMPARE(button.value(QStringLiteral("error")).toString(), QStringLiteral("not_settable"));

        const QJsonObject gone = run({{QStringLiteral("id"), QStringLiteral("r3")},
                                      {QStringLiteral("command"), QStringLiteral("set_option")},
                                      {QStringLiteral("row"), QStringLiteral("option:vanished")},
                                      {QStringLiteral("value"), true}});
        QCOMPARE(gone.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_row"));
        QVERIFY(app.changes().isEmpty());
    }

    void aValueThatDoesNotFitTheRowIsRefused() {
        const QStringList bad = {QStringLiteral("option:thinking"), QStringLiteral("option:theme"),
                                 QStringLiteral("option:steps")};
        const QList<QJsonValue> values = {QJsonValue(QStringLiteral("yes")), QJsonValue(QStringLiteral("purple")),
                                          QJsonValue(9000)};
        for (int i = 0; i < bad.size(); ++i) {
            const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r")},
                                            {QStringLiteral("command"), QStringLiteral("set_option")},
                                            {QStringLiteral("row"), bad.at(i)},
                                            {QStringLiteral("value"), values.at(i)}});
            QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("invalid_value"));
        }
        QCOMPARE(state.thinking, true);
        QCOMPARE(state.theme, QStringLiteral("dark"));
        QCOMPARE(state.steps, 50);
    }

    void writesOffRefusesSettingAndRunningButNotOpening() {
        writes = false;
        const QJsonObject set = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                     {QStringLiteral("command"), QStringLiteral("set_option")},
                                     {QStringLiteral("row"), QStringLiteral("option:thinking")},
                                     {QStringLiteral("value"), false}});
        QCOMPARE(set.value(QStringLiteral("error")).toString(), QStringLiteral("writes_disabled"));
        QCOMPARE(state.thinking, true);

        const QJsonObject ran = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                     {QStringLiteral("command"), QStringLiteral("run_action")},
                                     {QStringLiteral("key"), QStringLiteral("theme.reload")}});
        QCOMPARE(ran.value(QStringLiteral("error")).toString(), QStringLiteral("writes_disabled"));

        // Opening a pane changes nothing, so it is offered whatever the toggle says (§30.4).
        const QJsonObject opened = run({{QStringLiteral("id"), QStringLiteral("r3")},
                                        {QStringLiteral("command"), QStringLiteral("open")},
                                        {QStringLiteral("target"), QStringLiteral("options")},
                                        {QStringLiteral("row"), QStringLiteral("option:thinking")}});
        QVERIFY(opened.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.opened, QStringList{QStringLiteral("options:option:thinking")});
    }

    // ----- run_action ---------------------------------------------------------------------------

    void onlyAgentSafeActionsRun() {
        const QJsonObject safe = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                      {QStringLiteral("command"), QStringLiteral("run_action")},
                                      {QStringLiteral("key"), QStringLiteral("app.settings")}});
        QVERIFY(safe.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("app.settings")});
        // An action leaves a log entry and a notification, and offers no Undo.
        QCOMPARE(app.changes().size(), 1);
        QVERIFY(app.changes().first().action);
        QCOMPARE(relay::NotificationCenter::instance().entries().first().title, QStringLiteral("Agent ran Options"));
        QVERIFY(relay::NotificationCenter::instance().entries().first().actionLabel.isEmpty());

        const QJsonObject unsafe = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("pane.close")}});
        QCOMPARE(unsafe.value(QStringLiteral("error")).toString(), QStringLiteral("not_agent_safe"));

        const QJsonObject missing = run({{QStringLiteral("id"), QStringLiteral("r3")},
                                         {QStringLiteral("command"), QStringLiteral("run_action")},
                                         {QStringLiteral("key"), QStringLiteral("no.such.action")}});
        QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_action"));
        QCOMPARE(state.ran.size(), 1);
    }

    // #AG7R group 5, owner 2026-09-20: "dont let the agent do 1, 4, 6, 7. others are ok".
    // The four he named stay off; the rest of the list goes on, most of it not undoable at all.
    void theOwnersGroupFiveWideningIsOnAndHisFourRefusalsAreNot() {
        for (const QString &key : {QStringLiteral("tab.new"), QStringLiteral("window.new"),
                                   QStringLiteral("tab.moveToNewWindow"), QStringLiteral("pane.moveToNewTab"),
                                   QStringLiteral("pane.moveLeft"), QStringLiteral("pane.moveRight"),
                                   QStringLiteral("pane.moveUp"), QStringLiteral("pane.moveDown"),
                                   QStringLiteral("agent.stop"), QStringLiteral("agent.interrupt"),
                                   QStringLiteral("agent.continue"), QStringLiteral("agent.recap"),
                                   QStringLiteral("agent.newChat"), QStringLiteral("agent.compact"),
                                   QStringLiteral("agent.clearQueue"), QStringLiteral("agent.resumeQueue"),
                                   QStringLiteral("agent.stopAllSubagents"), QStringLiteral("terminal.interrupt"),
                                   QStringLiteral("terminal.clear"), QStringLiteral("terminal.native"),
                                   QStringLiteral("pane.restartShell"), QStringLiteral("control.prompt"),
                                   QStringLiteral("pane.share"), QStringLiteral("pane.sharing"),
                                   QStringLiteral("app.update"), QStringLiteral("project.detach"),
                                   QStringLiteral("hints.reset"), QStringLiteral("conversations.rebuild"),
                                   QStringLiteral("helper.ask"), QStringLiteral("ssh.splitSameHost")}) {
            QVERIFY2(relay::appcommands::actionIsAgentSafe(key), qPrintable(key));
            // None of it may leak into the read set: each one changes something, so the writes
            // toggle has to keep gating it (group 7).
            QVERIFY2(!relay::appcommands::actionIsRead(key), qPrintable(key));
        }
        // 1, 4, 6 and 7. Named in the table rather than merely absent, so that adding one back is
        // a contradiction a reader has to resolve.
        for (const QString &key : {QStringLiteral("voice.toggle"),
                                   QStringLiteral("control.human"), QStringLiteral("control.program.agent"),
                                   QStringLiteral("control.program.human"), QStringLiteral("program.delegate"),
                                   QStringLiteral("keybindings.clearOverrides"),
                                   QStringLiteral("history.clear")})
            QVERIFY2(!relay::appcommands::actionIsAgentSafe(key), qPrintable(key));
    }

    // `windows.fresh` enters a nested event loop behind a dialog (#AG7R group 4). Run inline, the
    // answer would never be sent before §30.3's deadline; since #FRVM an unaudited action runs on
    // the next turn instead, so the answer comes back first and the dialog is the person's. The
    // action has not run when the answer arrives, and has once the event loop turns.
    void anUnauditedActionRunsAfterTheAnswer() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("windows.fresh")}});
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(result.value(QStringLiteral("deferred")).toBool());
        QVERIFY(state.ran.isEmpty());
        QCoreApplication::processEvents();
        QCOMPARE(state.ran, QStringList{QStringLiteral("windows.fresh")});
        // An audited one still runs inline, and says nothing about deferring.
        const QJsonObject inlineRun = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                           {QStringLiteral("command"), QStringLiteral("run_action")},
                                           {QStringLiteral("key"), QStringLiteral("app.settings")}});
        QVERIFY(!inlineRun.contains(QStringLiteral("deferred")));
        QCOMPARE(state.ran.last(), QStringLiteral("app.settings"));
        // `pane.close` stays off: it closes the focused pane, not one an agent can aim.
        QVERIFY(!relay::appcommands::actionIsAgentSafe(QStringLiteral("pane.close")));
    }

    // Decision 2 promised every agent action was undoable in one click, so "Agent ran X" was
    // enough. Group 5 ended that, so the note has to say what it cost instead of describing the
    // button (#AG7R group 5).
    void aDestructiveActionSaysInTheNotificationWhatItCost() {
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r1")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), QStringLiteral("agent.compact")}})
                    .value(QStringLiteral("ok")).toBool());
        const auto note = relay::NotificationCenter::instance().entries().first();
        QCOMPARE(note.title, QStringLiteral("Agent ran Compact conversation"));
        QVERIFY2(note.body.contains(QStringLiteral("is gone")), qPrintable(note.body));
        // and not the palette blurb, which describes the button rather than the loss
        QVERIFY(!note.body.contains(QStringLiteral("Summarize the conversation")));
    }

    // The owner's report, 2026-09-20: "the sessions helper can't open panes because it says it's
    // unsafe". A split opens an empty pane and the × in its header takes it back in one click, so
    // it is on the reversible side of decision 2's line; closing a pane, which takes away what the
    // pane was holding, is not.
    void openingAPaneRunsAndClosingOneDoesNot() {
        for (const QString &key : {QStringLiteral("pane.splitRight"), QStringLiteral("pane.splitDown"),
                                   QStringLiteral("pane.splitLeft"), QStringLiteral("pane.splitUp"),
                                   QStringLiteral("closed.restore")})
            QVERIFY2(relay::appcommands::actionIsAgentSafe(key), qPrintable(key));
        QVERIFY(!relay::appcommands::actionIsAgentSafe(QStringLiteral("pane.close")));

        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r1")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), QStringLiteral("pane.splitRight")}})
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("pane.splitRight")});
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r2")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("pane.close")}}).value(QStringLiteral("error")).toString(),
                 QStringLiteral("not_agent_safe"));
        QCOMPARE(state.ran.size(), 1);
    }

    // ----- the registry fallback (#AG7R group 1) --------------------------------------------------
    //
    // Twelve of the 33 keys the safe table named had no palette row — they live only in the
    // keybinding registry — so an agent that read §30.2, believed it might move the focus between
    // panes, and tried it was told `unknown_action`: "Relay has no action 'pane.focusLeft'". The
    // table is the policy, so the executor now falls back to the registry the keyboard itself
    // dispatches through.
    void aSafeKeyWithNoPaletteRowRunsThroughTheRegistry() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("pane.focusLeft")}});
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(state.ran, QStringList{QStringLiteral("registry:pane.focusLeft")});
        // It is a change like any other action, logged under the registry's own description.
        QCOMPARE(app.changes().size(), 1);
        QCOMPARE(app.changes().first().label, QStringLiteral("Focus pane to the left"));
    }

    // …and the policy still answers first. A registered key the table does not name is refused on
    // the policy rather than denied as missing — the card's own note: "which would make them
    // reachable and then refused on the policy, which is the right answer".
    void aRegistryKeyOutsideTheTableIsRefusedOnThePolicyNotAsUnknown() {
        writes = true;
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("keybindings.clearOverrides")}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("not_agent_safe"));
        // Neither the catalog nor the registry: still nothing to run.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r2")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("pane.focusSideways")}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_action"));
        QVERIFY(state.ran.isEmpty());
    }

    // The same keys in the catalog, so `app_action_list` stops contradicting the policy table on
    // the same key in the same answer (#GMCF listed them under `Shortcuts` with agent_safe false).
    void theCatalogListsTheRegistryOnlySafeKeysAsSafe() {
        const QJsonObject block = app.catalog(QStringLiteral("tab-7"));
        const QJsonObject focus = rowOf(block, QStringLiteral("actions"), QStringLiteral("pane.focusLeft"));
        QVERIFY(!focus.isEmpty());
        QVERIFY(focus.value(QStringLiteral("agent_safe")).toBool());
        QCOMPARE(focus.value(QStringLiteral("section")).toString(), QStringLiteral("Shortcuts"));
        QCOMPARE(focus.value(QStringLiteral("label")).toString(), QStringLiteral("Focus pane to the left"));
        QVERIFY(rowOf(block, QStringLiteral("actions"), QStringLiteral("help.shortcuts"))
                    .value(QStringLiteral("agent_safe")).toBool());
        // Refused, and listed as refused (#FRVM): the worker sees the whole registry and treats
        // every key the catalog does not name as runnable, so a refusal has to be said here.
        const QJsonObject wipe = rowOf(block, QStringLiteral("actions"), QStringLiteral("keybindings.clearOverrides"));
        QVERIFY(!wipe.isEmpty());
        QVERIFY(!wipe.value(QStringLiteral("agent_safe")).toBool());
        // …and one the owner *did* allow in group 5 is advertised, from the registry, like the
        // focus keys above it — the same fallback, the other side of the policy.
        QVERIFY(rowOf(block, QStringLiteral("actions"), QStringLiteral("agent.interrupt"))
                    .value(QStringLiteral("agent_safe")).toBool());
        // A safe key with a palette row is listed once, from the palette, not twice.
        int settings = 0;
        for (const auto &value : block.value(QStringLiteral("actions")).toArray())
            if (value.toObject().value(QStringLiteral("key")).toString() == QStringLiteral("app.settings")) ++settings;
        QCOMPARE(settings, 1);
    }

    // `palette.agent` was in the table and nowhere else in the tree: src/Keymap.h migrates the key
    // to `palette.open`, so the policy was naming a key that could never arrive. (The audited table
    // since #FRVM; the policy itself no longer names keys it allows.)
    void theRetiredPaletteAgentKeyIsGone() {
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("palette.agent")));
        QVERIFY(!relay::appcommands::agentSafeActionKeys().contains(QStringLiteral("palette.agent")));
    }

    // ----- group 3: reversible, in the catalog, never named --------------------------------------
    // Owner, 2026-09-20 on #AG7R: "groups 1-3 all yes". The window-scoped half landed first; the
    // pane-scoped half waited for group 2, because a picker that cannot say *which* pane it means
    // lands on whichever one the person is looking at.
    void theWindowScopedGroupThreeKeysAreSafe() {
        for (const QString &key : {QStringLiteral("review.open"), QStringLiteral("tests.open"), QStringLiteral("app.about"),
                                   QStringLiteral("logs.open"), QStringLiteral("theme.folder"),
                                   QStringLiteral("agent.screenshotPane"), QStringLiteral("pane.equalize"),
                                   QStringLiteral("menu:closed")})
            QVERIFY2(relay::appcommands::actionIsAgentSafe(key), qPrintable(key));
    }

    // …and the pane-scoped half, now that a command can be aimed. Each writes — the model and the
    // effort are saved for the pane, the mode decides where the next thing typed goes — so each is
    // behind the writes toggle rather than in the read set (group 7).
    void thePaneScopedGroupThreeKeysAreSafeAndWrite() {
        for (const QString &key : {QStringLiteral("menu:model"), QStringLiteral("model:local/bonsai"),
                                   QStringLiteral("menu:effort"), QStringLiteral("effort:high"),
                                   QStringLiteral("menu:mode"), QStringLiteral("input.modeAuto"),
                                   QStringLiteral("input.modeTerminal"), QStringLiteral("input.modeAgent"),
                                   QStringLiteral("input.toggle"), QStringLiteral("agent.planToggle")}) {
            QVERIFY2(relay::appcommands::actionIsAgentSafe(key), qPrintable(key));
            QVERIFY2(!relay::appcommands::actionIsRead(key), qPrintable(key));
            QVERIFY2(relay::appcommands::actionIsPaneScoped(key), qPrintable(key));
        }
        // The window-scoped keys are not aimed at anything: a split anchors on the leaf the
        // person is looking at, and moving the focus is about the window.
        for (const QString &key : {QStringLiteral("pane.splitRight"), QStringLiteral("tab.next"),
                                   QStringLiteral("app.settings"), QStringLiteral("closed:2")})
            QVERIFY2(!relay::appcommands::actionIsPaneScoped(key), qPrintable(key));
    }

    // `model:<id>` and `effort:<level>` carry a suffix nothing can enumerate — a stored preset,
    // and whatever levels the pane's provider offers — so they are matched by prefix, exactly as
    // `closed:<id>` is. A near miss must not become audited by looking like one (since #FRVM every
    // key is safe unless refused; the audit decides only whether it runs inline).
    void aPickedModelMatchesByPrefixAndANearMissDoesNot() {
        QVERIFY(relay::appcommands::actionIsAgentSafe(QStringLiteral("model:local/bonsai")));
        QVERIFY(relay::appcommands::actionIsAgentSafe(QStringLiteral("model:anything-at-all")));
        QVERIFY(relay::appcommands::actionIsAgentSafe(QStringLiteral("effort:high")));
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("model:")));      // no id at all
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("effort:")));
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("models:local")));  // not the prefix
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("model.pick")));
        // Being named by the policy is not being in the catalog: the id has to be one the pane
        // really has, and `model:anything-at-all` is in no submenu, so it is still unknown.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("model:anything-at-all")}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_action"));
        QVERIFY(state.ran.isEmpty());
        QVERIFY(state.aimed.isEmpty());
    }

    // ----- group 2: which pane a command lands on -------------------------------------------------
    //
    // `RelayWindow::runAction()` opened with `Pane *pane = m_active` and `app_command` carried no
    // pane, so a pane-scoped action asked for by the agent in tab 2 acted on the pane the person
    // happened to be sitting in. The three rules are below, and the first is the one that must ask
    // nothing of the model.

    // A pane agent's command with no `pane`: `who` is its pane's session token, so its own pane is
    // resolved without it naming itself.
    void aPaneAgentsCommandWithNoPaneLandsOnItsOwnPane() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("input.toggle")}},
                                       QStringLiteral("pane-b"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(state.aimed, QStringList{QStringLiteral("input.toggle@pane-b")});
        QVERIFY(state.ran.isEmpty());                       // never the focused pane's closure
        QCOMPARE(result.value(QStringLiteral("pane")).toString(), QStringLiteral("pane-b"));
    }

    // The helper has no pane of its own — `who` is the word "helper" — so its commands go on
    // landing on the pane the person is focused on. Said out loud because it is the case that
    // surprises people.
    void aHelpersCommandWithNoPaneLandsOnTheFocusedPane() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("input.toggle")}},
                                       QStringLiteral("helper"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("input.toggle")});
        QVERIFY(state.aimed.isEmpty());
        QVERIFY(!result.contains(QStringLiteral("pane")));
    }

    // An explicit `pane` names any pane of the window — how one agent reaches another's pane.
    void anExplicitPaneIsWhereItLands() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("model:local/bonsai")},
                                        {QStringLiteral("pane"), QStringLiteral("pane-a")}},
                                       QStringLiteral("pane-b"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(state.aimed, QStringList{QStringLiteral("model:local/bonsai@pane-a")});
        QVERIFY(state.ran.isEmpty());
        QCOMPARE(result.value(QStringLiteral("pane")).toString(), QStringLiteral("pane-a"));
        // The helper may aim too: it has no pane of its own, and that is the only difference.
        run({{QStringLiteral("id"), QStringLiteral("r2")},
             {QStringLiteral("command"), QStringLiteral("run_action")},
             {QStringLiteral("key"), QStringLiteral("input.toggle")},
             {QStringLiteral("pane"), QStringLiteral("pane-b")}}, QStringLiteral("helper"));
        QCOMPARE(state.aimed.size(), 2);
        QCOMPARE(state.aimed.last(), QStringLiteral("input.toggle@pane-b"));
    }

    // A pane that was closed between the agent reading the list and asking. It is a refusal with
    // its own word, never a silent landing on somebody else's pane — which is the fault this
    // whole change is about.
    void aPaneThatHasGoneAnswersUnknownPane() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("run_action")},
                                        {QStringLiteral("key"), QStringLiteral("input.toggle")},
                                        {QStringLiteral("pane"), QStringLiteral("pane-gone")}},
                                       QStringLiteral("pane-b"));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_pane"));
        QVERIFY(result.value(QStringLiteral("message")).toString().contains(QStringLiteral("pane-gone")));
        QVERIFY(state.ran.isEmpty());
        QVERIFY(state.aimed.isEmpty());
        // …and it is refused before the policy is even consulted, so an unsafe key aimed at a
        // dead pane says the pane is gone rather than teaching the agent about the policy.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r2")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("pane.close")},
                      {QStringLiteral("pane"), QStringLiteral("pane-gone")}}, QStringLiteral("pane-b"))
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_pane"));
    }

    // A window-scoped action ignores the aim: a split anchors on the leaf the person is looking
    // at, and so do the focus moves and the tab keys. Aiming those would mean a pane appearing in
    // a tab nobody is watching (see runActionNow's comment).
    void aWindowScopedActionRunsWhereItAlwaysDid() {
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r1")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), QStringLiteral("pane.splitRight")},
                     {QStringLiteral("pane"), QStringLiteral("pane-b")}}, QStringLiteral("pane-a"))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("pane.splitRight")});
        QVERIFY(state.aimed.isEmpty());
        // A `row:` button is the app's, not a pane's, and runs its own closure whoever asked.
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r2")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), relay::appcommands::rowActionKey(
                                                 QStringLiteral("models"), QStringLiteral("provider:acme"), 0)}},
                    QStringLiteral("pane-b")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran.last(), QStringLiteral("button 0"));
        QVERIFY(state.aimed.isEmpty());
    }

    // A window with no pane lookup at all — every existing caller of AppCommands, and the tests
    // above this section, where `who` is not a pane token — runs exactly as it did before there
    // was a `pane` field.
    void withNoPaneLookupEveryActionRunsAsItAlwaysDid() {
        app.paneExists = nullptr;
        app.runActionAt = nullptr;
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r1")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), QStringLiteral("input.toggle")}},
                    QStringLiteral("pane-b")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("input.toggle")});
        QVERIFY(state.aimed.isEmpty());
        // A named pane cannot be checked, so it is refused rather than silently ignored.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r2")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("input.toggle")},
                      {QStringLiteral("pane"), QStringLiteral("pane-a")}}, QStringLiteral("pane-b"))
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_pane"));
    }

    // `list_panes`: where an agent reads the id of a pane that is not its own. It changes
    // nothing, so it is answered whatever the writes toggle says (§30.4), and the asking agent's
    // own pane is marked so it never has to guess.
    void listPanesNamesEveryPaneAndMarksTheAgentsOwn() {
        writes = false;
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("list_panes")}},
                                       QStringLiteral("pane-b"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        const QJsonArray panes = result.value(QStringLiteral("panes")).toArray();
        QCOMPARE(panes.size(), 2);
        QCOMPARE(panes.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("pane-a"));
        QVERIFY(!panes.at(0).toObject().contains(QStringLiteral("you")));
        QVERIFY(panes.at(1).toObject().value(QStringLiteral("you")).toBool());
        // The helper is in no pane, so none of them is marked as its own.
        for (const auto &value : run({{QStringLiteral("id"), QStringLiteral("r2")},
                                      {QStringLiteral("command"), QStringLiteral("list_panes")}},
                                     QStringLiteral("helper")).value(QStringLiteral("panes")).toArray())
            QVERIFY(!value.toObject().contains(QStringLiteral("you")));
    }

    // An `open` is aimed too: `new_pane: false` from a pane agent means *its* pane, not whichever
    // one has the focus. The resolved token travels on as `pane`, so the window's opener needs no
    // rule of its own.
    void openCarriesThePaneItWasAimedAt() {
        app.openTarget = [this](const QJsonObject &command, QString *) {
            state.opened << command.value(QStringLiteral("target")).toString() + QLatin1Char('@')
                                + command.value(QStringLiteral("pane")).toString();
            return true;
        };
        run({{QStringLiteral("id"), QStringLiteral("r1")},
             {QStringLiteral("command"), QStringLiteral("open")},
             {QStringLiteral("target"), QStringLiteral("conversation")}}, QStringLiteral("pane-b"));
        run({{QStringLiteral("id"), QStringLiteral("r2")},
             {QStringLiteral("command"), QStringLiteral("open")},
             {QStringLiteral("target"), QStringLiteral("conversation")},
             {QStringLiteral("pane"), QStringLiteral("pane-a")}}, QStringLiteral("pane-b"));
        run({{QStringLiteral("id"), QStringLiteral("r3")},
             {QStringLiteral("command"), QStringLiteral("open")},
             {QStringLiteral("target"), QStringLiteral("options")}}, QStringLiteral("helper"));
        QCOMPARE(state.opened, (QStringList{QStringLiteral("conversation@pane-b"),
                                            QStringLiteral("conversation@pane-a"),
                                            QStringLiteral("options@")}));
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r4")},
                      {QStringLiteral("command"), QStringLiteral("open")},
                      {QStringLiteral("target"), QStringLiteral("conversation")},
                      {QStringLiteral("pane"), QStringLiteral("pane-gone")}}, QStringLiteral("pane-b"))
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_pane"));
        QCOMPARE(state.opened.size(), 3);
    }

    // One named recently-closed pane. The key carries the id the close minted, so there is no set
    // to look it up in: it is matched by prefix, and `closed.list` / `closed.restore` cannot be
    // caught by that prefix because they use a dot.
    void aNamedClosedPaneIsSafeByItsPrefix() {
        QVERIFY(relay::appcommands::actionIsAgentSafe(QStringLiteral("closed:2")));
        QVERIFY(relay::appcommands::actionIsAgentSafe(QStringLiteral("closed:c-91f3ab")));
        QVERIFY(relay::appcommands::actionIsRead(QStringLiteral("closed:2")));
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("closed:")));       // no id at all
        QVERIFY(!relay::appcommands::actionIsAudited(QStringLiteral("closed.forget")));  // not the prefix
    }

    // ----- group 7: the writes toggle is about writing ------------------------------------------
    //
    // Owner, 2026-09-20: "7 pass the toggle like app_open". With the toggle off a helper could
    // open a conversation into a new pane through `app_open` — never gated, §30.4 — and could not
    // open an empty pane through `run_action`. Same act, two answers.
    void withWritesOffAnActionThatOnlyOpensRunsAndOneThatWritesDoesNot() {
        writes = false;
        QVERIFY2(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("pane.splitRight")}})
                     .value(QStringLiteral("ok")).toBool(),
                 "opening a pane changes nothing, so the toggle does not gate it");
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r2")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), QStringLiteral("pane.focusLeft")}})
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, (QStringList{QStringLiteral("pane.splitRight"),
                                         QStringLiteral("registry:pane.focusLeft")}));

        // Reversible, but it puts a file edited since the last read into effect: still gated.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r3")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), QStringLiteral("theme.reload")}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("writes_disabled"));
        // A row button that tests a key is reversible too, and also stays behind the toggle.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r4")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), relay::appcommands::rowActionKey(
                                                  QStringLiteral("models"), QStringLiteral("provider:acme"), 0)}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("writes_disabled"));
        QCOMPARE(state.ran.size(), 2);
    }

    // The two halves of the table are a partition, not two overlapping lists: everything that
    // reads is safe, and nothing may be read-only without being safe.
    void everyReadActionIsAgentSafe() {
        for (const QString &key : relay::appcommands::agentSafeActionKeys())
            QVERIFY2(relay::appcommands::actionIsAgentSafe(key), qPrintable(key));
        QVERIFY(!relay::appcommands::actionIsRead(QStringLiteral("theme.reload")));
        QVERIFY(!relay::appcommands::actionIsRead(QStringLiteral("pane.equalize")));
        QVERIFY(!relay::appcommands::actionIsRead(QStringLiteral("agent.screenshotPane")));
        QVERIFY(!relay::appcommands::actionIsRead(QStringLiteral("pane.close")));
    }

    // ----- group 6: the secret guard ------------------------------------------------------------
    //
    // `SettingRow::secret` is decision 1's "every value row except a secret", and until
    // 2026-09-20 no shipped row set it: the guard had never fired once, and nothing but §30.8
    // stood between the next key-shaped row and an agent reading its value out of the catalog.
    // So a Text row named like a credential and left unmarked is treated as one.
    //
    // The shipped catalog is built in RelayWindow::settingsSections(), which needs a window; the
    // rule itself is here, is what `catalog()` applies to every row it lists, and is what this
    // test walks. A row that trips it in the app also says so on stderr, naming itself.
    void aValueRowNamedLikeACredentialIsTreatedAsASecretEvenUnmarked() {
        app.sections = [this] {
            QList<SettingsSection> sections = catalog(&state);
            SettingsSection added;
            added.id = QStringLiteral("added");
            added.title = QStringLiteral("Added");
            for (const QString &id : {QStringLiteral("option:anthropic_api_key"),
                                      QStringLiteral("option:github_token"),
                                      QStringLiteral("option:sync_password"),
                                      QStringLiteral("option:store/credential"),
                                      QStringLiteral("option:ssh_passphrase")}) {
                SettingRow row;
                row.kind = SettingRow::Text;
                row.id = id;
                row.label = QStringLiteral("A row nobody marked");
                row.text = QStringLiteral("sk-do-not-leak");
                row.onText = [this](const QString &text) { state.ran << text; };
                added.rows << row;
            }
            {
                // …and one caught by its label rather than its id.
                SettingRow row;
                row.kind = SettingRow::Text;
                row.id = QStringLiteral("option:relayfree");
                row.label = QStringLiteral("OpenRouter key");
                row.text = QStringLiteral("sk-do-not-leak");
                row.onText = [this](const QString &text) { state.ran << text; };
                added.rows << row;
            }
            sections << added;
            return sections;
        };

        const QJsonObject block = app.catalog(QStringLiteral("tab-7"));
        static const QRegularExpression named(
            QStringLiteral("key|token|secret|password|passphrase|credential"),
            QRegularExpression::CaseInsensitiveOption);
        int guarded = 0;
        for (const auto &value : block.value(QStringLiteral("options")).toArray()) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("kind")).toString() != QStringLiteral("text")) continue;
            if (!named.match(entry.value(QStringLiteral("id")).toString()).hasMatch()
                && !named.match(entry.value(QStringLiteral("label")).toString()).hasMatch())
                continue;
            if (entry.value(QStringLiteral("id")).toString()
                == QStringLiteral("option:security/secret_patterns"))
                continue;   // the guard's own configuration, named in rowNamedLikeASecret()
            ++guarded;
            QVERIFY2(entry.value(QStringLiteral("secret")).toBool(),
                     qPrintable(entry.value(QStringLiteral("id")).toString()));
            QVERIFY2(!entry.value(QStringLiteral("settable")).toBool(),
                     qPrintable(entry.value(QStringLiteral("id")).toString()));
            QVERIFY2(!entry.contains(QStringLiteral("value")),
                     qPrintable(entry.value(QStringLiteral("id")).toString()));
        }
        QCOMPARE(guarded, 7);   // the six added above, plus the marked provider:acme/key
        QVERIFY(!QJsonDocument(block).toJson().contains("sk-do-not-leak"));

        // And the executor agrees with the catalog: listed as not settable, refused when set.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("set_option")},
                      {QStringLiteral("row"), QStringLiteral("option:github_token")},
                      {QStringLiteral("value"), QStringLiteral("ghp-nope")}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("secret"));
        QVERIFY(state.ran.isEmpty());
    }

    // The rule is narrow on purpose: only a Text row can hold a credential, and the broad rule
    // over every kind would have withheld the value of five innocent shipped rows on the word
    // "key" alone. Those must keep answering with their values.
    void aRowThatOnlyHasTheWordInItsNameKeepsItsValue() {
        app.sections = [this] {
            QList<SettingsSection> sections = catalog(&state);
            SettingsSection added;
            added.id = QStringLiteral("added");
            added.title = QStringLiteral("Added");
            {
                SettingRow row;                                   // Options › Voice, a Choice
                row.kind = SettingRow::Choice;
                row.id = QStringLiteral("option:voice_hold_key");
                row.label = QStringLiteral("Voice key");
                row.options = QStringList{QStringLiteral("ctrl"), QStringLiteral("alt")};
                row.current = QStringLiteral("ctrl");
                row.onChoose = [](const QString &) {};
                added.rows << row;
            }
            {
                SettingRow row;                                   // Options › Agent, a Number
                row.kind = SettingRow::Number;
                row.id = QStringLiteral("agent/first_token_timeout_s");
                row.label = QStringLiteral("Wait longer for the first token");
                row.number = 90;
                row.onNumber = [](int) {};
                added.rows << row;
            }
            {
                SettingRow row;                                   // Options › Security, a Text list
                row.kind = SettingRow::Text;
                row.id = QStringLiteral("option:security/secret_patterns");
                row.label = QStringLiteral("Files the agent never reads");
                row.text = QStringLiteral("\\.vault$ credentials");
                row.onText = [](const QString &) {};
                added.rows << row;
            }
            sections << added;
            return sections;
        };
        const QJsonObject block = app.catalog(QStringLiteral("tab-7"));
        for (const QString &id : {QStringLiteral("option:voice_hold_key"),
                                  QStringLiteral("agent/first_token_timeout_s"),
                                  QStringLiteral("option:security/secret_patterns")}) {
            const QJsonObject entry = rowOf(block, QStringLiteral("options"), id);
            QVERIFY2(!entry.value(QStringLiteral("secret")).toBool(), qPrintable(id));
            QVERIFY2(entry.value(QStringLiteral("settable")).toBool(), qPrintable(id));
            QVERIFY2(entry.contains(QStringLiteral("value")), qPrintable(id));
        }
    }

    void aButtonRunsUnlessRefusedAndAnUnauditedOneRunsAfterTheAnswer() {
        const QString test = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 0);
        const QString remove = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 1);
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r1")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), test}}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("button 0")});
        const QJsonObject removed = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                         {QStringLiteral("command"), QStringLiteral("run_action")},
                                         {QStringLiteral("key"), remove}});
        QVERIFY(removed.value(QStringLiteral("ok")).toBool());
        QVERIFY(removed.value(QStringLiteral("deferred")).toBool());
        QCoreApplication::processEvents();
        QCOMPARE(state.ran, (QStringList{QStringLiteral("button 0"), QStringLiteral("button 1")}));
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r3")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("remote.pair"))}})
                     .value(QStringLiteral("error")).toString(),
                 QStringLiteral("not_agent_safe"));
        QCOMPARE(state.ran.size(), 2);
    }

    // ----- undo and the row marker (§30.6) ------------------------------------------------------

    void undoPutsThePreviousValueBackAndIsAllowedWithWritesOff() {
        const QJsonObject set = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                     {QStringLiteral("command"), QStringLiteral("set_option")},
                                     {QStringLiteral("row"), QStringLiteral("option:theme")},
                                     {QStringLiteral("value"), QStringLiteral("light")}});
        const QString changeId = set.value(QStringLiteral("change_id")).toString();
        QCOMPARE(state.theme, QStringLiteral("light"));

        writes = false;      // the toggle stops new writes, never the way back (§30.4)
        const QJsonObject undone = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                        {QStringLiteral("command"), QStringLiteral("undo")},
                                        {QStringLiteral("change_id"), changeId}});
        QVERIFY(undone.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.theme, QStringLiteral("dark"));
        QCOMPARE(undone.value(QStringLiteral("previous")).toString(), QStringLiteral("light"));
        QCOMPARE(undone.value(QStringLiteral("value")).toString(), QStringLiteral("dark"));

        // The notification that made the offer says it was taken rather than offering it again,
        // and the agent's own revert is announced beside it with a way back of its own.
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.first().title, QStringLiteral("Agent changed Theme"));
        QCOMPARE(notes.first().actionLabel, QStringLiteral("Undo"));
        QCOMPARE(notes.at(1).title, QStringLiteral("Undone: Theme"));
        QVERIFY(notes.at(1).actionLabel.isEmpty());

        // And it cannot be taken twice.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r3")},
                      {QStringLiteral("command"), QStringLiteral("undo")},
                      {QStringLiteral("change_id"), changeId}}).value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_change"));
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r4")},
                      {QStringLiteral("command"), QStringLiteral("undo")},
                      {QStringLiteral("change_id"), QStringLiteral("c99")}}).value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_change"));
    }

    void theNotificationsUndoButtonDoesTheSameThing() {
        const QJsonObject set = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                     {QStringLiteral("command"), QStringLiteral("set_option")},
                                     {QStringLiteral("row"), QStringLiteral("option:steps")},
                                     {QStringLiteral("value"), 12}});
        QCOMPARE(state.steps, 12);
        const QString actionId = relay::NotificationCenter::instance().entries().first().actionId;
        QCOMPARE(actionId, AppCommands::undoActionId(set.value(QStringLiteral("change_id")).toString()));
        QVERIFY(app.undoFromNotification(actionId));
        QCOMPARE(state.steps, 50);
        QVERIFY(!app.undoFromNotification(QStringLiteral("appundo:c99")));   // another window's change
    }

    void theRowIsMarkedUntilThePersonTouchesIt() {
        run({{QStringLiteral("id"), QStringLiteral("r1")},
             {QStringLiteral("command"), QStringLiteral("set_option")},
             {QStringLiteral("row"), QStringLiteral("option:thinking")},
             {QStringLiteral("value"), false}});
        const QString note = relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking"));
        QCOMPARE(note, QStringLiteral("changed by the agent just now: on → off"));
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:theme")).isEmpty());

        relay::SettingsPane::clearAgentChanged(QStringLiteral("option:thinking"));
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking")).isEmpty());
    }

    void theMarkedRowDrawsItsNoteAndAHandEditTakesItAway() {
        run({{QStringLiteral("id"), QStringLiteral("r1")},
             {QStringLiteral("command"), QStringLiteral("set_option")},
             {QStringLiteral("row"), QStringLiteral("option:thinking")},
             {QStringLiteral("value"), false}});
        relay::SettingsPane pane(relay::SettingsPane::Mode::Options, [this] { return catalog(&state); },
                                 [this] { return actions(&state); });
        pane.show();
        pane.showTab(QStringLiteral("general"));
        const auto noteLabels = [&pane] {
            QStringList found;
            for (QLabel *label : pane.findChildren<QLabel *>())
                if (label->property("agentChanged").toBool()) found << label->text();
            return found;
        };
        QCOMPARE(noteLabels().size(), 1);
        QVERIFY(noteLabels().first().startsWith(QStringLiteral("changed by the agent")));

        // The person answers it by flipping the row back: the mark has done its job.
        auto *check = pane.findChild<QCheckBox *>();
        QVERIFY(check);
        check->toggle();
        QTest::qWait(30);
        QCOMPARE(state.thinking, true);
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking")).isEmpty());
        QVERIFY(noteLabels().isEmpty());
    }

    // ----- the answer, on whichever pipe the command came out of (§30.3) ----------------------
    //
    // There are two pipes and one executor: a pane's worker (src/Pane.h) and the tab's helper
    // worker (RelayWindow::boardWorker). Only the pane's was wired until 2026-09-20, so every
    // write the helper attempted waited out the 20-second deadline and answered `no_reply`. Both
    // routes now build the answer here, which is what keeps them from drifting apart again.
    void theAnswerIsBuiltTheSameWayOnEitherPipe() {
        const QJsonObject command{{QStringLiteral("id"), QStringLiteral("r9")},
                                  {QStringLiteral("command"), QStringLiteral("set_option")},
                                  {QStringLiteral("row"), QStringLiteral("option:thinking")},
                                  {QStringLiteral("value"), false}};
        // The helper's command, executed and answered: the same request id, the row's value
        // before and after, and a change the person can undo.
        const QJsonObject answer = relay::appcommands::answerFor(
            command, [this](const QJsonObject &sent) { return run(sent, QStringLiteral("helper")); });
        QCOMPARE(answer.value(QStringLiteral("type")).toString(), QStringLiteral("app_command_result"));
        QCOMPARE(answer.value(QStringLiteral("id")).toString(), QStringLiteral("r9"));
        QVERIFY(answer.value(QStringLiteral("ok")).toBool());
        QCOMPARE(answer.value(QStringLiteral("previous")).toBool(), true);
        QCOMPARE(answer.value(QStringLiteral("value")).toBool(), false);
        QVERIFY(!answer.value(QStringLiteral("change_id")).toString().isEmpty());
        QCOMPARE(state.thinking, false);
        QCOMPARE(app.changes().size(), 1);
        QCOMPARE(app.changes().first().who, QStringLiteral("helper"));

        // And with no window left to act in there is still an answer, so the tool call ends in an
        // error the transcript can show rather than in the deadline.
        const QJsonObject orphan = relay::appcommands::answerFor(command, {});
        QCOMPARE(orphan.value(QStringLiteral("type")).toString(), QStringLiteral("app_command_result"));
        QCOMPARE(orphan.value(QStringLiteral("id")).toString(), QStringLiteral("r9"));
        QVERIFY(!orphan.value(QStringLiteral("ok")).toBool());
        QCOMPARE(orphan.value(QStringLiteral("error")).toString(), QStringLiteral("failed"));
        QVERIFY(!orphan.value(QStringLiteral("message")).toString().isEmpty());
    }

    // A console's write is a helper-pipe write (card #AGNT). Options, Actions, Sessions, the
    // Switchboard and a card are all `Pane`s on the **tab's** worker now, so an `app_option_set`
    // typed into one of them arrives in that worker's own wire shape — `event: "app_command"`
    // with the command's fields beside it — and is executed with `who` = "helper". The drive of
    // #AGNT reported that such a write reached the setting and was never announced; it was
    // announced, and the notification list drew it outside its own viewport
    // (NotificationsPopup::rebuild). This is the half that is testable without a window, so that
    // "the console's route produces the notice and the mark" can never again rest on a
    // screenshot: the change log, the notice with its Undo, the row's marker, and the offer
    // working from where the person finds it.
    void aConsolesWriteIsAnnouncedAndMarkedLikeAPanesOwn() {
        const QJsonObject event{{QStringLiteral("event"), QStringLiteral("app_command")},
                                {QStringLiteral("id"), QStringLiteral("ac-1")},
                                {QStringLiteral("command"), QStringLiteral("set_option")},
                                {QStringLiteral("row"), QStringLiteral("option:thinking")},
                                {QStringLiteral("value"), false}};
        const QJsonObject answer = relay::appcommands::answerFor(
            event, [this](const QJsonObject &sent) { return run(sent, QStringLiteral("helper")); });
        QVERIFY(answer.value(QStringLiteral("ok")).toBool());
        QCOMPARE(answer.value(QStringLiteral("id")).toString(), QStringLiteral("ac-1"));
        QCOMPARE(state.thinking, false);

        QCOMPARE(app.changes().size(), 1);
        QCOMPARE(app.changes().first().who, QStringLiteral("helper"));
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().title, QStringLiteral("Agent changed Show thinking"));
        QCOMPARE(notes.first().body, QStringLiteral("on → off"));
        QCOMPARE(notes.first().actionLabel, QStringLiteral("Undo"));
        QCOMPARE(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking")),
                 QStringLiteral("changed by the agent just now: on → off"));

        // The popup hands the window the action id, the window hands it back here: the value
        // goes back, the mark goes with it, and the entry stops offering what has been taken.
        QVERIFY(app.undoFromNotification(notes.first().actionId));
        QCOMPARE(state.thinking, true);
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking")).isEmpty());
        const auto after = relay::NotificationCenter::instance().entries();
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.first().title, QStringLiteral("Undone: Show thinking"));
        QVERIFY(after.first().actionLabel.isEmpty());
    }

    // The same for an action: "Agent ran <label>", on the pipe a console's `app_action_run`
    // comes out of. No Undo on an action — what it did is its own to take back (§30.6).
    void aConsolesActionIsAnnouncedToo() {
        const QJsonObject event{{QStringLiteral("event"), QStringLiteral("app_command")},
                                {QStringLiteral("id"), QStringLiteral("ac-2")},
                                {QStringLiteral("command"), QStringLiteral("run_action")},
                                {QStringLiteral("key"), QStringLiteral("theme.reload")}};
        const QJsonObject answer = relay::appcommands::answerFor(
            event, [this](const QJsonObject &sent) { return run(sent, QStringLiteral("helper")); });
        QVERIFY(answer.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("theme.reload")});
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.first().title, QStringLiteral("Agent ran Reload themes"));
        QVERIFY(notes.first().actionLabel.isEmpty());
    }

    // Owner decision 6: a row an agent changed "carries a marker until the person touches it".
    // Pressing Undo *is* touching it, so the mark goes; an agent undoing its own change is still
    // the agent changing the row, so there the mark stays and says what the undo made it.
    void undoByHandClearsTheMarkAndUndoByTheAgentKeepsIt() {
        const QJsonObject set{{QStringLiteral("id"), QStringLiteral("r1")},
                              {QStringLiteral("command"), QStringLiteral("set_option")},
                              {QStringLiteral("row"), QStringLiteral("option:thinking")},
                              {QStringLiteral("value"), false}};
        QString change = run(set).value(QStringLiteral("change_id")).toString();
        QVERIFY(!change.isEmpty());
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking"))
                    .startsWith(QStringLiteral("changed by the agent")));

        // The person, through the notification's Undo.
        QVERIFY(app.undoFromNotification(relay::AppCommands::undoActionId(change)));
        QCOMPARE(state.thinking, true);
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking")).isEmpty());

        // The agent, through app_undo: the row is still one an agent is holding.
        change = run(set).value(QStringLiteral("change_id")).toString();
        const QJsonObject undone = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                        {QStringLiteral("command"), QStringLiteral("undo")},
                                        {QStringLiteral("change_id"), change}});
        QVERIFY(undone.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.thinking, true);
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking"))
                    .contains(QStringLiteral("off \u2192 on")));

        // \u2026and it is announced like any other agent write, with the way back from *it*: the
        // amended entry says the first offer was taken and offers nothing, so without this the
        // person whose setting the agent has just put back has nothing to press \u2014 and an
        // `app_undo` whose original entry had been dismissed would announce nothing at all.
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.first().title, QStringLiteral("Agent changed Show thinking"));
        QCOMPARE(notes.first().body, QStringLiteral("off \u2192 on"));
        QCOMPARE(notes.first().actionLabel, QStringLiteral("Undo"));
        QCOMPARE(notes.first().actionId, AppCommands::undoActionId(app.changes().last().id));
        QCOMPARE(notes.at(1).title, QStringLiteral("Undone: Show thinking"));
        QVERIFY(notes.at(1).actionLabel.isEmpty());

        // And that offer works: the person takes the agent's revert back, and the mark goes
        // because the person has now touched the row.
        QVERIFY(app.undoFromNotification(notes.first().actionId));
        QCOMPARE(state.thinking, false);
        QVERIFY(relay::SettingsPane::agentChangeNote(QStringLiteral("option:thinking")).isEmpty());
    }

    // ----- open a conversation (§30.4) ----------------------------------------------------------
    // The window's own half is one call to `Pane::openSavedSession` — the Sessions row's Enter —
    // so what is testable without a window is the rule in front of it: which row, and where.
    void openingAConversationCarriesTheRowAndWhereItGoes() {
        const QJsonObject row{{QStringLiteral("session_id"), QStringLiteral("abc123")},
                              {QStringLiteral("session_dir"), QStringLiteral("/home/e/sessions")},
                              {QStringLiteral("title"), QStringLiteral("The keybinding rewrite")}};
        QJsonObject item;
        bool newPane = false;
        QString error;
        QVERIFY(relay::appcommands::conversationToOpen(
            {{QStringLiteral("target"), QStringLiteral("conversation")},
             {QStringLiteral("conversation"), QStringLiteral("abc123")},
             {QStringLiteral("item"), row}}, &item, &newPane, &error));
        // A new pane unless the agent said otherwise: loading it where the person is sitting
        // takes that pane's conversation away.
        QVERIFY(newPane);
        QCOMPARE(item.value(QStringLiteral("session_id")).toString(), QStringLiteral("abc123"));
        QCOMPARE(item.value(QStringLiteral("session_dir")).toString(), QStringLiteral("/home/e/sessions"));
        QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("The keybinding rewrite"));

        QVERIFY(relay::appcommands::conversationToOpen(
            {{QStringLiteral("conversation"), QStringLiteral("abc123")},
             {QStringLiteral("item"), row},
             {QStringLiteral("new_pane"), false}}, &item, &newPane, &error));
        QVERIFY(!newPane);

        // The id alone still resumes: the row is what the worker knows, not what it must send.
        QVERIFY(relay::appcommands::conversationToOpen(
            {{QStringLiteral("conversation"), QStringLiteral("only-an-id")}}, &item, &newPane, &error));
        QCOMPARE(item.value(QStringLiteral("session_id")).toString(), QStringLiteral("only-an-id"));
    }

    void aConversationWithNoIdAndABadNewPaneAreRefused() {
        QJsonObject item;
        bool newPane = true;
        QString error;
        QVERIFY(!relay::appcommands::conversationToOpen(
            {{QStringLiteral("target"), QStringLiteral("conversation")}}, &item, &newPane, &error));
        QCOMPARE(error, QStringLiteral("unknown_conversation"));
        QVERIFY(!relay::appcommands::conversationToOpen(
            {{QStringLiteral("conversation"), QStringLiteral("abc123")},
             {QStringLiteral("new_pane"), QStringLiteral("yes")}}, &item, &newPane, &error));
        QCOMPARE(error, QStringLiteral("invalid_value"));
    }

    // …and the command reaches the window's opener like any other `open`, whatever the writes
    // toggle says: resuming a conversation changes no setting (§30.4).
    void openConversationGoesThroughTheOpenerWithTheIdAndTheFlag() {
        app.openTarget = [this](const QJsonObject &command, QString *error) {
            QJsonObject item;
            bool newPane = true;
            if (command.value(QStringLiteral("target")).toString() != QStringLiteral("conversation"))
                { if (error) *error = QStringLiteral("unknown_target"); return false; }
            if (!relay::appcommands::conversationToOpen(command, &item, &newPane, error)) return false;
            state.opened << QStringLiteral("conversation:%1:%2")
                                .arg(item.value(QStringLiteral("session_id")).toString(),
                                     newPane ? QStringLiteral("new") : QStringLiteral("here"));
            return true;
        };
        writes = false;
        QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                  {QStringLiteral("command"), QStringLiteral("open")},
                                  {QStringLiteral("target"), QStringLiteral("conversation")},
                                  {QStringLiteral("conversation"), QStringLiteral("abc123")},
                                  {QStringLiteral("new_pane"), true}});
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        result = run({{QStringLiteral("id"), QStringLiteral("r2")},
                      {QStringLiteral("command"), QStringLiteral("open")},
                      {QStringLiteral("target"), QStringLiteral("conversation")},
                      {QStringLiteral("conversation"), QStringLiteral("def456")},
                      {QStringLiteral("new_pane"), false}});
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.opened, (QStringList{QStringLiteral("conversation:abc123:new"),
                                            QStringLiteral("conversation:def456:here")}));

        const QJsonObject refused = run({{QStringLiteral("id"), QStringLiteral("r3")},
                                         {QStringLiteral("command"), QStringLiteral("open")},
                                         {QStringLiteral("target"), QStringLiteral("conversation")}});
        QCOMPARE(refused.value(QStringLiteral("error")).toString(),
                 QStringLiteral("unknown_conversation"));
    }

    // ----- group 8: one pane talking to another --------------------------------------------------
    //
    // Owner, 2026-09-20: "allow sending messages and pre-filling messages across panes". Until
    // this, no agent could put a prompt anywhere but its own pane, so the Sessions helper could
    // open three conversations and say nothing to any of them.

    void aSendReachesTheNamedPaneCarriesWhoAskedAndIsAnnounced() {
        const QJsonObject result = prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"),
                                          QStringLiteral("run the failing test"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("pane")).toString(), QStringLiteral("pane-b"));
        QCOMPARE(state.sent, (QStringList{QStringLiteral("pane-b<pane-a:run the failing test")}));
        QVERIFY(state.prefilled.isEmpty());
        // The person sees it: a pre-fill sitting in a composer announces itself, a send does not.
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.first().title.contains(QStringLiteral("sent a prompt")));
        QVERIFY(notes.first().title.contains(QStringLiteral("~/src pane-b")));   // the pane's own title
        QVERIFY(notes.first().body.contains(QStringLiteral("run the failing test")));
        // Whether it starts now or waits behind a turn already running there is part of what
        // happened, so it comes back with the result.
        QVERIFY(!result.value(QStringLiteral("queued")).toBool());
        state.paneBusy = true;
        QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"), QStringLiteral("and again"))
                    .value(QStringLiteral("queued")).toBool());
    }

    void aPrefillLeavesItUnsentAndNeverOverwritesADraft() {
        QVERIFY(prompt(QStringLiteral("prefill_prompt"), QStringLiteral("pane-b"),
                       QStringLiteral("git push --force?"))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.prefilled, (QStringList{QStringLiteral("pane-b<pane-a:git push --force?")}));
        QVERIFY(state.sent.isEmpty());          // nobody acted: the person still has to press Enter
        // The person is typing there. Their draft is in no file and no history until they send
        // it, so it is the one thing an agent may not write over.
        state.composerBusy = true;
        QCOMPARE(prompt(QStringLiteral("prefill_prompt"), QStringLiteral("pane-b"),
                        QStringLiteral("something else"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("busy"));
        QCOMPARE(state.prefilled.size(), 1);
    }

    void aPromptToAPaneThatHasGoneIsUnknownPaneAndAnUnnamedOneIsRefused() {
        QCOMPARE(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-gone"),
                        QStringLiteral("hello"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("unknown_pane"));
        QVERIFY(state.sent.isEmpty());
        // "Put this prompt somewhere" is not a request: unlike run_action there is no sensible
        // default pane, so an unnamed one is refused rather than guessed at.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("send_prompt")},
                      {QStringLiteral("text"), QStringLiteral("hello")}})
                     .value(QStringLiteral("error")).toString(), QStringLiteral("invalid_value"));
        // And an empty prompt reaches nobody.
        QCOMPARE(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"), QStringLiteral("   "))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("invalid_value"));
    }

    void sendingToYourOwnPaneIsRefused() {
        QCOMPARE(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-a"),
                        QStringLiteral("think harder"), QStringLiteral("pane-a"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("would_loop"));
        QCOMPARE(prompt(QStringLiteral("prefill_prompt"), QStringLiteral("pane-a"),
                        QStringLiteral("think harder"), QStringLiteral("pane-a"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("would_loop"));
        QVERIFY(state.sent.isEmpty() && state.prefilled.isEmpty());
    }

    // The ring: A prompts B, B prompts A, A prompts B… Each send is one link deeper than the
    // prompt the sending pane is running, and the chain stops at kMaxPromptChain — so a ring of
    // any size dies rather than running for ever with nobody asking.
    void theChainOfAgentPromptsIsCutOff() {
        state.panes << QStringLiteral("pane-c");
        QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"), QStringLiteral("1"),
                       QStringLiteral("pane-a")).value(QStringLiteral("ok")).toBool());
        QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-c"), QStringLiteral("2"),
                       QStringLiteral("pane-b")).value(QStringLiteral("ok")).toBool());
        QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-a"), QStringLiteral("3"),
                       QStringLiteral("pane-c")).value(QStringLiteral("ok")).toBool());
        const QJsonObject fourth = prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"),
                                          QStringLiteral("4"), QStringLiteral("pane-a"));
        QCOMPARE(fourth.value(QStringLiteral("error")).toString(), QStringLiteral("would_loop"));
        QCOMPARE(state.sent.size(), 3);
        // A pre-fill is not a link: nobody acts on it until the person presses Enter, and that
        // Enter is a person's prompt, which clears the count anyway.
        QVERIFY(prompt(QStringLiteral("prefill_prompt"), QStringLiteral("pane-b"), QStringLiteral("4"),
                       QStringLiteral("pane-a")).value(QStringLiteral("ok")).toBool());
        // The person typing in pane-a ends the chain that reached it.
        app.notePersonPrompt(QStringLiteral("pane-a"));
        QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"), QStringLiteral("5"),
                       QStringLiteral("pane-a")).value(QStringLiteral("ok")).toBool());
    }

    // The other half of the guard: the chain cap alone would let one pane fan a hundred prompts
    // into another pane's queue, each of them link one.
    void onePanesAgentCannotSendForEverWithoutAPersonAsking() {
        for (int i = 0; i < AppCommands::kMaxPromptsPerPane; ++i)
            QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"),
                           QStringLiteral("do %1").arg(i)).value(QStringLiteral("ok")).toBool());
        const QJsonObject over = prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"),
                                        QStringLiteral("and one more"));
        QCOMPARE(over.value(QStringLiteral("error")).toString(), QStringLiteral("would_loop"));
        QVERIFY(over.value(QStringLiteral("message")).toString().contains(QStringLiteral("typed one here")));
        QCOMPARE(state.sent.size(), AppCommands::kMaxPromptsPerPane);
        // A pre-fill still goes: it makes nobody act.
        QVERIFY(prompt(QStringLiteral("prefill_prompt"), QStringLiteral("pane-b"),
                       QStringLiteral("have a look")).value(QStringLiteral("ok")).toBool());
        // The person typing in pane-a is the evidence the budget exists to look for.
        app.notePersonPrompt(QStringLiteral("pane-a"));
        QVERIFY(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"),
                       QStringLiteral("one more, asked for")).value(QStringLiteral("ok")).toBool());
    }

    void bothAreRefusedWithTheWritesToggleOff() {
        writes = false;
        QCOMPARE(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"), QStringLiteral("go"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("writes_disabled"));
        QCOMPARE(prompt(QStringLiteral("prefill_prompt"), QStringLiteral("pane-b"), QStringLiteral("go"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("writes_disabled"));
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("rename")},
                      {QStringLiteral("what"), QStringLiteral("pane")},
                      {QStringLiteral("name"), QStringLiteral("deploy")}})
                     .value(QStringLiteral("error")).toString(), QStringLiteral("writes_disabled"));
        QVERIFY(state.sent.isEmpty() && state.prefilled.isEmpty() && state.renamed.isEmpty());
    }

    // `/rename` and `/rename-tab` are typed into a composer, and no agent can type into one — so
    // "call this pane «deploy»" could not be asked of an agent at all. Renaming back is the undo,
    // which is what allows it in the first place.
    void aPaneAndItsTabCanBeNamedAndNamedBack() {
        const QJsonObject named = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                       {QStringLiteral("command"), QStringLiteral("rename")},
                                       {QStringLiteral("what"), QStringLiteral("pane")},
                                       {QStringLiteral("name"), QStringLiteral("deploy")}},
                                      QStringLiteral("pane-b"));
        QVERIFY(named.value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.renamed, (QStringList{QStringLiteral("pane:pane-b=deploy")}));
        QCOMPARE(relay::NotificationCenter::instance().entries().first().title,
                 QStringLiteral("Agent renamed a pane"));
        // Renaming back: the previous name comes back with the result, so the agent can say what
        // it was and put it there again.
        const QJsonObject back = run({{QStringLiteral("id"), QStringLiteral("r2")},
                                      {QStringLiteral("command"), QStringLiteral("rename")},
                                      {QStringLiteral("what"), QStringLiteral("pane")},
                                      {QStringLiteral("name"), QString()},
                                      {QStringLiteral("pane"), QStringLiteral("pane-b")}},
                                     QStringLiteral("pane-a"));
        QCOMPARE(back.value(QStringLiteral("previous")).toString(), QStringLiteral("deploy"));
        QCOMPARE(state.names.value(QStringLiteral("pane:pane-b")), QString());
        // The tab the pane sits in, the other half of the pair.
        run({{QStringLiteral("id"), QStringLiteral("r3")},
             {QStringLiteral("command"), QStringLiteral("rename")},
             {QStringLiteral("what"), QStringLiteral("tab")},
             {QStringLiteral("name"), QStringLiteral("release")}}, QStringLiteral("pane-a"));
        QCOMPARE(state.names.value(QStringLiteral("tab:pane-a")), QStringLiteral("release"));
        // Neither a made-up thing nor a pane that has gone.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r4")},
                      {QStringLiteral("command"), QStringLiteral("rename")},
                      {QStringLiteral("what"), QStringLiteral("window")},
                      {QStringLiteral("name"), QStringLiteral("x")}}, QStringLiteral("pane-a"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("invalid_value"));
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r5")},
                      {QStringLiteral("command"), QStringLiteral("rename")},
                      {QStringLiteral("what"), QStringLiteral("pane")},
                      {QStringLiteral("name"), QStringLiteral("x")},
                      {QStringLiteral("pane"), QStringLiteral("pane-gone")}}, QStringLiteral("pane-a"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("unknown_pane"));
        // The helper has no pane of its own, so it has to say which one it means.
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r6")},
                      {QStringLiteral("command"), QStringLiteral("rename")},
                      {QStringLiteral("what"), QStringLiteral("pane")},
                      {QStringLiteral("name"), QStringLiteral("x")}}, QStringLiteral("helper"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("unknown_pane"));
        QCOMPARE(state.renamed.size(), 3);
    }

    // A window with none of these callbacks — the case every test above the group-2 block runs in
    // — says so rather than claiming to have done something.
    void withNoWindowToActInTheCommandsFail() {
        app.deliverPrompt = nullptr;
        app.renameTarget = nullptr;
        QCOMPARE(prompt(QStringLiteral("send_prompt"), QStringLiteral("pane-b"), QStringLiteral("go"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("failed"));
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r1")},
                      {QStringLiteral("command"), QStringLiteral("rename")},
                      {QStringLiteral("what"), QStringLiteral("pane")},
                      {QStringLiteral("name"), QStringLiteral("x")}}, QStringLiteral("pane-a"))
                     .value(QStringLiteral("error")).toString(), QStringLiteral("failed"));
    }

    void anUnknownCommandIsRefusedRatherThanIgnored() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("launch_rockets")}});
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_target"));
    }

    // #J0VY: the `app_catalog` echo brake. A `presets` event whose settings did not change used
    // to make the window resend the whole catalog to every worker; their echoes re-fired presets
    // and the loop ran for hours. The gate: first send always goes, identical content sends
    // nothing, a real change goes through, and a cleared cache (the worker went away) sends again.
    void catalogChangedGatesIdenticalResends() {
        const QJsonObject dark = {{QStringLiteral("values"),
                                   QJsonObject{{QStringLiteral("theme"), QStringLiteral("dark")}}}};
        const QJsonObject light = {{QStringLiteral("values"),
                                    QJsonObject{{QStringLiteral("theme"), QStringLiteral("light")}}}};
        QByteArray last;
        QVERIFY(AppCommands::catalogChanged(last, dark));
        QCOMPARE(last, QJsonDocument(dark).toJson(QJsonDocument::Compact));
        QVERIFY(!AppCommands::catalogChanged(last, dark));
        QVERIFY(AppCommands::catalogChanged(last, light));
        QVERIFY(!AppCommands::catalogChanged(last, light));
        last.clear();
        QVERIFY(AppCommands::catalogChanged(last, light));
    }
};

QTEST_MAIN(AppCommandsTests)
#include "appcommands_test.moc"
