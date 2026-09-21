// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Projects.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>
class QLineEdit;
class QTreeWidget;
class QLabel;
class QPushButton;
class QToolButton;
namespace relay::projects {
// Browsing has no attachment side effects. Every action goes through the window.
class ProjectsPane : public QWidget {
public:
    explicit ProjectsPane(QWidget *parent = nullptr);
    void setProjects(const QList<Record> &projects);
    void setDeclined(const QStringList &paths);
    // Open sessions: {session_id, title, workspace, project_path, status}; project_path is the
    // explicit attachment, including empty for a session outside projects.
    void setActiveSessions(const QJsonArray &sessions);
    void focusSearch();
    QString agentScreen() const;
    std::function<void(const QString &)> onOpenProject, onAttachProject, onOpenBoard,
        onShowSessions, onForget, onUndecline;
    std::function<void(const QJsonObject &)> onResume;
    std::function<void()> onBrowse, onInit;
private:
    void rebuild();
    void selectionChanged();
    QString selectedPath() const;
    QList<Record> m_projects;
    QStringList m_declined, m_pinned;
    QJsonArray m_sessions;
    QLineEdit *m_search;
    QTreeWidget *m_tree;
    QLabel *m_details;
    QPushButton *m_open, *m_attach, *m_board, *m_history, *m_resume, *m_allow;
    QToolButton *m_more;
};
}
