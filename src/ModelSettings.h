// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Provider and model configuration: the two modals plus the compact Settings window.
//
//  * KeysDialog    — "API keys…": one row per provider, grouped Subscriptions / Aggregator /
//                    Pay-as-you-go, with Add/Replace, Remove, Test and a link to the provider's key
//                    page. Keys are typed here and handed straight to the worker's keyring commands;
//                    nothing is echoed back and no key is ever stored in QSettings.
//  * RolesDialog   — "Model roles…": default provider, the three Main/Flash/Lite rows, and an
//                    Advanced disclosure with one row per job (protocol 13.7).
//  * SettingsWindow— sections down the left, rows on the right, built from a catalog the caller
//                    fills so the same rows also render in the actions palette.
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
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace relay {

// ----- compact settings window ----------------------------------------------------------------
// One row of a settings section. The caller supplies the reader and the writer, so QSettings stays
// the single source of truth and the palette can render the same rows as menu entries.
struct SettingRow {
    enum Kind { Toggle, Choice, Text, Number, Button, Info };
    Kind kind = Toggle;
    QString id;                 // stable identity, used as the palette key
    QString label, detail;
    QString aliases;            // extra palette search terms
    bool checked = false;                                   // Toggle
    std::function<void(bool)> onToggle;
    QStringList options, optionLabels;                      // Choice
    QString current;
    std::function<void(const QString &)> onChoose;
    QString text, placeholder;                              // Text
    std::function<void(const QString &)> onText;
    int number = 0, minimum = 0, maximum = 0;               // Number
    QString suffix;
    std::function<void(int)> onNumber;
    QString buttonText;                                     // Button
    std::function<void()> run;
};

struct SettingsSection {
    QString id, title, blurb;
    QList<SettingRow> rows;
};

// The window rebuilds itself from `build` after every change, so a row that depends on another
// (for example a model row that follows the chosen provider) always shows current values.
class SettingsWindow final : public QDialog {
public:
    explicit SettingsWindow(std::function<QList<SettingsSection>()> build, QWidget *parent = nullptr);
    void showSection(const QString &id);
    void rebuild();

private:
    void fillSection(QWidget *page, const SettingsSection &section);

    std::function<QList<SettingsSection>()> m_build;
    QListWidget *m_sections = nullptr;
    QStackedWidget *m_pages = nullptr;
    QString m_wanted;
};

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

    QJsonArray m_presets;
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
    QString shortProviderLabel(const QString &presetId) const;
    QStringList effortsFor(const QString &presetId) const;
    QJsonObject tierDefault(const QString &tier) const;
    void buildTierRow(QVBoxLayout *into, const QString &tier, const QJsonObject &spec);
    void buildVisionRow(QVBoxLayout *into);   // image context (issue EM1E)
    void buildActionRow(QVBoxLayout *into, const QJsonObject &action);
    void pinRole(const QString &role);

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
