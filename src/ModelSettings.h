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
//  * RolesDialog   — "per-job models": one row per job (protocol 13.7) plus the vision row. A job
//                    follows a tier — high, main, flash, lite or local — or has a provider of its
//                    own, and then a model from that provider's catalog and a reasoning level in
//                    the provider's own words. The tiers themselves are not here: they are the five
//                    ordered lists on Options › Models (owner, 2026-09-20), which is also where this
//                    dialog is opened from ("per-job models (advanced)").
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

// ----- per-job models ---------------------------------------------------------------------------
class RolesDialog final : public QDialog {
public:
    explicit RolesDialog(QWidget *parent = nullptr);

    std::function<void(QJsonObject request)> send;
    std::function<void()> onRolesChanged;                            // persist + set_agent_options
    std::function<void()> openKeys;

    // `presets` is the worker's `presets` event: each row carries `models`, that provider's catalog
    // ({id, label, efforts, effort_labels}), which is what a pinned row's model box lists.
    // `tierCatalog` supplies the one-line hint shown on each tier in a row's tier box.
    void setPresets(const QJsonArray &presets, const QJsonObject &tierCatalog, const QJsonArray &actions);
    // `tiers` is ignored: what each tier resolved to is shown on Options › Models.
    void setResolved(const QJsonObject &tiers, const QJsonObject &roles);
    // The pane's own provider: where a job lands before the worker has resolved anything.
    void setProvider(const QString &presetId);

    // QSettings helpers, shared with the pane so the protocol objects are built in one place.
    static QStringList tierIds();
    static QString tierSetting(const QString &tier, const QString &field);
    static QString roleSetting(const QString &role, const QString &field);
    // The two writes behind every "which model does this job run on" pick, shared with the
    // Switchboard pane's model box (#BRD3) so the two writers cannot drift. A tier pick (empty =
    // the job's built-in default tier) clears any pinned endpoint; a provider pick (empty = back
    // to the tier) clears the tier, and both drop a stored model id — protocol 13.7 makes a tier
    // and an endpoint exclusive.
    static void writeRoleTier(const QString &role, const QString &tier);
    static void writeRolePreset(const QString &role, const QString &presetId);
    // A provider *and* one of its models, with the level that model runs at (#PK5Q). The helper
    // agent's box lists the per-model catalog the terminal pane's box lists, so a pick there is a
    // pair, not a provider — which the role value has always been able to carry (protocol 13.7:
    // `preset` plus `model` plus `effort`); `writeRolePreset` simply never wrote the pair, because
    // the only caller before this could only name a provider. An empty `model` means that
    // provider's own default, which is exactly what a value written before today means.
    static void writeRoleEntry(const QString &role, const QString &presetId, const QString &model,
                               const QString &effort = QString());

private:
    void rebuild();
    QJsonObject presetRow(const QString &presetId) const;
    QJsonObject modelRow(const QString &presetId, const QString &modelId) const;
    QString presetLabel(const QString &presetId) const;
    QString providerName(const QString &presetId) const;
    QString providerChoice(const QString &presetId) const;
    QJsonArray choosableProviders() const;
    QStringList effortsFor(const QString &presetId, const QString &modelId) const;
    // Whether this row's level is the provider's to decide rather than this dialog's (card #MDL1,
    // 2026-09-21): the worker's `effort_fixed`, else no levels at all, else Relay Free.
    bool effortFixedFor(const QString &presetId, const QString &modelId) const;
    QString effortNoteFor(const QString &presetId) const;
    QString rolePreset(const QString &role) const;
    QString ownProviderFor(const QString &role) const;
    void fillProviders(QComboBox *box, const QString &neutral, const QString &selected) const;
    QComboBox *effortBox(const QString &presetId, const QString &modelId, const QString &stored,
                         const QString &name, const QString &tip,
                         std::function<void(const QString &)> onPick);
    // A combo over the provider's catalog, or Model… and a typed id when it sent no catalog.
    QWidget *modelCell(const QString &role, const QString &title, const QString &presetId);
    // Every row is five cells of one grid, so the rows line up: the text, what the job follows (a
    // tier or "its own provider…"; for vision, the provider), then the provider, model and effort
    // of a row that has a provider of its own.
    void buildVisionRow(QGridLayout *grid, int line);   // image context (issue EM1E)
    void buildActionRow(QGridLayout *grid, int line, const QJsonObject &action);

    QJsonArray m_presets, m_actions;
    QJsonObject m_catalog, m_resolvedRoles;
    QString m_provider;
    QWidget *m_rows = nullptr;
    bool m_filling = false;
};

}  // namespace relay
