// SPDX-License-Identifier: AGPL-3.0-or-later
#include "GlobalsPane.h"
#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTest>
#include <QVBoxLayout>
#include <QToolButton>
#include <QTreeWidget>
using relay::globals::GlobalsPane;

// The section control's rows by their stable id (the item data: 0 User memory, 1 Aliases,
// 2 Instructions, 3 All global records, 4 Suggestions, 5 Skills), not by where they sit (#9FX8).
static void selectSection(GlobalsPane &pane, int id) {
    auto *section = pane.findChild<QComboBox *>("globalsSection");
    section->setCurrentIndex(section->findData(id));
}

class GlobalsPaneTest : public QObject {
    Q_OBJECT
private slots:
    // #C8SV: a transcript's Keep refreshes the Globals panes on screen, and only those. GlobalsPane
    // has no Q_OBJECT, so findChildren<GlobalsPane *> matched every visible QWidget and called a
    // "refresh" whose onRequest was read out of an unrelated widget: SIGSEGV with the PC in the
    // heap, 1 ms after `globals_suggestion_accepted` (relay.log, 2026-09-23 16:44:18).
    void refreshVisibleCallsOnlyTheGlobalsPanesOnScreen() {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        layout->addWidget(new QLabel("a terminal pane's chrome"));
        layout->addWidget(new QLineEdit);
        auto *shown = new GlobalsPane;
        auto *hidden = new GlobalsPane;
        layout->addWidget(shown);
        layout->addWidget(hidden);
        int shownRequests = 0, hiddenRequests = 0;
        shown->onRequest = [&](const QJsonObject &) { ++shownRequests; };
        hidden->onRequest = [&](const QJsonObject &) { ++hiddenRequests; };
        window.show();
        hidden->hide();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        GlobalsPane::refreshVisible();
        QCOMPARE(shownRequests, 2);   // globals_suggestions and globals_list
        QCOMPARE(hiddenRequests, 0);
    }
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
        selectSection(pane, 3);
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
        selectSection(pane, 2);
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
        QCOMPARE(section->itemText(section->findData(4)), QString("Suggestions"));
        auto *review = pane.findChild<QPushButton *>("globalsReview");
        QVERIFY(review->isHidden());
        const QJsonObject a{{"id", "S1"}, {"fact", "Prefers terse answers"}, {"source", "agent"}, {"date", "2026-09-22"}, {"origin", "pane 3, turn 2"}};
        const QJsonObject b{{"id", "S2"}, {"fact", "Works in Zurich"}, {"source", "claude"}, {"date", "2026-09-22"}};
        const QJsonObject r{{"id", "R1"}, {"fact", "Likes tabs"}, {"source", "codex"}, {"date", "2026-09-21"}};
        // Any reply is the whole list, so it counts even when another surface asked for it.
        pane.handleEvent({{"event", "globals_suggestions"}, {"pending", QJsonArray{a, b}}, {"rejected", QJsonArray{r}}});
        QCOMPARE(section->itemText(section->findData(4)), QString("Suggestions (2)"));
        QVERIFY(!review->isHidden());
        QCOMPARE(review->text(), QString("Review 2 suggestions"));
        review->click();
        QCOMPARE(section->currentData().toInt(), 4);
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
        QCOMPARE(section->itemText(section->findData(4)), QString("Suggestions (1)"));
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
        QCOMPARE(section->itemText(section->findData(4)), QString("Suggestions"));
        QVERIFY(pane.agentScreen().contains("Suggestions waiting: 0"));
    }
    void transcriptEditOpensTheSuggestion() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        // Asked for before the list has arrived: selected when it does.
        pane.showSuggestion("S2");
        QCOMPARE(pane.findChild<QComboBox *>("globalsSection")->currentData().toInt(), 4);
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
        selectSection(pane, 4);
        QTest::qWait(20);   // the splitter settles on the next layout pass
        QVERIFY(split->sizes().at(0) > split->sizes().at(1));   // the review list gets the room
        QVERIFY(split->sizes().at(0) > listBefore);
        selectSection(pane, 0);
        QTest::qWait(20);
        QVERIFY(split->sizes().at(0) <= listBefore + 1);
        QCOMPARE(pane.findChild<QLabel *>("globalsSource")->text(), QString("Select a record to inspect or edit its source."));
        selectSection(pane, 4);
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
    // #9FX8 step 3: Globals › Skills sits between Aliases and Instructions, asks the tab's worker
    // for the registry the first time it is shown, lists only the global rows (`project: false`)
    // of a mixed payload, and opens the same skill page the Board's tab draws — with the import
    // and update actions in its toolbar, and Re-verify drafting (never sending) once a console
    // is there to draft into.
    void skillsSectionListsGlobalSkillsOnlyAndOpensAPage() {
        GlobalsPane pane;
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");   // #9FX8 evidence
        if (!shotDir.isEmpty()) { pane.resize(1000, 760); pane.show(); }
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        auto *section = pane.findChild<QComboBox *>("globalsSection");
        QCOMPARE(section->itemText(2), QString("Skills"));   // between Aliases and Instructions
        QCOMPARE(section->itemText(1), QString("Aliases"));
        QCOMPARE(section->itemText(3), QString("Instructions"));
        auto *page = pane.findChild<QWidget *>("globalsSkillsPage");
        QVERIFY(page);
        QVERIFY(page->isHidden());
        QVERIFY(requests.isEmpty());                          // nothing asked until it is shown
        selectSection(pane, 5);
        QVERIFY(!page->isHidden());
        QVERIFY(!pane.findChild<QListWidget *>("globalsList")->isVisibleTo(&pane));   // the view replaces it
        QCOMPARE(requests.last().value("type").toString(), QString("skills_registry"));
        const QString asked = requests.last().value("id").toString();
        QVERIFY(!asked.isEmpty());
        QCOMPARE(pane.findChild<QLabel *>("globalsSkillCount")->text(), QString("Loading…"));
        QVERIFY(pane.findChild<QToolButton *>("globalsSkillImport"));
        QVERIFY(pane.findChild<QToolButton *>("globalsSkillUpdates"));

        const QJsonObject bundled{{"id", "zz-bundled"}, {"name", "zz-bundled"}, {"source", "relay-bundled"},
            {"project", false}, {"path", "/opt/relay/skills/zz-bundled/SKILL.md"},
            {"version", "sha256:feed01"}, {"version_short", "feed01"}, {"description", "a bundled skill"},
            {"profile", QJsonObject{{"artifact", "text"}, {"human", "optional"}}},
            {"stats", QJsonObject{{"cases", 3}, {"pass_rate_30", 1.0}, {"last_served", "2026-07-01"}, {"stale", true}}},
            {"last_verified", "2026-07-01"}, {"stale_reason", "last passed 2026-07-01; rot high, so 7 days"},
            {"cases", QJsonArray{}}, {"cards", QJsonArray{"K7Q2"}}, {"changelog", QJsonArray{}}};
        QJsonObject userSkill = bundled;
        userSkill["id"] = "zz-user"; userSkill["name"] = "zz-user"; userSkill["source"] = "claude";
        userSkill["stats"] = QJsonObject{{"cases", 0}, {"pass_rate_30", QJsonValue()}, {"stale", false}};
        userSkill["last_verified"] = QString();
        userSkill.remove("stale_reason");
        QJsonObject projectSkill = bundled;
        projectSkill["id"] = "zz-project"; projectSkill["name"] = "zz-project";
        projectSkill["source"] = "project-relay"; projectSkill["project"] = true;
        // A reply to someone else's request (the Board's tab on the same worker) is not ours.
        pane.handleEvent({{"event", "skills_registry"}, {"id", "board-7"}, {"items", QJsonArray{projectSkill}}});
        QCOMPARE(pane.findChild<QLabel *>("globalsSkillCount")->text(), QString("Loading…"));
        pane.handleEvent({{"event", "skills_registry"}, {"id", asked},
                          {"items", QJsonArray{projectSkill, bundled, userSkill}}});
        auto *list = pane.findChild<QTreeWidget *>("globalsSkillList");
        QVERIFY(list);
        QCOMPARE(list->topLevelItemCount(), 2);              // the project skill is the Board's
        QStringList names;
        for (int i = 0; i < list->topLevelItemCount(); ++i) names << list->topLevelItem(i)->text(0);
        QVERIFY(names.contains("zz-bundled"));
        QVERIFY(names.contains("zz-user"));
        QVERIFY(!names.join(' ').contains("zz-project"));
        QCOMPARE(pane.findChild<QLabel *>("globalsSkillCount")->text(), QString("2 skills · 0 excluded"));
        const int bundledRow = names.indexOf("zz-bundled"), userRow = names.indexOf("zz-user");
        QCOMPARE(list->topLevelItem(bundledRow)->text(1), QString("relay-bundled · feed01"));
        QCOMPARE(list->topLevelItem(bundledRow)->text(5), QString("stale"));
        QCOMPARE(list->topLevelItem(bundledRow)->toolTip(5), QString("last passed 2026-07-01; rot high, so 7 days"));
        QCOMPARE(list->topLevelItem(userRow)->text(5), QString("no cases yet"));   // never stale

        // The page: the first row opens itself; picking the stale one shows why.
        list->setCurrentItem(list->topLevelItem(bundledRow));
        auto *title = pane.findChild<QLabel *>("globalsSkillTitle");
        auto *stats = pane.findChild<QLabel *>("globalsSkillStats");
        QVERIFY(title->text().contains("zz-bundled"));
        QVERIFY(title->text().contains("feed01"));
        QVERIFY(pane.findChild<QLabel *>("globalsSkillProfile")->text().contains("artifact text"));
        QVERIFY(stats->text().contains("<b>stale</b> — last passed 2026-07-01; rot high, so 7 days"));
        // One chip row per page, however often the page is redrawn: a row made a moment ago is
        // not shown yet, and the page used to miss it and draw the chips twice.
        QTest::qWait(10);   // the replaced rows' deleteLater
        QCOMPARE(pane.findChild<QWidget *>("globalsSkillLinked")->findChildren<QPushButton *>("globalsLinkedChip").size(), 1);
        QVERIFY(pane.findChild<QLabel *>("globalsSkillProvenance")->text().contains("\nsha256:feed01"));
        if (!shotDir.isEmpty()) {
            pane.setDraftTarget([](const QString &) {});   // as the window wires it: Load and Re-verify show
            QTest::qWait(50);   // the layout settles before the grab
            QVERIFY(pane.grab().save(shotDir + "/globals-skills.png"));
            pane.setDraftTarget({});
        }
        list->setCurrentItem(list->topLevelItem(userRow));
        QVERIFY(stats->text().contains("no cases yet"));
        QVERIFY(!stats->text().contains("stale"));

        // Re-verify is a draft, once there is a console to draft into; without one it is hidden.
        auto *reverify = pane.findChild<QPushButton *>("globalsSkillReverify");
        QVERIFY(reverify);
        QVERIFY(reverify->isHidden());
        QString drafted;
        pane.setDraftTarget([&drafted](const QString &text) { drafted = text; });
        QVERIFY(!reverify->isHidden());
        const int before = requests.size();
        reverify->click();
        QCOMPARE(drafted, QString("/skill zz-user serve one case of zz-user and record it with board_case"));
        QCOMPARE(requests.size(), before);                    // drafted, nothing sent

        // Refine goes to the worker as `refine_skills`; its reply reloads the registry.
        pane.findChild<QPushButton *>("globalsSkillRefine")->click();
        QCOMPARE(requests.last().value("type").toString(), QString("refine_skills"));
        QCOMPARE(requests.last().value("names").toArray().first().toString(), QString("zz-user"));
        pane.handleEvent({{"event", "skills_refined"}, {"id", requests.last().value("id")}, {"items", QJsonArray{}}});
        QCOMPARE(requests.last().value("type").toString(), QString("skills_registry"));
        // An error answering one of the view's own requests is said on the page.
        pane.handleEvent({{"event", "error"}, {"id", requests.last().value("id")}, {"text", "registry failed"}});
        QVERIFY(pane.findChild<QLabel *>("globalsSkillStatus")->text().contains("registry failed"));
    }
    // #JVEJ: what the retired Skills dialog showed is on Globals › Skills — a refined copy and an
    // overridden skill on the row, the folders the index skipped, the empty state — Refine takes
    // every selected skill, a refined copy opens in a pane, and `/skills <name>` (showSkill) opens
    // the section on that skill even before the registry has arrived.
    void skillsSectionHasTheDialogsParity() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        QStringList opened;
        pane.setDocumentTarget([&opened](const QString &path) { opened << path; });
        pane.showSkill("zz-refined");
        auto *section = pane.findChild<QComboBox *>("globalsSection");
        QCOMPARE(section->currentData().toInt(), 5);
        QCOMPARE(requests.size(), 1);                         // asked once, not once per call
        QCOMPARE(requests.last().value("type").toString(), QString("skills_registry"));

        // The empty state first: nothing global at all (a project skill is the Board's).
        const QJsonObject project{{"id", "zz-project"}, {"name", "zz-project"}, {"source", "project-relay"},
                                  {"project", true}, {"path", "/w/.relay/skills/zz-project/SKILL.md"}};
        pane.handleEvent({{"event", "skills_registry"}, {"id", requests.last().value("id")},
                          {"items", QJsonArray{project}}, {"skipped", QJsonArray{}}});
        auto *list = pane.findChild<QTreeWidget *>("globalsSkillList");
        auto *title = pane.findChild<QLabel *>("globalsSkillTitle");
        QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY2(title->text().startsWith("No skills found. Import some from a repository"), qPrintable(title->text()));

        const QString home = QDir::homePath();
        const QJsonObject refined{{"id", "zz-refined"}, {"name", "zz-refined"}, {"source", "relay-refined"},
            {"project", false}, {"path", home + "/.config/relay/skills/zz-refined/SKILL.md"},
            {"refined_from", home + "/.claude/skills/zz-refined/SKILL.md"}, {"version_short", "ab12"},
            {"stats", QJsonObject{{"cases", 0}}}};
        const QJsonObject shadowed{{"id", "zz-refined@claude"}, {"name", "zz-refined"}, {"source", "claude"},
            {"project", false}, {"path", home + "/.claude/skills/zz-refined/SKILL.md"},
            {"shadowed_by", home + "/.config/relay/skills/zz-refined/SKILL.md"}, {"stats", QJsonObject{{"cases", 0}}}};
        const QJsonObject other{{"id", "zz-other"}, {"name", "zz-other"}, {"source", "codex"}, {"project", false},
            {"path", "/opt/zz-other/SKILL.md"}, {"stats", QJsonObject{{"cases", 0}}}};
        pane.handleEvent({{"event", "skills_registry"}, {"id", requests.last().value("id")},
                          {"items", QJsonArray{refined, shadowed, other}},
                          {"skipped", QJsonArray{"broken: no description", "odd name: unsupported folder name"}}});
        QCOMPARE(list->topLevelItemCount(), 3);
        auto *count = pane.findChild<QLabel *>("globalsSkillCount");
        QCOMPARE(count->text(), QString("3 skills · 0 excluded · 2 skipped"));
        QVERIFY(count->toolTip().contains("broken: no description"));
        QCOMPARE(list->topLevelItem(0)->text(1), QString("relay-refined · ab12 · refined"));
        QVERIFY(list->topLevelItem(0)->toolTip(1).contains("refined from ~/.claude/skills/zz-refined/SKILL.md"));
        QCOMPARE(list->topLevelItem(1)->text(1), QString("claude · overridden"));
        QVERIFY(list->topLevelItem(1)->toolTip(1).contains("Not used: ~/.config/relay/skills/zz-refined/SKILL.md has the same name"));
        QVERIFY(list->topLevelItem(1)->foreground(0).style() != Qt::NoBrush);   // greyed
        QCOMPARE(list->topLevelItem(2)->foreground(0).style(), Qt::NoBrush);
        // showSkill waited for the rows: the named skill's page is the one open.
        QCOMPARE(list->currentItem(), list->topLevelItem(0));
        QVERIFY(title->text().contains("zz-refined"));

        // Refine takes every selected skill; its answer opens the copy in a pane.
        list->topLevelItem(2)->setSelected(true);
        pane.findChild<QPushButton *>("globalsSkillRefine")->click();
        QCOMPARE(requests.last().value("type").toString(), QString("refine_skills"));
        QCOMPARE(requests.last().value("names").toArray(), (QJsonArray{"zz-refined", "zz-other"}));
        pane.handleEvent({{"event", "skills_refined"}, {"id", requests.last().value("id")},
                          {"items", QJsonArray{QJsonObject{{"path", "/copy/zz-refined/SKILL.md"}}}}});
        QCOMPARE(opened, QStringList{"/copy/zz-refined/SKILL.md"});
        // Open file goes to the same pane opener, not the desktop.
        pane.findChild<QPushButton *>("globalsSkillOpen")->click();
        QCOMPARE(opened.last(), home + "/.config/relay/skills/zz-refined/SKILL.md");
    }
    // Project memories leave Globals (#9FX8, the owner's boundary #Y2MP/#P7SJ): a memory record
    // that is not user-scoped is listed in no section, and "All global records" says where it went.
    void projectMemoriesAreNotListed() {
        GlobalsPane pane;
        QList<QJsonObject> requests;
        pane.onRequest = [&](const QJsonObject &r) { requests.append(r); };
        auto *section = pane.findChild<QComboBox *>("globalsSection");
        QCOMPARE(section->itemText(section->findData(3)), QString("All global records"));
        const QJsonObject user{{"kind", "memory"}, {"key", "US01"}, {"title", "User fact"}, {"memory_scope", "user"}};
        const QJsonObject unscoped{{"kind", "memory"}, {"key", "US02"}, {"title", "Old fact"}};   // no scope: user
        const QJsonObject project{{"kind", "memory"}, {"key", "PR01"}, {"title", "Project fact"}, {"memory_scope", "project"}};
        const QJsonObject alias{{"kind", "alias"}, {"key", "AL01"}, {"title", "An alias"}};
        pane.refresh();
        pane.handleEvent({{"type", "globals_state"}, {"id", requests.last().value("id")},
                          {"records", QJsonArray{user, unscoped, project, alias}}});
        auto *list = pane.findChild<QListWidget *>("globalsList");
        const auto listed = [list] {
            QStringList rows;
            for (int i = 0; i < list->count(); ++i) rows << list->item(i)->text();
            return rows.join('\n');
        };
        for (int id : {0, 3}) {
            selectSection(pane, id);
            QVERIFY2(!listed().contains("Project fact"), qPrintable(listed()));
            QVERIFY(listed().contains("User fact"));
            QVERIFY(listed().contains("Old fact"));
        }
        QCOMPARE(list->count(), 4 - 1);                      // All: two user memories and the alias
        const QString shotDir = qEnvironmentVariable("RELAY_SHOT_DIR");   // #9FX8 evidence
        if (!shotDir.isEmpty()) {
            pane.resize(1000, 600); pane.show();
            QTest::qWait(50);
            QVERIFY(pane.grab().save(shotDir + "/globals-all-global-records.png"));
        }
        bool said = false;
        for (auto *label : pane.findChildren<QLabel *>())
            said = said || (label->isVisibleTo(&pane) && label->text().contains("Board › Memories")
                            && label->text().contains("1 project-scoped memory is not listed here"));
        QVERIFY(said);
    }
};
QTEST_MAIN(GlobalsPaneTest)
#include "globalspane_test.moc"
