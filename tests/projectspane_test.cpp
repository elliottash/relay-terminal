// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ProjectsPane.h"
#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTest>
#include <QTreeWidget>
using namespace relay::projects;
class ProjectsPaneTest : public QObject {
    Q_OBJECT
private slots:
    void browsingDoesNotAttach() {
        ProjectsPane pane;
        Record r; r.path = QStringLiteral("/tmp/demo"); r.name = QStringLiteral("demo");
        r.reason = QStringLiteral("picker");
        pane.setProjects({r});
        int opened = 0, attached = 0, boards = 0;
        pane.onOpenProject = [&](const QString &path) { QCOMPARE(path, r.path); ++opened; };
        pane.onAttachProject = [&](const QString &path) { QCOMPARE(path, r.path); ++attached; };
        pane.onOpenBoard = [&](const QString &path) { QCOMPARE(path, r.path); ++boards; };
        auto *tree = pane.findChild<QTreeWidget *>();
        tree->setCurrentItem(tree->topLevelItem(0));
        QCOMPARE(opened + attached + boards, 0);
        pane.findChild<QPushButton *>(QStringLiteral("projectsOpen"))->click();
        QCOMPARE(opened, 1); QCOMPARE(attached, 0);
        pane.findChild<QPushButton *>(QStringLiteral("projectsAttach"))->click();
        QCOMPARE(attached, 1);
        // Projects without a board can still ask to initialize/open it.
        pane.findChild<QPushButton *>(QStringLiteral("projectsBoard"))->click();
        QCOMPARE(boards, 1);
    }
    void activeSessionsAndSelectionSurviveRefresh() {
        ProjectsPane pane;
        Record r; r.path = QStringLiteral("/tmp/demo"); r.name = QStringLiteral("demo");
        pane.setProjects({r});
        QJsonObject session{{"session_id", "s1"}, {"title", "Active work"}, {"project_path", r.path}};
        pane.setActiveSessions({session});
        auto *tree = pane.findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
        pane.setProjects({r});
        QVERIFY(tree->currentItem()); QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Active work"));
        QJsonObject resumed;
        pane.onResume = [&](const QJsonObject &s) { resumed = s; };
        pane.findChild<QPushButton *>(QStringLiteral("projectsResume"))->click();
        QCOMPARE(resumed, session);
        QString chosen = QStringLiteral("unset");
        pane.onShowSessions = [&](const QString &path) { chosen = path; };
        tree->setCurrentItem(tree->topLevelItem(1));
        pane.findChild<QPushButton *>(QStringLiteral("projectsSessions"))->click();
        QVERIFY(chosen.isEmpty());
    }
    void searchAndDeclinedRemainUsable() {
        ProjectsPane pane;
        Record r; r.path = QStringLiteral("/tmp/demo"); r.name = QStringLiteral("demo");
        pane.setProjects({r}); pane.setDeclined({QStringLiteral("/tmp/declined")});
        auto *search = pane.findChild<QLineEdit *>(); search->setText(QStringLiteral("declined"));
        auto *tree = pane.findChild<QTreeWidget *>();
        QCOMPARE(tree->topLevelItemCount(), 2);
        tree->setCurrentItem(tree->topLevelItem(1));
        QString path;
        pane.onUndecline = [&](const QString &p) { path = p; };
        pane.findChild<QPushButton *>(QStringLiteral("projectsUndecline"))->click();
        QCOMPARE(path, QStringLiteral("/tmp/declined"));
        QCOMPARE(search->text(), QStringLiteral("declined"));
    }
};
QTEST_MAIN(ProjectsPaneTest)
#include "projectspane_test.moc"
