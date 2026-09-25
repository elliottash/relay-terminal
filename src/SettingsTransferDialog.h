// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QJsonObject>

#include "SettingsExport.h"

#include <functional>
#include <QMap>

class QCheckBox;
class QTreeWidget;
class QLabel;

// Card #05J2: the two Options / palette dialogs over src/SettingsExport.h —
// "Export settings…" (what will leave this machine) and "Import settings…"
// (the visible review screen: current vs incoming side by side, per-item
// Keep current / Use imported for conflicts, bulk resolution, skipped and
// attention items shown with reasons).

namespace relay {

class SettingsExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsExportDialog(QWidget *parent = nullptr);

private:
    void refreshSummary();
    void doExport();

    QCheckBox *m_memories = nullptr;
    QLabel *m_summary = nullptr;
    QString m_path;
};

class SettingsImportDialog : public QDialog {
    Q_OBJECT
public:
    // `afterApply` re-applies live state (settings cache, keymap, theme) once
    // the import landed; the dialog calls it after a successful apply.
    std::function<void()> afterApply;
    // Model/hotkey validation against this installation's catalog; set by the
    // window before the dialog runs. Unset hooks accept every action and flag
    // every model id as needing attention instead of silently applying it.
    relay::settingsexport::Hooks hooks;

    // `modelKnown` decides whether an imported model id is served by this
    // install; `actionKnown` decides whether a hotkey action exists here.
    explicit SettingsImportDialog(const QString &path, QWidget *parent = nullptr);

private:
    void rebuild();
    void applyBulk(int resolutionIndex);
    void doApply();
    void rowDecided(const QString &id, bool decided);
    void updateStatusLine();
    relay::settingsexport::ImportPlan planFromUi() const;

    QString m_path;
    QJsonObject m_bundle;
    QMap<QString, relay::settingsexport::Resolution> m_decisions;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_status = nullptr;
    class QPushButton *m_apply = nullptr;
};

} // namespace relay
