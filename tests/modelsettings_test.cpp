// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The roles modal picks a *provider*, so its lists name companies ("Kimi", "Z.AI (GLM)") and offer
// only providers whose key is actually stored. Owner, 2026-09-18: "in model roles, it shouldn't show
// options where you don't have a key assigned … it should not say the model name".
#include "ModelSettings.h"

#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
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
            {QStringLiteral("hint"), QStringLiteral("what it is for")}};
}

QJsonObject catalog() {
    return {{QStringLiteral("tiers"), QJsonArray{tierSpec(QStringLiteral("main"), QStringLiteral("Main")),
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

// The tier rows' provider combos, in row order; the default-provider box is told apart by its name.
// A rebuild retires the old rows with deleteLater, so those have to be collected first or they are
// still children of the dialog and answer findChildren.
QList<QComboBox *> tierProviderBoxes(const RolesDialog &dialog) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QList<QComboBox *> out;
    for (QComboBox *box : dialog.findChildren<QComboBox *>()) {
        if (box->accessibleName() == QLatin1String("Default provider")) continue;
        if (box->count() > 0 && box->itemText(0) == QLatin1String("Default provider")) out << box;
    }
    return out;
}

// The Local tier's own combo (card #JH22): it lists saved local endpoints, not providers, so it
// carries its own accessible name and never appears among the provider boxes above.
QComboBox *localTierBox(const RolesDialog &dialog) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    for (QComboBox *box : dialog.findChildren<QComboBox *>())
        if (box->accessibleName() == QLatin1String("Local model")) return box;
    return nullptr;
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

// One job for the Advanced list, as roles.py action_catalog() sends it.
QJsonArray actions(const QString &tier = QStringLiteral("flash")) {
    return {QJsonObject{{QStringLiteral("role"), QStringLiteral("summaries")},
                        {QStringLiteral("label"), QStringLiteral("Summaries")},
                        {QStringLiteral("hint"), QStringLiteral("condensing")},
                        {QStringLiteral("tier"), tier},
                        {QStringLiteral("settable"), true}}};
}

// The rows are rebuilt on every change, and the retired widgets only go away when the deferred
// deletes run — so every lookup flushes them first, exactly as tierProviderBoxes() does.
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

private Q_SLOTS:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("RelayTerminalTest"));
        QCoreApplication::setApplicationName(QStringLiteral("ModelSettingsTest"));
    }

    void init() { QSettings().clear(); }

    void defaultProviderOffersOnlyProvidersWithAKeyAndNamesTheCompany() {
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        auto *box = dialog.findChild<QComboBox *>();
        QVERIFY(box);
        QCOMPARE(box->accessibleName(), QStringLiteral("Default provider"));
        QCOMPARE(itemsOf(box), QStringList({QStringLiteral("Kimi"), QStringLiteral("Z.AI (GLM)")}));
        QCOMPARE(box->currentText(), QStringLiteral("Kimi"));
        // No model ids, and no rows for providers with no key.
        for (const QString &item : itemsOf(box)) {
            QVERIFY(!item.contains(QStringLiteral("K3")));
            QVERIFY(!item.contains(QStringLiteral("GPT")));
            QVERIFY(!item.contains(QStringLiteral("no key")));
        }
    }

    void twoPlansFromOneCompanyAreToldApartByThePlan() {
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("kimi-code")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        auto *box = dialog.findChild<QComboBox *>();
        QCOMPARE(itemsOf(box), QStringList({QStringLiteral("Kimi · Pay-as-you-go"),
                                            QStringLiteral("Kimi · Coding Plan")}));
    }

    void withNoKeysAtAllEveryProviderIsStillOfferedSoTheDialogIsNotEmpty() {
        RolesDialog dialog;
        dialog.setPresets(presets({}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        auto *box = dialog.findChild<QComboBox *>();
        QCOMPARE(box->count(), 4);
        QVERIFY(box->itemText(0).startsWith(QStringLiteral("Kimi")));
        QVERIFY(box->itemText(0).endsWith(QStringLiteral("(no key)")));
    }

    void aTierOffersTheKeyedProvidersAndStoresTheOneChosen() {
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        const QList<QComboBox *> rows = tierProviderBoxes(dialog);
        QCOMPARE(rows.size(), 2);          // Flash and Lite; Main is the pane's own model
        QComboBox *flash = rows.first();
        QCOMPARE(itemsOf(flash), QStringList({QStringLiteral("Default provider"), QStringLiteral("Kimi"),
                                              QStringLiteral("Z.AI (GLM)")}));
        const int zai = flash->findData(QStringLiteral("glm-coding"));
        QVERIFY(zai > 0);
        flash->setCurrentIndex(zai);
        Q_EMIT flash->activated(zai);
        // A provider and no model: the worker turns that into that provider's Flash model
        // (presets.provider_tier_model), which is the whole point of picking a provider here.
        QCOMPARE(QSettings().value(RolesDialog::tierSetting(QStringLiteral("flash"),
                                                           QStringLiteral("preset"))).toString(),
                 QStringLiteral("glm-coding"));
        QVERIFY(!QSettings().contains(RolesDialog::tierSetting(QStringLiteral("flash"),
                                                              QStringLiteral("model"))));
    }

    void changingTheDefaultProviderKeepsATierPinnedToADifferentProvider() {
        QSettings().setValue(RolesDialog::tierSetting(QStringLiteral("flash"), QStringLiteral("preset")),
                             QStringLiteral("glm-coding"));
        QSettings().setValue(RolesDialog::tierSetting(QStringLiteral("lite"), QStringLiteral("preset")),
                             QStringLiteral("openai"));
        RolesDialog dialog;
        QString chosen;
        dialog.onProviderChosen = [&chosen](const QString &id) { chosen = id; };
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding"),
                                   QStringLiteral("openai")}), catalog(), {});
        dialog.setProvider(QStringLiteral("openai"));
        auto *box = dialog.findChild<QComboBox *>();
        const int kimi = box->findData(QStringLiteral("kimi"));
        QVERIFY(kimi >= 0);
        box->setCurrentIndex(kimi);
        Q_EMIT box->activated(kimi);
        QCOMPARE(chosen, QStringLiteral("kimi"));
        // Main moves to Kimi; Flash stays on Z.AI — that pairing is the reason the row exists.
        QCOMPARE(QSettings().value(RolesDialog::tierSetting(QStringLiteral("flash"),
                                                           QStringLiteral("preset"))).toString(),
                 QStringLiteral("glm-coding"));
        // Lite followed the provider being left behind, so it goes back to following the default.
        QVERIFY(!QSettings().contains(RolesDialog::tierSetting(QStringLiteral("lite"),
                                                              QStringLiteral("preset"))));
    }

    void aTierKeepsShowingAProviderWhoseKeyWentAway() {
        QSettings().setValue(RolesDialog::tierSetting(QStringLiteral("flash"), QStringLiteral("preset")),
                             QStringLiteral("glm-coding"));
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *flash = tierProviderBoxes(dialog).first();
        QCOMPARE(flash->currentText(), QStringLiteral("Z.AI (GLM)  (no key)"));
    }

    // A model server on this machine has no key and needs none, so the roles modal offers it like
    // any reachable provider — and never as "(no key)", which would read as something to fix.
    void aLocalEndpointIsChoosableWithoutAStoredKey() {
        RolesDialog dialog;
        dialog.setPresets(presetsWithLocal({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        auto *box = dialog.findChild<QComboBox *>();
        QVERIFY(box->findData(QStringLiteral("local:bonsai")) >= 0);
        QVERIFY(itemsOf(box).contains(QStringLiteral("Bonsai 2 27B")));
        for (const QString &item : itemsOf(box)) QVERIFY(!item.contains(QStringLiteral("no key")));
        // And a tier can be pinned to it too.
        QComboBox *flash = tierProviderBoxes(dialog).first();
        const int local = flash->findData(QStringLiteral("local:bonsai"));
        QVERIFY(local > 0);
        flash->setCurrentIndex(local);
        Q_EMIT flash->activated(local);
        QCOMPARE(QSettings().value(RolesDialog::tierSetting(QStringLiteral("flash"),
                                                            QStringLiteral("preset"))).toString(),
                 QStringLiteral("local:bonsai"));
    }

    // The fourth tier (card #JH22). It belongs to no provider, so its row offers the saved local
    // endpoints and nothing else — no hosted provider, no model box, no effort box.
    void theLocalTierRowListsOnlyLocalEndpointsAndStoresTheOneChosen() {
        RolesDialog dialog;
        dialog.setPresets(presetsWithLocal({QStringLiteral("kimi"), QStringLiteral("glm-coding")}),
                          catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *local = localTierBox(dialog);
        QVERIFY(local);
        QCOMPARE(itemsOf(local), QStringList({QStringLiteral("Bonsai 2 27B")}));
        QVERIFY(local->isEnabled());
        // The hosted providers the other rows offer are not choices here.
        QCOMPARE(local->findData(QStringLiteral("kimi")), -1);
        QCOMPARE(local->findData(QStringLiteral("glm-coding")), -1);
        local->setCurrentIndex(0);
        Q_EMIT local->activated(0);
        QCOMPARE(QSettings().value(RolesDialog::tierSetting(QStringLiteral("local"),
                                                            QStringLiteral("preset"))).toString(),
                 QStringLiteral("local:bonsai"));
        // And the Local row is not one of the provider rows: those are still Flash and Lite only.
        QCOMPARE(tierProviderBoxes(dialog).size(), 2);
    }

    void theLocalTierRowIsDisabledWhenThisMachineServesNothing() {
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *local = localTierBox(dialog);
        QVERIFY(local);
        QCOMPARE(itemsOf(local), QStringList({QStringLiteral("No local model is set up.")}));
        QVERIFY(!local->isEnabled());
        QVERIFY(!QSettings().contains(RolesDialog::tierSetting(QStringLiteral("local"),
                                                               QStringLiteral("preset"))));
    }

    // Every job in Advanced can follow the Local tier too, by the same list the tier rows use.
    void anAdvancedRowOffersTheLocalTier() {
        const QJsonArray actions{QJsonObject{{QStringLiteral("role"), QStringLiteral("summaries")},
                                             {QStringLiteral("label"), QStringLiteral("Summaries")},
                                             {QStringLiteral("hint"), QStringLiteral("condensing")},
                                             {QStringLiteral("tier"), QStringLiteral("flash")},
                                             {QStringLiteral("settable"), true}}};
        QSettings().setValue(QStringLiteral("roles/advanced_open"), true);
        RolesDialog dialog;
        dialog.setPresets(presetsWithLocal({QStringLiteral("kimi")}), catalog(), actions);
        dialog.setProvider(QStringLiteral("kimi"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QComboBox *choice = nullptr;
        for (QComboBox *box : dialog.findChildren<QComboBox *>())
            if (box->accessibleName() == QLatin1String("Summaries")) choice = box;
        QVERIFY(choice);
        const int local = choice->findData(QStringLiteral("local"));
        QVERIFY(local > 0);
        choice->setCurrentIndex(local);
        Q_EMIT choice->activated(local);
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                            QStringLiteral("tier"))).toString(),
                 QStringLiteral("local"));
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
        roles.setPresets(presetsWithHosted({QStringLiteral("kimi")}, false), catalog(), {});
        roles.setProvider(QStringLiteral("kimi"));
        auto *box = roles.findChild<QComboBox *>();
        QCOMPARE(box->findData(QStringLiteral("relay-free")), -1);
    }

    // The roles modal offers Relay Free like any reachable provider — named as the company, never
    // as "(no key)" — and a tier can be pinned to it.
    void theRolesModalOffersRelayFreeWithoutAKey() {
        RolesDialog dialog;
        dialog.setPresets(presetsWithHosted({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("relay-free"));
        auto *box = dialog.findChild<QComboBox *>();
        QCOMPARE(box->accessibleName(), QStringLiteral("Default provider"));
        QCOMPARE(itemsOf(box), QStringList({QStringLiteral("Relay"), QStringLiteral("Kimi")}));
        QCOMPARE(box->currentText(), QStringLiteral("Relay"));
        for (const QString &item : itemsOf(box)) QVERIFY(!item.contains(QStringLiteral("no key")));
        QComboBox *flash = tierProviderBoxes(dialog).first();
        const int hosted = flash->findData(QStringLiteral("relay-free"));
        QVERIFY(hosted > 0);
        flash->setCurrentIndex(hosted);
        Q_EMIT flash->activated(hosted);
        QCOMPARE(QSettings().value(RolesDialog::tierSetting(QStringLiteral("flash"),
                                                            QStringLiteral("preset"))).toString(),
                 QStringLiteral("relay-free"));
    }

    // ----- the Main row (owner, 2026-09-18: "you also still cant pick the main model options") -----

    // Main used to be a dead label reading "this pane's model". It now carries the same three
    // controls as Flash and Lite, and says whose provider it is using rather than implying a private
    // override: the Main tier is the pane, so its provider is the default one.
    void theMainRowCarriesAModelAProviderAndAnEffort() {
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QVERIFY(modelButton(dialog, QStringLiteral("Main model")));
        QVERIFY(modelButton(dialog, QStringLiteral("Main model"))->isEnabled());
        QComboBox *provider = comboNamed(dialog, QStringLiteral("Main provider"));
        QVERIFY(provider);
        // The same list as the box at the top, and no "Default provider" entry: Main *is* the default.
        QCOMPARE(itemsOf(provider), QStringList({QStringLiteral("Kimi"), QStringLiteral("Z.AI (GLM)")}));
        QCOMPARE(provider->currentText(), QStringLiteral("Kimi"));
        // …so it is not one of the per-tier override boxes either.
        QCOMPARE(tierProviderBoxes(dialog).size(), 2);
        QComboBox *effort = comboNamed(dialog, QStringLiteral("Main effort"));
        QVERIFY(effort);
        // No "Model default": the pane always runs at some level, and that level is `agent/effort`.
        QCOMPARE(itemsOf(effort), QStringList({QStringLiteral("low"), QStringLiteral("high")}));
    }

    void theMainRowsProviderIsTheDefaultProviderAndSwitchingItSwitchesBoth() {
        RolesDialog dialog;
        QString chosen;
        dialog.onProviderChosen = [&chosen](const QString &id) { chosen = id; };
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        pick(comboNamed(dialog, QStringLiteral("Main provider")), QStringLiteral("glm-coding"));
        QCOMPARE(chosen, QStringLiteral("glm-coding"));
        QCOMPARE(dialog.findChild<QComboBox *>()->currentText(), QStringLiteral("Z.AI (GLM)"));
        // And nothing was written under tiers/main: roles.py rejects `tiers.main` on purpose.
        QVERIFY(!QSettings().contains(RolesDialog::tierSetting(QStringLiteral("main"),
                                                               QStringLiteral("preset"))));
    }

    // The row acts through the pane, which is the only thing that can change the pane's own model:
    // the dialog itself writes nothing, not even `provider/model` (Pane::setMainModel does that).
    void theMainRowsModelAndEffortGoBackToThePane() {
        RolesDialog dialog;
        QString model, level;
        int models = 0;
        dialog.onMainModelChosen = [&model, &models](const QString &value) { model = value; ++models; };
        dialog.onMainEffortChosen = [&level](const QString &value) { level = value; };
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        answerModelPrompt(QStringLiteral("kimi-k3-turbo"));
        modelButton(dialog, QStringLiteral("Main model"))->click();
        QCOMPARE(models, 1);
        QCOMPARE(model, QStringLiteral("kimi-k3-turbo"));
        QVERIFY(!QSettings().contains(QStringLiteral("provider/model")));
        QVERIFY(!QSettings().contains(RolesDialog::tierSetting(QStringLiteral("main"),
                                                               QStringLiteral("model"))));
        pick(comboNamed(dialog, QStringLiteral("Main effort")), QStringLiteral("high"));
        QCOMPARE(level, QStringLiteral("high"));
        QVERIFY(!QSettings().contains(RolesDialog::tierSetting(QStringLiteral("main"),
                                                               QStringLiteral("effort"))));
    }

    // Cancelling the prompt changes nothing at all.
    void cancellingTheMainModelPromptTellsThePaneNothing() {
        RolesDialog dialog;
        int models = 0;
        dialog.onMainModelChosen = [&models](const QString &) { ++models; };
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        answerModelPrompt(QStringLiteral("kimi-k3-turbo"), false);
        modelButton(dialog, QStringLiteral("Main model"))->click();
        QCOMPARE(models, 0);
    }

    // ----- reasoning effort lists -------------------------------------------------------------

    // The levels are the provider's own (presets.py `effort_levels`), and a level stored while some
    // other provider was in use lands on the nearest one offered — never silently on "Model default",
    // which read as "no reasoning setting" when the pane was in fact running at high.
    void aStoredEffortTheProviderDoesNotOfferShowsTheNearestOneOffered() {
        QSettings().setValue(QStringLiteral("agent/effort"), QStringLiteral("medium"));
        QSettings().setValue(RolesDialog::tierSetting(QStringLiteral("flash"), QStringLiteral("effort")),
                             QStringLiteral("medium"));
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        // Kimi offers low and high; medium is one step from each, and a tie goes up.
        QCOMPARE(comboNamed(dialog, QStringLiteral("Main effort"))->currentText(), QStringLiteral("high"));
        QCOMPARE(comboNamed(dialog, QStringLiteral("Flash effort"))->currentText(), QStringLiteral("high"));
    }

    // Relay Free caps at medium, so a stored "max" comes down to it rather than disappearing.
    void aLevelAboveTheProvidersCapComesDownToTheCap() {
        QSettings().setValue(QStringLiteral("agent/effort"), QStringLiteral("max"));
        RolesDialog dialog;
        dialog.setPresets(withEfforts(presets({QStringLiteral("kimi")}), QStringLiteral("kimi"),
                                      {QStringLiteral("low"), QStringLiteral("medium")},
                                      QStringLiteral("high and max are sent as medium.")),
                          catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *effort = comboNamed(dialog, QStringLiteral("Main effort"));
        QCOMPARE(itemsOf(effort), QStringList({QStringLiteral("low"), QStringLiteral("medium")}));
        QCOMPARE(effort->currentText(), QStringLiteral("medium"));
        // And the provider's one-line note is on the picker, so the fold is not a mystery.
        QVERIFY(effort->toolTip().contains(QStringLiteral("high and max are sent as medium.")));
        QVERIFY(comboNamed(dialog, QStringLiteral("Flash effort"))
                    ->toolTip().contains(QStringLiteral("high and max are sent as medium.")));
    }

    // A provider with no effort knob at all (`efforts` empty) gets no picker rather than an empty one.
    void aProviderWithNoEffortKnobGetsNoEffortPicker() {
        RolesDialog dialog;
        dialog.setPresets(withEfforts(presets({QStringLiteral("kimi")}), QStringLiteral("kimi"), {}),
                          catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Main effort")));
        QVERIFY(!comboNamed(dialog, QStringLiteral("Flash effort")));
    }

    // ----- Advanced: a provider per job ---------------------------------------------------------

    // Owner, 2026-09-18: "i think advanced options should be separate from the providers. i might
    // want to pick kimi k3 for main agents and glm 5.3 flash for subagents". So a job's provider is
    // a box beside its tier, not a wizard behind it, and the two are exclusive.
    void anAdvancedRowPicksItsOwnProviderInlineAndDropsTheTier() {
        QSettings().setValue(QStringLiteral("roles/advanced_open"), true);
        QSettings().setValue(RolesDialog::roleSetting(QStringLiteral("summaries"), QStringLiteral("tier")),
                             QStringLiteral("flash"));
        RolesDialog dialog;
        int changed = 0;
        dialog.onRolesChanged = [&changed] { ++changed; };
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), actions());
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *provider = comboNamed(dialog, QStringLiteral("Summaries provider"));
        QVERIFY(provider);
        QCOMPARE(itemsOf(provider), QStringList({QStringLiteral("Same as tier"), QStringLiteral("Kimi"),
                                                 QStringLiteral("Z.AI (GLM)")}));
        QCOMPARE(provider->currentText(), QStringLiteral("Same as tier"));
        pick(provider, QStringLiteral("glm-coding"));
        QVERIFY(changed > 0);
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                            QStringLiteral("preset"))).toString(),
                 QStringLiteral("glm-coding"));
        // Protocol 13.7 takes a tier or an endpoint, never both.
        QVERIFY(!QSettings().contains(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                               QStringLiteral("tier"))));
        // The tier box says so rather than pretending the job still follows Flash.
        QCOMPARE(comboNamed(dialog, QStringLiteral("Summaries"))->currentText(), QStringLiteral("Own provider"));
        // And back: naming a tier drops the job's own endpoint.
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("lite"));
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                            QStringLiteral("tier"))).toString(),
                 QStringLiteral("lite"));
        QVERIFY(!QSettings().contains(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                               QStringLiteral("preset"))));
    }

    // The old two-step "Pin to a model…" wizard is gone: there is one way in, and it is the row.
    void thereIsNoPinToAModelWizardLeft() {
        QSettings().setValue(QStringLiteral("roles/advanced_open"), true);
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi")}), catalog(), actions());
        dialog.setProvider(QStringLiteral("kimi"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (QComboBox *box : dialog.findChildren<QComboBox *>())
            QVERIFY(!itemsOf(box).contains(QStringLiteral("Pin to a model…")));
    }

    // A model id is read against a provider, so the button waits for one; then it stores the id for
    // that job alone.
    void anAdvancedRowsModelWaitsForAProviderOfItsOwn() {
        QSettings().setValue(QStringLiteral("roles/advanced_open"), true);
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), actions());
        dialog.setProvider(QStringLiteral("kimi"));
        QVERIFY(!modelButton(dialog, QStringLiteral("Summaries model"))->isEnabled());
        pick(comboNamed(dialog, QStringLiteral("Summaries provider")), QStringLiteral("glm-coding"));
        QPushButton *edit = modelButton(dialog, QStringLiteral("Summaries model"));
        QVERIFY(edit->isEnabled());
        answerModelPrompt(QStringLiteral("glm-5.3-flash"));
        edit->click();
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                            QStringLiteral("model"))).toString(),
                 QStringLiteral("glm-5.3-flash"));
        // Moving the job back to a tier takes the hand-picked model with it.
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("flash"));
        QVERIFY(!QSettings().contains(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                               QStringLiteral("model"))));
    }

    // A job with neither a tier nor a provider of its own has no entry in the `roles` object at all
    // (Pane::rolesObject), so an effort stored for it would never be sent: the box waits instead of
    // accepting a setting that does nothing.
    void anAdvancedRowsEffortWaitsUntilTheRowNamesATierOrAProvider() {
        QSettings().setValue(QStringLiteral("roles/advanced_open"), true);
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), actions());
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *effort = comboNamed(dialog, QStringLiteral("Summaries effort"));
        QVERIFY(effort);
        QVERIFY(!effort->isEnabled());
        pick(comboNamed(dialog, QStringLiteral("Summaries")), QStringLiteral("flash"));
        effort = comboNamed(dialog, QStringLiteral("Summaries effort"));
        QVERIFY(effort->isEnabled());
        QCOMPARE(itemsOf(effort), QStringList({QStringLiteral("Model default"), QStringLiteral("low"),
                                               QStringLiteral("high")}));
        pick(effort, QStringLiteral("low"));
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                            QStringLiteral("effort"))).toString(),
                 QStringLiteral("low"));
        // The tier stays: an effort is a refinement of the row's target, not a target of its own.
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("summaries"),
                                                            QStringLiteral("tier"))).toString(),
                 QStringLiteral("flash"));
    }

    // The vision row takes a provider the same way, which is what retired the wizard's last caller.
    void theVisionRowPicksItsProviderInline() {
        RolesDialog dialog;
        dialog.setPresets(presets({QStringLiteral("kimi"), QStringLiteral("glm-coding")}), catalog(), {});
        dialog.setProvider(QStringLiteral("kimi"));
        QComboBox *provider = comboNamed(dialog, QStringLiteral("Vision provider"));
        QVERIFY(provider);
        QCOMPARE(itemsOf(provider), QStringList({QStringLiteral("Automatic"), QStringLiteral("Kimi"),
                                                 QStringLiteral("Z.AI (GLM)")}));
        QVERIFY(!modelButton(dialog, QStringLiteral("Vision model"))->isEnabled());
        pick(provider, QStringLiteral("glm-coding"));
        QCOMPARE(QSettings().value(RolesDialog::roleSetting(QStringLiteral("vision"),
                                                            QStringLiteral("preset"))).toString(),
                 QStringLiteral("glm-coding"));
        QVERIFY(modelButton(dialog, QStringLiteral("Vision model"))->isEnabled());
        pick(comboNamed(dialog, QStringLiteral("Vision provider")), QString());
        QVERIFY(!QSettings().contains(RolesDialog::roleSetting(QStringLiteral("vision"),
                                                               QStringLiteral("preset"))));
    }
};

QTEST_MAIN(ModelSettingsTests)
#include "modelsettings_test.moc"
