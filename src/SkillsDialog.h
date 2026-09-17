// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Skills manager: lists the skills the agent can load, toggles exclusions, refines copies and
// imports skills from a git repository after review (protocol section 11). Non-modal; it talks
// to the pane's worker through `send` and receives skills* events through handleEvent().
#include <QDialog>
#include <QJsonObject>
#include <functional>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace relay {

class SkillsDialog final : public QDialog {
public:
    explicit SkillsDialog(QWidget *parent = nullptr);

    std::function<void(QJsonObject request)> send;
    std::function<void(const QStringList &excluded)> onExcludedChanged;   // persisted by the caller
    QStringList excluded;   // names excluded in settings, merged with the worker's view

    void refresh();
    void handleEvent(const QJsonObject &event);

private:
    void showList(const QJsonObject &event);
    void importFromRepository();
    void showImportPreview(const QJsonObject &event);
    void checkUpdates();
    QStringList selectedNames() const;
    QStringList excludedNames() const;

    QTreeWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_refine = nullptr, *m_updates = nullptr;
    QStringList m_listed;
    QString m_note;
    bool m_filling = false;
};

}  // namespace relay
