// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Provider keys: the one modal that is left. (The Settings pane is src/SettingsPane.h; the models
// pane, which is where models are picked, prioritized and given to a job, is src/ModelsPane.h.)
//
//  * KeysDialog    — "API keys…": one row per provider, grouped Included / Subscriptions /
//                    Aggregator / Pay-as-you-go, with Add/Replace, Remove, Test and a link to the
//                    provider's key page. Keys are typed here and handed straight to the worker's
//                    keyring commands; nothing is echoed back and no key is ever stored in QSettings.
//                    The Included row is Relay Free (presets.py `hosted`): no key to add or remove,
//                    its status column carries today's allowance, and Test does one real call.
//
// `RolesDialog` — "per-job models (advanced)" — was here until 2026-09-21 and is **retired** with
// card #MDL1: it is the models pane's fourth tab now (`src/JobsTab.h`, design 5.9), reviewed and
// rebuilt around the column it never had, the model each job actually runs on. Its storage lives
// on as `relay::rolestore` in that header, because `Pane::rolesObject` reads the same keys back.
//
// It talks to the pane's worker through `send` and receives its events through handleEvent(); it
// knows nothing else about the application.
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace relay {

// ----- API keys ---------------------------------------------------------------------------------
class KeysDialog final : public QDialog {
public:
    explicit KeysDialog(QWidget *parent = nullptr);

    std::function<void(QJsonObject request)> send;
    std::function<void()> onKeysChanged;      // a key was stored or removed; refresh the pane

    void setPresets(const QJsonArray &presets);
    void handleEvent(const QJsonObject &event);

private:
    void rebuild();
    void addOrReplace(const QString &id, const QString &label);
    void remove(const QString &id, const QString &label);
    void test(const QString &id);
    QTreeWidgetItem *rowFor(const QString &id) const;
    QString presetLabelFor(const QString &id) const;
    QString hostedStatus(const QJsonObject &preset) const;
    void updateButtons();

    QJsonArray m_presets;
    QJsonObject m_hostedQuota;   // the last hosted_quota seen: {limit, used, resets_at}
    QTreeWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_add = nullptr, *m_remove = nullptr, *m_test = nullptr, *m_where = nullptr;
};

}  // namespace relay
