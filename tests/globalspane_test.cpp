// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GlobalsPane.h"
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
using relay::globals::GlobalsPane;

class GlobalsPaneTest : public QObject {
    Q_OBJECT
private slots:
    void protocolEventsExposeProblems() {
        GlobalsPane pane;
        QJsonObject request;
        pane.onRequest = [&](const QJsonObject &r) { request = r; };
        pane.refresh();
        pane.handleEvent({{"event", "globals_state"}, {"id", request.value("id")},
            {"records", QJsonArray{}}, {"problems", QJsonArray{QJsonObject{{"path", "/hq/bad.md"}, {"message", "Invalid card"}}}}});
        QCOMPARE(pane.findChild<QLabel *>("globalsNotice")->text(), QString("/hq/bad.md: Invalid card"));
    }
    void draftSurvivesSelectionRefreshAndConflict() {
        GlobalsPane pane;
        pane.findChild<QComboBox *>("globalsSection")->setCurrentIndex(3);
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        const QJsonObject a{{"kind", "memory"}, {"key", "AB12"}, {"title", "First"}, {"status", "active"}, {"path", "/hq/memory/a.md"}};
        const QJsonObject b{{"kind", "alias"}, {"key", "CD34"}, {"title", "Second"}};
        pane.setWorkspace("/project");
        QCOMPARE(requests.last().value("workspace").toString(), QString("/project"));
        auto state = [&](QJsonArray records) { pane.handleEvent({{"type", "globals_state"}, {"id", requests.last().value("id")}, {"records", records}}); };
        state({a, b});
        auto *list = pane.findChild<QListWidget *>("globalsList");
        auto *editor = pane.findChild<QPlainTextEdit *>("globalsEditor");
        list->setCurrentRow(0);
        auto loaded = a; loaded.insert("text", "Original"); loaded.insert("hash", "hash-a");
        pane.handleEvent({{"type", "globals_record"}, {"id", requests.last().value("id")}, {"record", loaded}});
        editor->setPlainText("Unsaved change");
        const int count = requests.size();
        list->setCurrentRow(1);
        QCOMPARE(requests.size(), count);
        QCOMPARE(list->currentRow(), 0);
        pane.refresh(); state({b, a});
        QCOMPARE(editor->toPlainText(), QString("Unsaved change"));
        QCOMPARE(list->currentRow(), 1);
        pane.findChild<QPushButton *>("globalsSave")->click();
        QCOMPARE(requests.last().value("base_hash").toString(), QString("hash-a"));
        QCOMPARE(requests.last().value("text").toString(), QString("Unsaved change"));
        QVERIFY(editor->isReadOnly());
        pane.handleEvent({{"type", "globals_error"}, {"id", requests.last().value("id")}, {"message", "File changed on disk"}});
        QCOMPARE(editor->toPlainText(), QString("Unsaved change"));
        QVERIFY(!editor->isReadOnly());
        QVERIFY(pane.findChild<QLabel *>("globalsNotice")->text().contains("changed on disk"));
        pane.findChild<QPushButton *>("globalsCancel")->click();
        QCOMPARE(editor->toPlainText(), QString("Original"));
        pane.refresh(); state({a, b});
        QCOMPARE(requests.last().value("type").toString(), QString("globals_get"));
        loaded.insert("text", "Changed elsewhere"); loaded.insert("hash", "hash-b");
        pane.handleEvent({{"type", "globals_record"}, {"id", requests.last().value("id")}, {"record", loaded}});
        QCOMPARE(editor->toPlainText(), QString("Changed elsewhere"));
    }
    void templatesSaveAndRetireWithoutDeleting() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        pane.findChild<QPushButton *>("globalsNewMemory")->click();
        auto *editor = pane.findChild<QPlainTextEdit *>("globalsEditor");
        QVERIFY(editor->toPlainText().contains("pinned: true"));
        pane.findChild<QPushButton *>("globalsSave")->click();
        QCOMPARE(requests.last().value("type").toString(), QString("globals_save"));
        QCOMPARE(requests.last().value("kind").toString(), QString("memory"));
        QVERIFY(requests.last().value("base_hash").toString().isEmpty());
        QJsonObject saved{{"kind", "memory"}, {"key", "NEW1"}, {"text", editor->toPlainText()}, {"hash", "new-hash"}, {"status", "active"}};
        pane.handleEvent({{"type", "globals_saved"}, {"id", requests.last().value("id")}, {"record", saved}});
        pane.findChild<QPushButton *>("globalsRetire")->click();
        QCOMPARE(requests.last().value("type").toString(), QString("globals_retire"));
        QCOMPARE(requests.last().value("key").toString(), QString("NEW1"));
        QCOMPARE(requests.last().value("base_hash").toString(), QString("new-hash"));
    }
    void instructionsExposeSourceAndCannotRetire() {
        GlobalsPane pane;
        pane.findChild<QComboBox *>("globalsSection")->setCurrentIndex(2);
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        QJsonObject instruction{{"kind", "instruction"}, {"key", "/home/test/AGENTS.md"}, {"path", "/home/test/AGENTS.md"}, {"title", "Instructions"}, {"scope", "user"}, {"shadowed", true}};
        pane.refresh();
        pane.handleEvent({{"type", "globals_state"}, {"id", requests.last().value("id")}, {"records", QJsonArray{instruction}}});
        pane.findChild<QListWidget *>("globalsList")->setCurrentRow(0);
        instruction.insert("text", "Rules"); instruction.insert("hash", "hash");
        pane.handleEvent({{"type", "globals_record"}, {"id", requests.last().value("id")}, {"record", instruction}});
        QVERIFY(!pane.findChild<QPushButton *>("globalsRetire")->isEnabled());
        QVERIFY(pane.findChild<QLabel *>("globalsSource")->text().contains("/home/test/AGENTS.md"));
        QVERIFY(pane.findChild<QLabel *>("globalsSource")->text().contains("overridden"));
        pane.findChild<QLineEdit *>("globalsSearch")->setText("missing");
        QCOMPARE(pane.findChild<QListWidget *>("globalsList")->count(), 0);
        pane.findChild<QLineEdit *>("globalsSearch")->clear();
        QCOMPARE(pane.findChild<QListWidget *>("globalsList")->currentRow(), 0);
        pane.findChild<QPlainTextEdit *>("globalsEditor")->setPlainText("Updated rules");
        pane.findChild<QPushButton *>("globalsSave")->click();
        QCOMPARE(requests.last().value("key").toString(), QString("/home/test/AGENTS.md"));
    }
    void userMemorySectionAndInterview() {
        GlobalsPane pane;
        QJsonObject request;
        pane.onRequest = [&](const QJsonObject &r) { request = r; };
        bool interviewed = false;
        pane.onInterview = [&] { interviewed = true; };
        pane.refresh();
        pane.handleEvent({{"event", "globals_state"}, {"id", request.value("id")}, {"records", QJsonArray{
            QJsonObject{{"kind", "memory"}, {"key", "AB12"}, {"title", "Working style"},
                        {"memory_scope", "user"}, {"summary", "Explain decisions concisely"}},
            QJsonObject{{"kind", "memory"}, {"key", "AB13"}, {"memory_scope", "team"}},
            QJsonObject{{"kind", "alias"}, {"key", "AB14"}},
            QJsonObject{{"kind", "instruction"}, {"key", "rules"}}}}});
        auto *list = pane.findChild<QListWidget *>("globalsList");
        QCOMPARE(list->count(), 1);
        QVERIFY(list->item(0)->text().contains("Explain decisions"));
        pane.findChild<QLineEdit *>("globalsSearch")->setText("concisely");
        QCOMPARE(list->count(), 1);
        pane.findChild<QPushButton *>("globalsInterview")->click();
        QVERIFY(interviewed);
        interviewed = false;
        pane.findChild<QPushButton *>("globalsNewMemory")->click();
        QVERIFY(!pane.findChild<QComboBox *>("globalsSection")->isEnabled());
        pane.findChild<QPushButton *>("globalsInterview")->click();
        QVERIFY(!interviewed);
        QVERIFY(pane.agentScreen().contains("User memory"));
    }
    void staleRepliesCannotReplaceCurrentSelection() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        QJsonObject a{{"kind", "memory"}, {"key", "A"}}, b{{"kind", "memory"}, {"key", "B"}};
        pane.refresh();
        pane.handleEvent({{"type", "globals_state"}, {"id", requests.last().value("id")}, {"records", QJsonArray{a, b}}});
        auto *list = pane.findChild<QListWidget *>("globalsList");
        list->setCurrentRow(0); const auto oldId = requests.last().value("id");
        list->setCurrentRow(1); const auto newId = requests.last().value("id");
        b.insert("text", "B body"); a.insert("text", "A body");
        pane.handleEvent({{"type", "globals_record"}, {"id", newId}, {"record", b}});
        pane.handleEvent({{"type", "globals_record"}, {"id", oldId}, {"record", a}});
        QCOMPARE(pane.findChild<QPlainTextEdit *>("globalsEditor")->toPlainText(), QString("B body"));
    }
};
QTEST_MAIN(GlobalsPaneTest)
#include "globalspane_test.moc"
