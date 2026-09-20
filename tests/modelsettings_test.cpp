// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The two model modals. The keys modal lists a row per plan. The per-job models modal (RolesDialog)
// is one row per job plus the vision row: a job follows a tier or has a provider of its own, and the
// provider lists name companies ("Kimi", "Z.AI (GLM)") and offer only providers whose key is
// actually stored. Owner, 2026-09-18: "in model roles, it shouldn't show options where you don't
// have a key assigned … it should not say the model name". The tiers themselves — main, high,
// flash, lite, local — are the five lists on Options › Models (owner, 2026-09-20), so nothing here
// covers a default-provider box or a tier row any more: the first test below says they are gone.
#include "ModelSettings.h"

#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

using relay::RolesDialog;

namespace {

QJsonObject preset(const QString &id, const QString &label, const QString &provider,
                   const QString &plan, bool key) {
    return {{QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("provider"), provider},
            {QStringLiteral("plan"), plan},
            {QStringLiteral("base_url"), QStringLiteral("https://example.invalid/") + id},
            {QStringLiteral("model"), QStringLiteral("model-of-") + id},
            {QStringLiteral("group"), QStringLiteral("payg")},
            {QStringLiteral("efforts"), QJsonArray{QStringLiteral("low"), QStringLiteral("high")}},
            {QStringLiteral("has_stored_key"), key}};
}

// Presets as the worker sends them, with `keyed` holding the ids that have a stored key.
QJsonArray presets(const QStringList &keyed) {
    const auto has = [&keyed](const QString &id) { return keyed.contains(id); };
    return {preset(QStringLiteral("kimi"), QStringLiteral("Kimi · K3"), QStringLiteral("Kimi"),
                   QStringLiteral("Pay-as-you-go"), has(QStringLiteral("kimi"))),
            preset(QStringLiteral("kimi-code"), QStringLiteral("Kimi Code · K3"), QStringLiteral("Kimi"),
                   QStringLiteral("Coding Plan"), has(QStringLiteral("kimi-code"))),
            preset(QStringLiteral("glm-coding"), QStringLiteral("Z.AI · GLM-5.3 · Coding Plan"),
                   QStringLiteral("Z.AI (GLM)"), QStringLiteral("Coding Plan"),
                   has(QStringLiteral("glm-coding"))),
            preset(QStringLiteral("openai"), QStringLiteral("OpenAI · GPT-6 Astra"),
                   QStringLiteral("OpenAI (ChatGPT)"), QStringLiteral("Pay-as-you-go"),
                   has(QStringLiteral("openai")))};
}

// A model server on this machine, as backend/worker.py appends it to the `presets` event: no key is
// stored and none is needed, `local` is what makes the row usable (card #24XJ).
QJsonObject localPreset() {
    return {{QStringLiteral("id"), QStringLiteral("local:bonsai")},
            {QStringLiteral("label"), QStringLiteral("Bonsai 2 27B")},
            {QStringLiteral("provider"), QStringLiteral("Bonsai 2 27B")},
            {QStringLiteral("plan"), QStringLiteral("llama.cpp")},
            {QStringLiteral("base_url"), QStringLiteral("http://127.0.0.1:8080/v1")},
            {QStringLiteral("model"), QStringLiteral("bonsai-2-27b")},
            {QStringLiteral("group"), QStringLiteral("local")},
            {QStringLiteral("efforts"), QJsonArray{}},
            {QStringLiteral("local"), true},
            {QStringLiteral("server"), QStringLiteral("llamacpp")},
            {QStringLiteral("key_source"), QStringLiteral("local")},
            {QStringLiteral("has_stored_key"), false}};
}

QJsonArray presetsWithLocal(const QStringList &keyed) {
    QJsonArray rows = presets(keyed);
    rows.append(localPreset());   // the worker appends local rows after the built-in presets
    return rows;
}

// Relay Free, as backend/worker.py emits it (protocol 13.8): the hosted provider. No key is stored
// and none is needed; `hosted` plus `available` is what makes the row usable, and `quota` is the
// last allowance the worker saw (null before the first exchange).
QJsonObject hostedPreset(bool available = true, const QJsonValue &quota = QJsonValue::Null) {
    return {{QStringLiteral("id"), QStringLiteral("relay-free")},
            {QStringLiteral("label"), QStringLiteral("Relay Free")},
            {QStringLiteral("provider"), QStringLiteral("Relay")},
            {QStringLiteral("plan"), QStringLiteral("Included")},
            {QStringLiteral("base_url"), QStringLiteral("https://api.relay-terminal.ai/v1")},
            {QStringLiteral("model"), QStringLiteral("relay-main")},
            {QStringLiteral("group"), QStringLiteral("included")},
            {QStringLiteral("efforts"), QJsonArray{}},
            {QStringLiteral("key_url"), QStringLiteral("https://relay-terminal.ai/free.html")},
            {QStringLiteral("note"), QStringLiteral("Included with Relay, no API key.")},
            {QStringLiteral("hosted"), true},
            {QStringLiteral("available"), available},
            {QStringLiteral("key_source"), QStringLiteral("included")},
            {QStringLiteral("has_stored_key"), false},
            {QStringLiteral("quota"), quota}};
}

QJsonObject quota(int limit, int used) {
    return {{QStringLiteral("limit"), limit}, {QStringLiteral("used"), used},
            {QStringLiteral("resets_at"), 1800000000}};
}

// The worker lists the hosted row first, before the keyed presets.
QJsonArray presetsWithHosted(const QStringList &keyed, bool available = true,
                             const QJsonValue &quota = QJsonValue::Null) {
    QJsonArray rows{hostedPreset(available, quota)};
    for (const auto &row : presets(keyed)) rows.append(row);
    return rows;
}

// The keys modal's tree: the group headings in order, and the row for one preset.
QStringList groupTitles(const QTreeWidget *list) {
    QStringList out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) out << list->topLevelItem(i)->text(0);
    return out;
}

QTreeWidgetItem *keysRow(QTreeWidget *list, const QString &id) {
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        for (int j = 0; j < list->topLevelItem(i)->childCount(); ++j)
            if (list->topLevelItem(i)->child(j)->data(0, Qt::UserRole + 1).toString() == id)
                return list->topLevelItem(i)->child(j);
    return nullptr;
}

QPushButton *buttonNamed(const QDialog &dialog, const QString &text) {
    for (QPushButton *button : dialog.findChildren<QPushButton *>())
        if (button->text() == text) return button;
    return nullptr;
}

QJsonObject tierSpec(const QString &id, const QString &label) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("hint"), QStringLiteral("what ") + id + QStringLiteral(" is for")}};
}

// The worker's tier catalog. The dialog takes only the tiers' hints from it now; `recommended` is
// still sent, and is what the removed "Recommended: …" line used to print.
QJsonObject catalog() {
    return {{QStringLiteral("tiers"), QJsonArray{tierSpec(QStringLiteral("high"), QStringLiteral("High")),
                                                 tierSpec(QStringLiteral("main"), QStringLiteral("Main")),
                                                 tierSpec(QStringLiteral("flash"), QStringLiteral("Flash")),
                                                 tierSpec(QStringLiteral("lite"), QStringLiteral("Lite")),
                                                 tierSpec(QStringLiteral("local"), QStringLiteral("Local"))}},
            {QStringLiteral("recommended"), QJsonArray{QJsonArray{QStringLiteral("glm-coding"),
                                                                  QStringLiteral("kimi")}}}};
}

QStringList itemsOf(const QComboBox *box) {
    QStringList out;
    for (int i = 0; i < box->count(); ++i) out << box->itemText(i);
    return out;
}

// One row of a provider's per-model catalog, as presets.py catalog_rows() sends it.
QJsonObject modelRow(const QString &id, const QString &label, const QJsonArray &efforts,
                     const QJsonObject &labels = {}) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("tier"), QJsonValue::Null}, {QStringLiteral("efforts"), efforts},
            {QStringLiteral("effort_labels"), labels}};
}

// A copy of `rows` with one preset's fields replaced.
QJsonArray with(QJsonArray rows, const QString &id, const QJsonObject &fields) {
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject();
        if (row.value(QStringLiteral("id")).toString() != id) continue;
        for (auto it = fields.begin(); it != fields.end(); ++it) row.insert(it.key(), it.value());
        rows.replace(i, row);
    }
    return rows;
}

// GLM's catalog: the default model (the preset's `model`) and its Flash sibling.
QJsonArray glmModels() {
    const QJsonArray levels{QStringLiteral("low"), QStringLiteral("high")};
    return {modelRow(QStringLiteral("model-of-glm-coding"), QStringLiteral("GLM 5.3"), levels),
            modelRow(QStringLiteral("glm-5.3-flash"), QStringLiteral("GLM 5.3 Flash"), levels)};
}

// A copy of `rows` with one preset's effort list and note replaced: what that provider's endpoint can
// actually be asked for (presets.py `effort_levels`) and the line about the levels it folds away.
QJsonArray withEfforts(QJsonArray rows, const QString &id, const QJsonArray &levels,
                       const QString &note = QString()) {
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject();
        if (row.value(QStringLiteral("id")).toString() != id) continue;
        row.insert(QStringLiteral("efforts"), levels);
        row.insert(QStringLiteral("effort_note"), note);
        rows.replace(i, row);
    }
    return rows;
}

// One job, as roles.py action_catalog() sends it.
QJsonArray actions(const QString &tier = QStringLiteral("flash")) {
    return {QJsonObject{{QStringLiteral("role"), QStringLiteral("summaries")},
                        {QStringLiteral("label"), QStringLiteral("Summaries")},
                        {QStringLiteral("hint"), QStringLiteral("condensing")},
                        {QStringLiteral("tier"), tier},
                        {QStringLiteral("settable"), true}}};
}

// The rows are rebuilt on every change, and the retired widgets only go away when the deferred
// deletes run — so every lookup flushes them first, or the old rows still answer findChildren.
QComboBox *comboNamed(const QDialog &dialog, const QString &name) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (QComboBox *box : dialog.findChildren<QComboBox *>())
        if (box->accessibleName() == name) return box;
    return nullptr;
}

QPushButton *modelButton(const QDialog &dialog, const QString &name) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (QPushButton *button : dialog.findChildren<QPushButton *>())
        if (button->accessibleName() == name) return button;
    return nullptr;
}

void pick(QComboBox *box, const QString &data) {
    const int index = box->findData(data);
    QVERIFY2(index >= 0, qPrintable(box->accessibleName() + QStringLiteral(" has no entry ") + data));
    box->setCurrentIndex(index);
    Q_EMIT box->activated(index);
}

// Model… opens a modal QInputDialog and blocks in its own event loop, so the answer has to be armed
// before the click. The try count is only a backstop: an unanswered dialog would hang the suite.
void answerModelPrompt(const QString &text, bool accept = true) {
    auto *timer = new QTimer;
    auto *tries = new int(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, tries, text, accept] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog && ++*tries < 500) return;
        if (dialog) {
            dialog->setTextValue(text);
            if (accept) dialog->accept();
            else dialog->reject();
        } else if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            modal->reject();
        }
        timer->stop();
        timer->deleteLater();
        delete tries;
    });
    timer->start(0);
}

}  // namespace

class ModelSettingsTests : public QObject {
    Q_OBJECT

    // The dialog as the pane opens it: presets, the tier catalog, one job, the pane's provider.
    static void open(RolesDialog &dialog, const QJsonArray &rows, const QString &provider = QStringLiteral("kimi")) {
        dialog.setPresets(rows, catalog(), actions());
        dialog.setProvider(provider);
    }

    // Moves the Summaries job onto a provider of its own, the way a user does: through its tier box.
    static void pin(RolesDialog &dialog) { pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("custom")); }

    static QString stored(const QString &role, const QString &field) {
        return QSettings().value(RolesDialog::roleSetting(role, field)).toString();
    }
    static bool has(const QString &role, const QString &field) {
        return QSettings().contains(RolesDialog::roleSetting(role, field));
    }

private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("ModelSettingsTest"));
    }

    void init() { QSettings().clear(); }

    // ----- what the dialog is (owner, 2026-09-20) ------------------------------------------------

    // The default-provider box, the recommended line and the Main / Flash / Lite / Local rows went to
    // Options › Models, where the tiers are five ordered lists. What was behind "Advanced options"
    // is the dialog: the job rows are there without opening anything.
    void theDialogIsThePerJobRowsAndTheTierRowsAreGone() {
        RolesDialog dialog;
        open(dialog, presetsWithLocal({QStringLiteral("kimi"), QStringLiteral("glm-coding")}));
        QVERIFY(dialog.windowTitle().contains(QStringLiteral("per-job models")));
        for (const char *gone : {"Default provider", "Main provider", "Main effort", "Flash effort",
                                 "Lite effort", "Local model"})
            QVERIFY2(!comboNamed(dialog, QLatin1String(gone)), gone);
        for (const char *gone : {"Main model", "Flash model", "Lite model"})
            QVERIFY2(!modelButton(dialog, QLatin1String(gone)), gone);
        for (QPushButton *button : dialog.findChildren<QPushButton *>())
            QVERIFY2(!button->text().contains(QStringLiteral("Advanced options")), qPrintable(button->text()));
        for (QLabel *label : dialog.findChildren<QLabel *>())
            QVERIFY2(!label->text().startsWith(QStringLiteral("Recommended:")), qPrintable(label->text()));
        // No disclosure to open first: roles/advanced_open is not set, and the job row is there.
        QVERIFY(!QSettings().contains(QStringLiteral("roles/advanced_open")));
        QVERIFY(comboNamed(dialog, QStringLiteral("Summaries")));
        QVERIFY(comboNamed(dialog, QStringLiteral("Vision provider")));
        QVERIFY(buttonNamed(dialog, QStringLiteral("API keys…")));
    }

    // A job follows one of the five tiers, named as Options › Models names its lists — lower-case —
    // or has a provider of its own. The first entry is the job's built-in tier, which stores nothing.
    void aJobsTierBoxListsTheFiveTiersInLowerCaseThenItsOwnProvider() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi")}));
        QComboBox *choice = comboNamed(dialog, QStringLiteral("Summaries"));
        QVERIFY(choice);
        QCOMPARE(itemsOf(choice), QStringList({QStringLiteral("default (flash)"), QStringLiteral("high"),
                                               QStringLiteral("main"), QStringLiteral("flash"),
                                               QStringLiteral("lite"), QStringLiteral("local"),
                                               QStringLiteral("its own provider…")}));
        QCOMPARE(choice->currentIndex(), 0);
        // The same ids the pane and the Switchboard build the protocol objects from.
        for (const QString &tier : RolesDialog::tierIds()) QVERIFY(choice->findData(tier) > 0);
        // Each tier says what it is for, from the worker's catalog.
        QCOMPARE(choice->itemData(choice->findData(QStringLiteral("high")), Qt::ToolTipRole).toString(),
                 QStringLiteral("what high is for"));
        pick(choice, QStringLiteral("high"));
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("tier")), QStringLiteral("high"));
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QString());
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("tier")));
    }

    // Every job can follow the Local tier too, by the same list.
    void aJobRowOffersTheLocalTier() {
        RolesDialog dialog;
        open(dialog, presetsWithLocal({QStringLiteral("kimi")}));
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("local"));
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("tier")), QStringLiteral("local"));
    }

    // Nothing in this dialog writes a `tiers/…` key or the pane's own model any more: the tiers are
    // the five lists on Options › Models.
    void theRemovedRowsCallbacksAreNeverCalledAndNoTierSettingIsWritten() {
        RolesDialog dialog;
        int calls = 0, changed = 0;
        dialog.onRolesChanged = [&changed] { ++changed; };
        open(dialog, with(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}),
                          QStringLiteral("glm-coding"), {{QStringLiteral("models"), glmModels()}}));
        pin(dialog);
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("glm-coding"));
        pick(comboNamed(dialog, QStringLiteral("Summaries model")), QStringLiteral("glm-5.3-flash"));
        pick(comboNamed(dialog, QStringLiteral("Summaries effort")), QStringLiteral("low"));
        pick(comboNamed(dialog, QStringLiteral("Vision provider")), QStringLiteral("glm-coding"));
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("main"));
        QCOMPARE(calls, 0);
        QCOMPARE(changed, 6);
        QSettings settings;
        for (const QString &key : settings.allKeys())
            QVERIFY2(key.startsWith(QStringLiteral("roles/")), qPrintable(key));
    }

    // ----- a provider of the job's own -------------------------------------------------------------

    // Owner, 2026-09-18: "i might want to pick kimi k3 for main agents and glm 5.3 flash for
    // subagents". So a provider of the job's own is a choice in the same box as the tiers, and the
    // two are exclusive. Until it is picked the row has no provider box and no model box at all.
    void itsOwnProviderPinsTheJobWhereItAlreadyRunsAndDropsTheTier() {
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("tier")),
                             QStringLiteral("flash"));
        RolesDialog dialog;
        int changed = 0;
        dialog.onRolesChanged = [&changed] { ++changed; };
        open(dialog, presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}));
        // The worker says the job's tier landed on Z.AI, so that is where "its own provider…" starts:
        // the pick changes nothing until the boxes it reveals are used.
        dialog.setResolved({}, {{QStringLiteral("summaries"),
                                 QJsonObject{{QStringLiteral("tier"), QStringLiteral("flash")},
                                             {QStringLiteral("preset"), QStringLiteral("glm-coding")},
                                             {QStringLiteral("model"), QStringLiteral("glm-5.3-flash")}}}});
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries provider")));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries model")));
        QVERIFY(!modelButton(dialog, QStringLiteral("Summaries model")));
        pin(dialog);
        QVERIFY(changed > 0);
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("glm-coding"));
        // Protocol 13.7 takes a tier or an endpoint, never both.
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("tier")));
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries"))->currentText(), QStringLiteral("its own provider…"));
        QComboBox *provider = comboNamed(dialog, QStringLiteral("Summaries provider"));
        QVERIFY(provider);
        // Companies, not model names, and no neutral entry: the way back is the tier box.
        QCOMPARE(itemsOf(provider), QStringList({QStringLiteral("Kimi"), QStringLiteral("Z.AI (GLM)")}));
        QCOMPARE(provider->currentText(), QStringLiteral("Z.AI (GLM)"));
        pick(provider, QStringLiteral("kimi"));
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("kimi"));
        // Picking the entry again while pinned changes nothing.
        pin(dialog);
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("kimi"));
        // And back: naming a tier drops the job's own endpoint, and its boxes with it.
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("lite"));
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("tier")), QStringLiteral("lite"));
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("preset")));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries provider")));
    }

    // With nothing resolved yet the job is pinned to the pane's provider; when that one has no key,
    // to the first provider that has.
    void itsOwnProviderStartsOnAProviderThatCanBeReached() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("glm-coding")}), QStringLiteral("kimi"));
        pin(dialog);
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("glm-coding"));
    }

    void aProviderBoxOffersOnlyProvidersWithAKeyAndNamesTheCompany() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}));
        pin(dialog);
        for (const char *name : {"Summaries provider", "Vision provider"}) {
            QComboBox *box = comboNamed(dialog, QLatin1String(name));
            QVERIFY(box);
            QVERIFY(itemsOf(box).contains(QStringLiteral("Kimi")));
            QVERIFY(itemsOf(box).contains(QStringLiteral("Z.AI (GLM)")));
            // No model ids, and no rows for providers with no key.
            for (const QString &item : itemsOf(box)) {
                QVERIFY(!item.contains(QStringLiteral("K3")));
                QVERIFY(!item.contains(QStringLiteral("GPT")));
                QVERIFY(!item.contains(QStringLiteral("no key")));
            }
        }
    }

    void twoPlansFromOneCompanyAreToldApartByThePlan() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi"), QStringLiteral("kimi-code")}));
        pin(dialog);
        QCOMPARE(itemsOf(comboNamed(dialog, QStringLiteral("Summaries provider"))),
                 QStringList({QStringLiteral("Kimi · Pay-as-you-go"), QStringLiteral("Kimi · Coding Plan")}));
    }

    void withNoKeysAtAllEveryProviderIsStillOfferedSoTheRowIsNotEmpty() {
        RolesDialog dialog;
        open(dialog, presets({}));
        pin(dialog);
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("kimi"));
        QComboBox *box = comboNamed(dialog, QStringLiteral("Summaries provider"));
        QCOMPARE(box->count(), 4);
        QVERIFY(box->itemText(0).startsWith(QStringLiteral("Kimi")));
        QVERIFY(box->itemText(0).endsWith(QStringLiteral("(no key)")));
    }

    void aJobKeepsShowingAProviderWhoseKeyWentAway() {
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("preset")),
                             QStringLiteral("glm-coding"));
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi")}));
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries provider"))->currentText(),
                 QStringLiteral("Z.AI (GLM)  (no key)"));
    }

    // A model server on this machine has no key and needs none, so the dialog offers it like any
    // reachable provider — and never as "(no key)", which would read as something to fix.
    void aLocalEndpointIsChoosableWithoutAStoredKey() {
        RolesDialog dialog;
        open(dialog, presetsWithLocal({QStringLiteral("kimi")}));
        pin(dialog);
        QComboBox *box = comboNamed(dialog, QStringLiteral("Summaries provider"));
        QVERIFY(itemsOf(box).contains(QStringLiteral("Bonsai 2 27B")));
        for (const QString &item : itemsOf(box)) QVERIFY(!item.contains(QStringLiteral("no key")));
        pick(box, QStringLiteral("local:bonsai"));
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("local:bonsai"));
        // A local server sends no catalog and has no effort knob: a typed id, and no effort box.
        QVERIFY(modelButton(dialog, QStringLiteral("Summaries model")));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries effort")));
    }

    // …but it is not an API key to hold, so it stays out of the keys modal entirely: that dialog
    // lists the subscription/aggregator/payg groups and a local row's group is "local".
    void aLocalEndpointIsAbsentFromTheKeysDialog() {
        relay::KeysDialog dialog;
        dialog.setPresets(presetsWithLocal({QStringLiteral("kimi")}));
        auto *list = dialog.findChild<QTreeWidget *>();
        QVERIFY(list);
        QStringList ids;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *group = list->topLevelItem(i);
            QVERIFY(group->text(0) != QStringLiteral("On this machine"));
            for (int j = 0; j < group->childCount(); ++j)
                ids << group->child(j)->data(0, Qt::UserRole + 1).toString();
        }
        QVERIFY(!ids.contains(QStringLiteral("local:bonsai")));
        QVERIFY(ids.contains(QStringLiteral("kimi")));   // the keyed presets are still listed
    }

    // ----- Relay Free (the hosted provider, protocol 13.8/13.9) ----------------------------------

    // The keys modal lists Relay Free under "Included", ahead of the groups you pay for, and its
    // status column says there is nothing to add. The Add and Remove buttons have nothing to do
    // on that row; Test stays, because the worker makes one real call through the gateway; and the
    // link button is about the service, not a key page.
    void theIncludedGroupComesFirstAndItsRowHoldsNoKey() {
        relay::KeysDialog dialog;
        dialog.setPresets(presetsWithHosted({QStringLiteral("kimi")}));
        auto *list = dialog.findChild<QTreeWidget *>();
        QVERIFY(list);
        QCOMPARE(groupTitles(list).first(), QStringLiteral("Included"));
        QVERIFY(groupTitles(list).contains(QStringLiteral("Pay-as-you-go")));
        QTreeWidgetItem *row = keysRow(list, QStringLiteral("relay-free"));
        QVERIFY(row);
        QCOMPARE(row->text(0), QStringLiteral("Relay Free"));
        QCOMPARE(row->text(1), QStringLiteral("Included · no key needed"));
        list->setCurrentItem(row);
        QPushButton *add = buttonNamed(dialog, QStringLiteral("Add / replace…"));
        QPushButton *remove = buttonNamed(dialog, QStringLiteral("Remove"));
        QPushButton *test = buttonNamed(dialog, QStringLiteral("Test"));
        QPushButton *about = buttonNamed(dialog, QStringLiteral("About Relay Free…"));
        QVERIFY(add && remove && test && about);
        QVERIFY(!add->isEnabled());
        QVERIFY(!remove->isEnabled());
        QVERIFY(test->isEnabled());
        QVERIFY(about->isEnabled());
        QVERIFY(!buttonNamed(dialog, QStringLiteral("Get a key…")));
        // Moving to an ordinary row brings the key buttons back (Remove stays off here only because
        // the test's kimi row carries no key_source, which reads as "Not set").
        list->setCurrentItem(keysRow(list, QStringLiteral("kimi")));
        QVERIFY(add->isEnabled());
        QVERIFY(buttonNamed(dialog, QStringLiteral("Get a key…")));
        QVERIFY(!buttonNamed(dialog, QStringLiteral("About Relay Free…")));
    }

    // The status column carries today's allowance: from the `presets` row's quota, then live from
    // every hosted_quota event, which is what the worker sends after each gateway call.
    void theIncludedRowShowsTheAllowanceLeftToday() {
        relay::KeysDialog dialog;
        dialog.setPresets(presetsWithHosted({}, true, quota(250000, 67500)));
        auto *list = dialog.findChild<QTreeWidget *>();
        QCOMPARE(keysRow(list, QStringLiteral("relay-free"))->text(1), QStringLiteral("Included · 73% left today"));
        dialog.handleEvent({{QStringLiteral("event"), QStringLiteral("hosted_quota")},
                            {QStringLiteral("limit"), 250000}, {QStringLiteral("used"), 240000},
                            {QStringLiteral("resets_at"), 1800000000}});
        QCOMPARE(keysRow(list, QStringLiteral("relay-free"))->text(1), QStringLiteral("Included · 4.0% left today"));
        // A later presets event keeps the live figure over the row's stale one.
        dialog.setPresets(presetsWithHosted({}, true, quota(250000, 67500)));
        QCOMPARE(keysRow(list, QStringLiteral("relay-free"))->text(1), QStringLiteral("Included · 4.0% left today"));
    }

    // Without python3-cryptography the worker cannot register with the gateway: the row says so and
    // is not offered as a provider anywhere.
    void anUnavailableIncludedRowSaysWhatItNeeds() {
        relay::KeysDialog keys;
        keys.setPresets(presetsWithHosted({QStringLiteral("kimi")}, false));
        auto *list = keys.findChild<QTreeWidget *>();
        QCOMPARE(keysRow(list, QStringLiteral("relay-free"))->text(1), QStringLiteral("Needs python3-cryptography"));
        RolesDialog roles;
        open(roles, presetsWithHosted({QStringLiteral("kimi")}, false));
        pin(roles);
        QCOMPARE(comboNamed(roles, QStringLiteral("Summaries provider"))->findData(QStringLiteral("relay-free")), -1);
        QCOMPARE(comboNamed(roles, QStringLiteral("Vision provider"))->findData(QStringLiteral("relay-free")), -1);
    }

    // The dialog offers Relay Free like any reachable provider — named as the company, never as
    // "(no key)" — and a job can be pinned to it.
    void theRolesModalOffersRelayFreeWithoutAKey() {
        RolesDialog dialog;
        open(dialog, presetsWithHosted({QStringLiteral("kimi")}), QStringLiteral("relay-free"));
        pin(dialog);
        // The pane runs on Relay Free, so that is where the job's own provider starts.
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("preset")), QStringLiteral("relay-free"));
        QComboBox *box = comboNamed(dialog, QStringLiteral("Summaries provider"));
        QCOMPARE(itemsOf(box), QStringList({QStringLiteral("Relay"), QStringLiteral("Kimi")}));
        QCOMPARE(box->currentText(), QStringLiteral("Relay"));
        for (const QString &item : itemsOf(box)) QVERIFY(!item.contains(QStringLiteral("no key")));
    }

    // The old two-step "Pin to a model…" wizard is gone: there is one way in, and it is the row.
    void thereIsNoPinToAModelWizardLeft() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi")}));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (QComboBox *box : dialog.findChildren<QComboBox *>())
            QVERIFY(!itemsOf(box).contains(QStringLiteral("Pin to a model…")));
    }

    // ----- the model of a pinned job ---------------------------------------------------------------

    // A pinned job's model is picked from that provider's own catalog — the `models` its preset row
    // carries — instead of typed into a Model… box. The list shows the labels and stores the ids.
    void aPinnedJobsModelIsPickedFromThatProvidersCatalog() {
        RolesDialog dialog;
        int changed = 0;
        dialog.onRolesChanged = [&changed] { ++changed; };
        open(dialog, with(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}),
                          QStringLiteral("glm-coding"), {{QStringLiteral("models"), glmModels()}}));
        pin(dialog);
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("glm-coding"));
        QVERIFY(!modelButton(dialog, QStringLiteral("Summaries model")));   // no free-text box
        QComboBox *model = comboNamed(dialog, QStringLiteral("Summaries model"));
        QVERIFY(model);
        QCOMPARE(itemsOf(model), QStringList({QStringLiteral("provider default (GLM 5.3)"),
                                              QStringLiteral("GLM 5.3"), QStringLiteral("GLM 5.3 Flash")}));
        QCOMPARE(model->currentIndex(), 0);
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("model")));
        changed = 0;
        pick(model, QStringLiteral("glm-5.3-flash"));
        QCOMPARE(changed, 1);
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("model")), QStringLiteral("glm-5.3-flash"));
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries model"))->currentText(), QStringLiteral("GLM 5.3 Flash"));
        // Back to the provider's default: the key goes rather than holding an empty string.
        pick(comboNamed(dialog, QStringLiteral("Summaries model")), QString());
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("model")));
        // Another provider's models are another list, so moving the job drops the stored id.
        pick(comboNamed(dialog, QStringLiteral("Summaries model")), QStringLiteral("glm-5.3-flash"));
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("kimi"));
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("model")));
    }

    // An id typed before the catalog existed is still what the job runs on, so it is listed as
    // itself rather than the box reading "provider default".
    void aStoredModelTheCatalogDoesNotListIsStillShown() {
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("preset")),
                             QStringLiteral("glm-coding"));
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("model")),
                             QStringLiteral("glm-4-legacy"));
        RolesDialog dialog;
        open(dialog, with(presets({QStringLiteral("glm-coding")}), QStringLiteral("glm-coding"),
                          {{QStringLiteral("models"), glmModels()}}));
        QComboBox *model = comboNamed(dialog, QStringLiteral("Summaries model"));
        QVERIFY(model);
        QCOMPARE(model->currentText(), QStringLiteral("glm-4-legacy"));
        QCOMPARE(model->count(), 4);
    }

    // The free-text fallback is only for a provider that sent no `models`: then it is Model… and a
    // typed id, stored for that job alone.
    void aProviderThatSentNoModelsFallsBackToATypedId() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}));
        QVERIFY(!modelButton(dialog, QStringLiteral("Summaries model")));   // waits for a provider
        pin(dialog);
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("glm-coding"));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries model")));
        QPushButton *edit = modelButton(dialog, QStringLiteral("Summaries model"));
        QVERIFY(edit);
        QVERIFY(edit->isEnabled());
        answerModelPrompt(QStringLiteral("glm-5.3-flash"));
        edit->click();
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("model")), QStringLiteral("glm-5.3-flash"));
        // Cancelling the prompt changes nothing.
        answerModelPrompt(QStringLiteral("something-else"), false);
        modelButton(dialog, QStringLiteral("Summaries model"))->click();
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("model")), QStringLiteral("glm-5.3-flash"));
        // Moving the job back to a tier takes the hand-picked model with it.
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("flash"));
        QVERIFY(!has(QStringLiteral("summaries"), QStringLiteral("model")));
    }

    // ----- reasoning effort lists -------------------------------------------------------------

    // Owner, 2026-09-20: "for codex planning you pick xhigh, not max; for glm 5.3 you pick max". The
    // box shows the provider's own word for each level (`effort_labels`) and stores Relay's level, so
    // the setting still means something when the job moves to another provider.
    void anEffortBoxShowsTheProvidersOwnWordAndStoresTheRelayLevel() {
        const QJsonArray levels{QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
        const QJsonObject words{{QStringLiteral("low"), QStringLiteral("low")},
                                {QStringLiteral("high"), QStringLiteral("high")},
                                {QStringLiteral("max"), QStringLiteral("xhigh")}};
        RolesDialog dialog;
        open(dialog, with(presets({QStringLiteral("kimi"), QStringLiteral("openai")}), QStringLiteral("openai"),
                          {{QStringLiteral("efforts"), levels}, {QStringLiteral("effort_labels"), words}}));
        pin(dialog);
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("openai"));
        QComboBox *effort = comboNamed(dialog, QStringLiteral("Summaries effort"));
        QVERIFY(effort);
        QCOMPARE(itemsOf(effort), QStringList({QStringLiteral("model default"), QStringLiteral("low"),
                                               QStringLiteral("high"), QStringLiteral("xhigh")}));
        QVERIFY(!itemsOf(effort).contains(QStringLiteral("max")));
        const int xhigh = effort->findText(QStringLiteral("xhigh"));
        QCOMPARE(effort->itemData(xhigh).toString(), QStringLiteral("max"));
        QVERIFY(effort->itemData(xhigh, Qt::ToolTipRole).toString().contains(QStringLiteral("max")));
        effort->setCurrentIndex(xhigh);
        Q_EMIT effort->activated(xhigh);
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("effort")), QStringLiteral("max"));
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries effort"))->currentText(), QStringLiteral("xhigh"));
        // The same stored level on a provider with no word of its own reads as Relay's: Kimi offers
        // low and high, so max comes down to high, shown as "high".
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("kimi"));
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries effort"))->currentText(), QStringLiteral("high"));
    }

    // The picked model's own catalog row decides, where it has one: its levels and its words win over
    // the provider's, and a model with no levels at all gets no effort box.
    void thePickedModelsOwnLevelsAndWordsWinOverTheProviders() {
        const QJsonArray models{
            modelRow(QStringLiteral("model-of-openai"), QStringLiteral("GPT-6 Astra"),
                     {QStringLiteral("high"), QStringLiteral("max")},
                     {{QStringLiteral("high"), QStringLiteral("high")}, {QStringLiteral("max"), QStringLiteral("xhigh")}}),
            modelRow(QStringLiteral("gpt-6-mini"), QStringLiteral("GPT-6 mini"), {})};
        RolesDialog dialog;
        open(dialog, with(presets({QStringLiteral("kimi"), QStringLiteral("openai")}), QStringLiteral("openai"),
                          {{QStringLiteral("models"), models}}));
        pin(dialog);
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("openai"));
        // The provider default is the first row, so its levels apply — not the preset's low/high.
        QCOMPARE(itemsOf(comboNamed(dialog, QStringLiteral("Summaries effort"))),
                 QStringList({QStringLiteral("model default"), QStringLiteral("high"), QStringLiteral("xhigh")}));
        pick(comboNamed(dialog, QStringLiteral("Summaries model")), QStringLiteral("gpt-6-mini"));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries effort")));
    }

    // The levels are the provider's own (presets.py `effort_levels`), and a level stored while some
    // other provider was in use lands on the nearest one offered — never silently on "model default",
    // which read as "no reasoning setting" when the job was in fact running at high.
    void aStoredEffortTheProviderDoesNotOfferShowsTheNearestOneOffered() {
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("tier")),
                             QStringLiteral("flash"));
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("effort")),
                             QStringLiteral("medium"));
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi")}));
        // Kimi offers low and high; medium is one step from each, and a tie goes up.
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries effort"))->currentText(), QStringLiteral("high"));
    }

    // Relay Free caps at medium, so a stored "max" comes down to it rather than disappearing.
    void aLevelAboveTheProvidersCapComesDownToTheCap() {
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("tier")),
                             QStringLiteral("flash"));
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("effort")),
                             QStringLiteral("max"));
        RolesDialog dialog;
        open(dialog, withEfforts(presets({QStringLiteral("kimi")}), QStringLiteral("kimi"),
                                 {QStringLiteral("low"), QStringLiteral("medium")},
                                 QStringLiteral("high and max are sent as medium.")));
        QComboBox *effort = comboNamed(dialog, QStringLiteral("Summaries effort"));
        QCOMPARE(itemsOf(effort), QStringList({QStringLiteral("model default"), QStringLiteral("low"),
                                               QStringLiteral("medium")}));
        QCOMPARE(effort->currentText(), QStringLiteral("medium"));
        // And the provider's one-line note is on the picker, so the fold is not a mystery.
        QVERIFY(effort->toolTip().contains(QStringLiteral("high and max are sent as medium.")));
    }

    // A provider with no effort knob at all (`efforts` empty) gets no picker rather than an empty one.
    void aProviderWithNoEffortKnobGetsNoEffortPicker() {
        RolesDialog dialog;
        open(dialog, withEfforts(presets({QStringLiteral("kimi")}), QStringLiteral("kimi"), {}));
        QVERIFY(comboNamed(dialog, QStringLiteral("Summaries")));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Summaries effort")));
    }

    // A job with neither a tier nor a provider of its own has no entry in the `roles` object at all
    // (Pane::rolesObject), so an effort stored for it would never be sent: the box waits instead of
    // accepting a setting that does nothing.
    void aJobsEffortWaitsUntilTheRowNamesATierOrAProvider() {
        RolesDialog dialog;
        open(dialog, presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}));
        QComboBox *effort = comboNamed(dialog, QStringLiteral("Summaries effort"));
        QVERIFY(effort);
        QVERIFY(!effort->isEnabled());
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("flash"));
        effort = comboNamed(dialog, QStringLiteral("Summaries effort"));
        QVERIFY(effort->isEnabled());
        QCOMPARE(itemsOf(effort), QStringList({QStringLiteral("model default"), QStringLiteral("low"),
                                               QStringLiteral("high")}));
        pick(effort, QStringLiteral("low"));
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("effort")), QStringLiteral("low"));
        // The tier stays: an effort is a refinement of the row's target, not a target of its own.
        QCOMPARE(stored(QStringLiteral("summaries"), QStringLiteral("tier")), QStringLiteral("flash"));
        // A provider of the job's own is a target too.
        pin(dialog);
        QVERIFY(comboNamed(dialog, QStringLiteral("Summaries effort"))->isEnabled());
    }

    // ----- vision ----------------------------------------------------------------------------------

    // The vision row follows no tier: its first box is a provider, Automatic until one is picked, and
    // its model waits for that provider exactly as a job's does.
    void theVisionRowPicksItsProviderInline() {
        RolesDialog dialog;
        open(dialog, with(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}),
                          QStringLiteral("glm-coding"), {{QStringLiteral("models"), glmModels()}}));
        QComboBox *provider = comboNamed(dialog, QStringLiteral("Vision provider"));
        QVERIFY(provider);
        QCOMPARE(itemsOf(provider), QStringList({QStringLiteral("Automatic"), QStringLiteral("Kimi"),
                                                 QStringLiteral("Z.AI (GLM)")}));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Vision model")));
        QVERIFY(!modelButton(dialog, QStringLiteral("Vision model")));
        // Kimi sent no catalog: a typed id.
        pick(provider, QStringLiteral("kimi"));
        QCOMPARE(stored(QStringLiteral("vision"), QStringLiteral("preset")), QStringLiteral("kimi"));
        QVERIFY(modelButton(dialog, QStringLiteral("Vision model")));
        // Z.AI sent one: a list.
        pick(comboNamed(dialog, QStringLiteral("Vision provider")), QStringLiteral("glm-coding"));
        QVERIFY(!modelButton(dialog, QStringLiteral("Vision model")));
        pick(comboNamed(dialog, QStringLiteral("Vision model")), QStringLiteral("glm-5.3-flash"));
        QCOMPARE(stored(QStringLiteral("vision"), QStringLiteral("model")), QStringLiteral("glm-5.3-flash"));
        QVERIFY(!has(QStringLiteral("vision"), QStringLiteral("tier")));
        pick(comboNamed(dialog, QStringLiteral("Vision provider")), QString());
        QVERIFY(!has(QStringLiteral("vision"), QStringLiteral("preset")));
        QVERIFY(!has(QStringLiteral("vision"), QStringLiteral("model")));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Vision model")));
    }

    // The worker's job list names "vision" too; it gets the vision row, not a second, tier-shaped one.
    void visionIsNotRepeatedAsAJobRow() {
        QJsonArray jobs = actions();
        jobs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("vision")},
                                {QStringLiteral("label"), QStringLiteral("Images")},
                                {QStringLiteral("hint"), QStringLiteral("reading pictures")},
                                {QStringLiteral("tier"), QStringLiteral("flash")},
                                {QStringLiteral("settable"), true}});
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), jobs);
        QVERIFY(!comboNamed(dialog, QStringLiteral("Images")));
        QVERIFY(comboNamed(dialog, QStringLiteral("Vision provider")));
    }
};

QTEST_MAIN(ModelSettingsTests)
#include "modelsettings_test.moc"
