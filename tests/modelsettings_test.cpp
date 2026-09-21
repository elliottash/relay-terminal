// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The keys modal, which is the only modal left in src/ModelSettings.h: a row per plan, grouped
// Included / Subscriptions / Aggregator / Pay-as-you-go, with Relay Free first and holding no key
// at all, and a model server on this machine never listed (it needs none).
//
// The per-job models modal was tested here too until 2026-09-21. It is retired with card #MDL1 —
// it is the models pane's **jobs** tab now (design 5.9) — and what was worth keeping of its cases
// moved to tests/jobstab_test.cpp: the storage keys a job writes, a provider that is offered
// without a stored key, and the levels a model's own list gives a row. The rest was about the
// tier box, the provider box and the "its own provider…" row, which the tab does not have.
#include "ModelSettings.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTreeWidget>

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

    // Without python3-cryptography the worker cannot register with the gateway: the row says so.
    // That it is then offered to nothing is the catalog's rule (`relay::models`), asserted where
    // the catalog is tested.
    void anUnavailableIncludedRowSaysWhatItNeeds() {
        relay::KeysDialog keys;
        keys.setPresets(presetsWithHosted({QStringLiteral("kimi")}, false));
        auto *list = keys.findChild<QTreeWidget *>();
        QCOMPARE(keysRow(list, QStringLiteral("relay-free"))->text(1), QStringLiteral("Needs python3-cryptography"));
    }
};

QTEST_MAIN(ModelSettingsTests)
#include "modelsettings_test.moc"
