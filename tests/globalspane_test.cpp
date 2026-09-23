// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GlobalsPane.h"
#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTest>
#include <QToolButton>
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
    void suggestionsListKeepEditAndNo() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        pane.refresh();
        QCOMPARE(requests.at(requests.size() - 2).value("type").toString(), QString("globals_suggestions"));
        QCOMPARE(requests.last().value("type").toString(), QString("globals_list"));
        auto *section = pane.findChild<QComboBox *>("globalsSection");
        QCOMPARE(section->itemText(4), QString("Suggestions"));
        auto *review = pane.findChild<QPushButton *>("globalsReview");
        QVERIFY(review->isHidden());
        const QJsonObject a{{"id", "S1"}, {"fact", "Prefers terse answers"}, {"source", "agent"}, {"date", "2026-09-22"}, {"origin", "pane 3, turn 2"}};
        const QJsonObject b{{"id", "S2"}, {"fact", "Works in Zurich"}, {"source", "claude"}, {"date", "2026-09-22"}};
        const QJsonObject r{{"id", "R1"}, {"fact", "Likes tabs"}, {"source", "codex"}, {"date", "2026-09-21"}};
        // Any reply is the whole list, so it counts even when another surface asked for it.
        pane.handleEvent({{"event", "globals_suggestions"}, {"pending", QJsonArray{a, b}}, {"rejected", QJsonArray{r}}});
        QCOMPARE(section->itemText(4), QString("Suggestions (2)"));
        QVERIFY(!review->isHidden());
        QCOMPARE(review->text(), QString("Review 2 suggestions"));
        review->click();
        QCOMPARE(section->currentIndex(), 4);
        auto *list = pane.findChild<QListWidget *>("globalsList");
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(1)->text().contains("imported from Claude Code"));
        auto *toggle = pane.findChild<QToolButton *>("globalsRejectedToggle");
        auto *rejected = pane.findChild<QListWidget *>("globalsRejected");
        QCOMPARE(toggle->text(), QString("Rejected (1)"));
        QVERIFY(rejected->isHidden());
        toggle->click();
        QVERIFY(!rejected->isHidden());
        QVERIFY(rejected->item(0)->text().contains("Likes tabs"));
        QVERIFY(pane.findChild<QPushButton *>("globalsSave")->isHidden());
        auto *keep = pane.findChild<QPushButton *>("globalsKeep");
        auto *no = pane.findChild<QPushButton *>("globalsReject");
        QVERIFY(!keep->isEnabled());
        const int before = requests.size();
        list->setCurrentRow(0);
        QCOMPARE(requests.size(), before);   // the list holds the whole suggestion: no round trip
        auto *editor = pane.findChild<QPlainTextEdit *>("globalsEditor");
        QCOMPARE(editor->toPlainText(), QString("Prefers terse answers"));
        QVERIFY(pane.findChild<QLabel *>("globalsSource")->text().contains("pane 3, turn 2"));
        // Keep unedited sends no text; the backend keeps the fact as suggested.
        keep->click();
        QCOMPARE(requests.last().value("type").toString(), QString("globals_suggestion_accept"));
        QCOMPARE(requests.last().value("sid").toString(), QString("S1"));
        QVERIFY(!requests.last().contains("text"));
        QVERIFY(!keep->isEnabled());
        pane.handleEvent({{"event", "globals_suggestion_accepted"}, {"id", requests.last().value("id")},
                          {"record", QJsonObject{{"kind", "memory"}, {"key", "M1"}}}});
        QVERIFY(pane.findChild<QLabel *>("globalsNotice")->text().contains("Kept"));
        QCOMPARE(section->itemText(4), QString("Suggestions (1)"));
        QCOMPARE(list->count(), 1);
        QCOMPARE(requests.last().value("type").toString(), QString("globals_list"));   // refreshed
        // Edit, then Keep: the edited wording goes with the accept.
        list->setCurrentRow(0);
        pane.findChild<QPushButton *>("globalsEdit")->click();
        editor->setPlainText("Works in Zurich, Switzerland");
        QVERIFY(!section->isEnabled());
        keep->click();
        QCOMPARE(requests.last().value("text").toString(), QString("Works in Zurich, Switzerland"));
        pane.handleEvent({{"event", "globals_error"}, {"id", requests.last().value("id")}, {"message", "Already decided"}});
        QCOMPARE(editor->toPlainText(), QString("Works in Zurich, Switzerland"));
        QVERIFY(pane.findChild<QLabel *>("globalsNotice")->text().contains("Already decided"));
        pane.findChild<QPushButton *>("globalsCancel")->click();
        QCOMPARE(editor->toPlainText(), QString("Works in Zurich"));
        no->click();
        QCOMPARE(requests.last().value("type").toString(), QString("globals_suggestion_reject"));
        QCOMPARE(requests.last().value("sid").toString(), QString("S2"));
        pane.handleEvent({{"event", "globals_suggestion_rejected"}, {"id", requests.last().value("id")}, {"sid", "S2"}});
        QVERIFY(pane.findChild<QLabel *>("globalsNotice")->text().contains("won't be suggested again"));
        QCOMPARE(section->itemText(4), QString("Suggestions"));
        QVERIFY(pane.agentScreen().contains("Suggestions waiting: 0"));
    }
    void transcriptEditOpensTheSuggestion() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        // Asked for before the list has arrived: selected when it does.
        pane.showSuggestion("S2");
        QCOMPARE(pane.findChild<QComboBox *>("globalsSection")->currentIndex(), 4);
        pane.handleEvent({{"event", "globals_suggestions"}, {"pending", QJsonArray{
            QJsonObject{{"id", "S1"}, {"fact", "One"}}, QJsonObject{{"id", "S2"}, {"fact", "Two"}}}}, {"rejected", QJsonArray{}}});
        QCOMPARE(pane.findChild<QListWidget *>("globalsList")->currentRow(), 1);
        QCOMPARE(pane.findChild<QPlainTextEdit *>("globalsEditor")->toPlainText(), QString("Two"));
        QVERIFY(pane.findChild<QPushButton *>("globalsKeep")->isEnabled());
        // Decided in the transcript meanwhile: the editor lets it go.
        pane.handleEvent({{"event", "globals_suggestions"}, {"pending", QJsonArray{QJsonObject{{"id", "S1"}, {"fact", "One"}}}},
                          {"rejected", QJsonArray{}}});
        QVERIFY(pane.findChild<QPlainTextEdit *>("globalsEditor")->toPlainText().isEmpty());
        QVERIFY(!pane.findChild<QPushButton *>("globalsKeep")->isEnabled());
        pane.showSuggestion("S1");
        QCOMPARE(pane.findChild<QPlainTextEdit *>("globalsEditor")->toPlainText(), QString("One"));
    }
    void toolResultsBecomeTranscriptNotes() {
        using relay::globals::SuggestionNote;
        auto note = relay::globals::suggestionFromToolResult({{"tool", "app_user_memory"}, {"result", QJsonObject{
            {"status", "pending"}, {"id", "S9"}, {"fact", "Prefers  terse\nanswers"}}}});
        QCOMPARE(note.kind, SuggestionNote::Pending);
        QCOMPARE(note.id, QString("S9"));
        QCOMPARE(relay::globals::suggestionLine(note), QString("Remember: Prefers terse answers"));
        note = relay::globals::suggestionFromToolResult({{"tool", "app_user_memory"}, {"result", QJsonObject{
            {"status", "declined"}, {"id", QJsonValue()}, {"fact", "Likes tabs"},
            {"matched", QJsonObject{{"kind", "rejected"}, {"date", "2026-09-21"}}}}}});
        QCOMPARE(note.kind, SuggestionNote::Declined);
        QCOMPARE(relay::globals::suggestionLine(note), QString("Not suggested: Likes tabs · you rejected it on 2026-09-21"));
        note = relay::globals::suggestionFromToolResult({{"tool", "mcp__relay__app_user_memory"}, {"result", QJsonObject{
            {"output", "{\"status\": \"duplicate\", \"fact\": \"x\"}"}}}});
        QCOMPARE(note.kind, SuggestionNote::Duplicate);
        QVERIFY(relay::globals::suggestionLine(note).endsWith("already in user memory"));
        // A list or save through the same tool, or another tool, draws nothing.
        QCOMPARE(relay::globals::suggestionFromToolResult({{"tool", "app_user_memory"}, {"result", QJsonObject{{"records", QJsonArray{}}}}}).kind,
                 SuggestionNote::None);
        QCOMPARE(relay::globals::suggestionFromToolResult({{"tool", "read_file"}, {"result", QJsonObject{{"status", "pending"}, {"id", "x"}, {"fact", "y"}}}}).kind,
                 SuggestionNote::None);
        QVERIFY(relay::globals::suggestionOutcome(false, "x").startsWith("Rejected — won't be suggested again"));
    }
    void importedMemoriesRefreshTheList() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        pane.resize(600, 800);
        pane.show();
        QTest::qWait(20);
        auto *split = pane.findChild<QSplitter *>();
        const int listBefore = split->sizes().at(0);
        pane.findChild<QComboBox *>("globalsSection")->setCurrentIndex(4);
        QTest::qWait(20);   // the splitter settles on the next layout pass
        QVERIFY(split->sizes().at(0) > split->sizes().at(1));   // the review list gets the room
        QVERIFY(split->sizes().at(0) > listBefore);
        pane.findChild<QComboBox *>("globalsSection")->setCurrentIndex(0);
        QTest::qWait(20);
        QVERIFY(split->sizes().at(0) <= listBefore + 1);
        QCOMPARE(pane.findChild<QLabel *>("globalsSource")->text(), QString("Select a record to inspect or edit its source."));
        pane.findChild<QComboBox *>("globalsSection")->setCurrentIndex(4);
        QCOMPARE(pane.findChild<QLabel *>("globalsNotice")->text(), QString("No suggestions waiting."));
        QVERIFY(pane.findChild<QLabel *>("globalsSource")->text().startsWith("Select a suggestion"));
        pane.handleEvent({{"event", "memory_import"}, {"claude", 2}, {"codex", 0}});
        QCOMPARE(requests.last().value("type").toString(), QString("globals_suggestions"));
        QString title, body;
        QVERIFY(relay::globals::memoryImportNotice({{"claude", 2}, {"codex", 1}}, &title, &body));
        QCOMPARE(title, QString("3 memories from Claude Code and Codex to review"));
        QVERIFY(relay::globals::memoryImportNotice({{"codex", 1}}, &title, &body));
        QCOMPARE(title, QString("1 memory from Codex to review"));
        QVERIFY(!relay::globals::memoryImportNotice({{"claude", 0}, {"codex", 0}}, &title, &body));
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
