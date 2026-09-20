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
        // Test is reversible and Remove is not, on one row: decision 2, per button.
        SettingRow row;
        row.kind = SettingRow::Buttons;
        row.id = QStringLiteral("provider:acme");
        row.label = QStringLiteral("acme");
        row.buttonTexts = QStringList{QStringLiteral("test"), QStringLiteral("remove")};
        row.agentSafeButtons = QList<int>{0};
        row.onButton = [state](int index) { state->ran << QStringLiteral("button %1").arg(index); };
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
        item.agentSafe = relay::appcommands::actionIsAgentSafe(item.key);
        item.run = [state] { state->ran << QStringLiteral("app.settings"); };
        items << item;
    }
    {
        ActionItem item;
        item.key = QStringLiteral("windows.fresh");
        item.section = QStringLiteral("Relay");
        item.label = QStringLiteral("Start a fresh window set");
        item.agentSafe = relay::appcommands::actionIsAgentSafe(item.key);
        item.run = [state] { state->ran << QStringLiteral("windows.fresh"); };
        items << item;
    }
    return items;
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

private Q_SLOTS:
    void init() {
        state = State();
        writes = true;
        app = AppCommands();
        app.sections = [this] { return catalog(&state); };
        app.actions = [this] { return actions(&state); };
        app.writesEnabled = [this] { return writes; };
        app.tabId = [] { return QStringLiteral("tab-7"); };
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
        const QJsonObject block = app.catalog();
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
        const QJsonObject key = rowOf(app.catalog(), QStringLiteral("options"), QStringLiteral("provider:acme/key"));
        QVERIFY(!key.isEmpty());                                    // it is listed: the agent can say where it is
        QVERIFY(key.value(QStringLiteral("secret")).toBool());
        QVERIFY(!key.value(QStringLiteral("settable")).toBool());
        QVERIFY(!key.contains(QStringLiteral("value")));            // and the key itself never crosses
        QVERIFY(!QJsonDocument(app.catalog()).toJson().contains("sk-do-not-leak"));
    }

    void buttonRowsAreListedAsActionsOneMarkedSafeAndOneNot() {
        const QJsonObject block = app.catalog();
        const QJsonObject buttons = rowOf(block, QStringLiteral("options"), QStringLiteral("provider:acme"));
        QCOMPARE(buttons.value(QStringLiteral("kind")).toString(), QStringLiteral("buttons"));
        QVERIFY(!buttons.value(QStringLiteral("settable")).toBool());

        const QString test = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 0);
        const QString remove = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 1);
        QVERIFY(rowOf(block, QStringLiteral("actions"), test).value(QStringLiteral("agent_safe")).toBool());
        QVERIFY(!rowOf(block, QStringLiteral("actions"), remove).value(QStringLiteral("agent_safe")).toBool());
    }

    void agentSafeIsOptInPerAction() {
        const QJsonObject block = app.catalog();
        QVERIFY(rowOf(block, QStringLiteral("actions"), QStringLiteral("app.settings")).value(QStringLiteral("agent_safe")).toBool());
        QVERIFY(!rowOf(block, QStringLiteral("actions"), QStringLiteral("windows.fresh")).value(QStringLiteral("agent_safe")).toBool());
    }

    void theToggleRidesOnTheCatalog() {
        writes = false;
        QVERIFY(!app.catalog().value(QStringLiteral("writes_enabled")).toBool());
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
                                     {QStringLiteral("key"), QStringLiteral("app.settings")}});
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
                                        {QStringLiteral("key"), QStringLiteral("windows.fresh")}});
        QCOMPARE(unsafe.value(QStringLiteral("error")).toString(), QStringLiteral("not_agent_safe"));

        const QJsonObject missing = run({{QStringLiteral("id"), QStringLiteral("r3")},
                                         {QStringLiteral("command"), QStringLiteral("run_action")},
                                         {QStringLiteral("key"), QStringLiteral("no.such.action")}});
        QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_action"));
        QCOMPARE(state.ran.size(), 1);
    }

    void aSafeButtonOnARowRunsAndAnUnsafeOneDoesNot() {
        const QString test = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 0);
        const QString remove = relay::appcommands::rowActionKey(QStringLiteral("models"), QStringLiteral("provider:acme"), 1);
        QVERIFY(run({{QStringLiteral("id"), QStringLiteral("r1")},
                     {QStringLiteral("command"), QStringLiteral("run_action")},
                     {QStringLiteral("key"), test}}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(state.ran, QStringList{QStringLiteral("button 0")});
        QCOMPARE(run({{QStringLiteral("id"), QStringLiteral("r2")},
                      {QStringLiteral("command"), QStringLiteral("run_action")},
                      {QStringLiteral("key"), remove}}).value(QStringLiteral("error")).toString(),
                 QStringLiteral("not_agent_safe"));
        QCOMPARE(state.ran.size(), 1);
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

        // The notification says it was taken rather than offering it again.
        const auto notes = relay::NotificationCenter::instance().entries();
        QCOMPARE(notes.first().title, QStringLiteral("Undone: Theme"));
        QVERIFY(notes.first().actionLabel.isEmpty());

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

    void anUnknownCommandIsRefusedRatherThanIgnored() {
        const QJsonObject result = run({{QStringLiteral("id"), QStringLiteral("r1")},
                                        {QStringLiteral("command"), QStringLiteral("launch_rockets")}});
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_target"));
    }
};

QTEST_MAIN(AppCommandsTests)
#include "appcommands_test.moc"
