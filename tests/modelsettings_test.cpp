// SPDX-License-Identifier: GPL-3.0-or-later
//
// The roles modal picks a *provider*, so its lists name companies ("Kimi", "Z.AI (GLM)") and offer
// only providers whose key is actually stored. Owner, 2026-09-18: "in model roles, it shouldn't show
// options where you don't have a key assigned … it should not say the model name".
#include "ModelSettings.h"

#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

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

QJsonObject tierSpec(const QString &id, const QString &label) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("hint"), QStringLiteral("what it is for")}};
}

QJsonObject catalog() {
    return {{QStringLiteral("tiers"), QJsonArray{tierSpec(QStringLiteral("main"), QStringLiteral("Main")),
                                                 tierSpec(QStringLiteral("flash"), QStringLiteral("Flash")),
                                                 tierSpec(QStringLiteral("lite"), QStringLiteral("Lite"))}},
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
};

QTEST_MAIN(ModelSettingsTests)
#include "modelsettings_test.moc"
