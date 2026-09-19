// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Provider and model configuration: the two modals. (The Settings pane is src/SettingsPane.h.)
//
//  * KeysDialog    — "API keys…": one row per provider, grouped Included / Subscriptions /
//                    Aggregator / Pay-as-you-go, with Add/Replace, Remove, Test and a link to the
//                    provider's key page. Keys are typed here and handed straight to the worker's
//                    keyring commands; nothing is echoed back and no key is ever stored in QSettings.
//                    The Included row is Relay Free (presets.py `hosted`): no key to add or remove,
//                    its status column carries today's allowance, and Test does one real call.
//  * RolesDialog   — "Model roles…": default provider, the Main/Flash/Lite/Local rows and the
//                    vision row, and an Advanced disclosure with one row per job (protocol 13.7).
//                    Every row is the same grid of controls — Model…, a provider, an effort — and a
//                    job in Advanced adds the tier it follows in front of them.
//
// Both dialogs talk to the pane's worker through `send` and receive its events through
// handleEvent(); neither knows anything else about the application.
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
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

// ----- model roles ------------------------------------------------------------------------------
class RolesDialog final : public QDialog {
public:
    explicit RolesDialog(QWidget *parent = nullptr);

    std::function<void(QJsonObject request)> send;
    std::function<void(const QString &presetId)> onProviderChosen;   // switches the pane's model
    std::function<void()> onRolesChanged;                            // persist + set_agent_options
    std::function<void()> openKeys;
    // The Main row. The pane's own model is not a tier the worker resolves — roles.py rejects
    // `tiers.main`, because it *is* the pane — so what that row offers goes back to the pane
    // instead: a model id becomes a set_model, a level becomes the pane's reasoning effort.
    // Empty model: the provider's own default model for this tier.
    std::function<void(const QString &model)> onMainModelChosen;
    std::function<void(const QString &level)> onMainEffortChosen;

    void setPresets(const QJsonArray &presets, const QJsonObject &tierCatalog, const QJsonArray &actions);
    void setResolved(const QJsonObject &tiers, const QJsonObject &roles);
    void setProvider(const QString &presetId);

    // QSettings helpers, shared with the pane so the protocol objects are built in one place.
    static QStringList tierIds();
    static QString tierSetting(const QString &tier, const QString &field);
    static QString roleSetting(const QString &role, const QString &field);

private:
    void rebuild();
    void chooseProvider(const QString &presetId);
    bool hasKey(const QString &presetId) const;
    QString presetLabel(const QString &presetId) const;
    QString providerName(const QString &presetId) const;
    QString providerChoice(const QString &presetId) const;
    QString shortProviderLabel(const QString &presetId) const;
    QJsonArray choosableProviders() const;
    QStringList effortsFor(const QString &presetId) const;
    QString effortNoteFor(const QString &presetId) const;
    QString rolePreset(const QString &role) const;
    QJsonObject tierDefault(const QString &tier) const;
    void fillProviders(QComboBox *box, const QString &neutral, const QString &selected) const;
    void fillDefaultProviders(QComboBox *box) const;
    QComboBox *effortBox(const QString &presetId, const QString &stored, const QString &name,
                         const QString &tip, bool allowDefault,
                         std::function<void(const QString &)> onPick);
    // Every row is four cells of one grid, so Main, Flash, Lite, Local and vision line up: the text,
    // Model…, the provider, the effort. The Advanced rows put the tier choice in front of those.
    void buildTierRow(QGridLayout *grid, int line, const QString &tier, const QJsonObject &spec);
    void buildVisionRow(QGridLayout *grid, int line);   // image context (issue EM1E)
    void buildActionRow(QGridLayout *grid, int line, const QJsonObject &action);

    QJsonArray m_presets, m_actions;
    QJsonObject m_catalog, m_resolvedTiers, m_resolvedRoles;
    QString m_provider;
    QComboBox *m_providerBox = nullptr;
    QLabel *m_recommended = nullptr;
    QWidget *m_tiers = nullptr, *m_advanced = nullptr;
    QPushButton *m_disclosure = nullptr;
    bool m_showAdvanced = false, m_filling = false;
};

}  // namespace relay
