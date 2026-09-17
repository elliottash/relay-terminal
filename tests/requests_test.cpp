// SPDX-License-Identifier: GPL-3.0-or-later
#include "RequestLedger.h"
#include "RequestsPanel.h"

#include <QJsonArray>
#include <QHash>
#include <QJsonDocument>
#include <QLabel>
#include <QTest>
#include <QTreeWidget>

using relay::RequestLedgerModel;
using relay::RequestsPanel;

namespace {
// Test JSON uses single quotes to keep the C++ strings readable.
QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }

const char *kRequests = R"({'event':'requests','total':4,'open':2,
  'counts':{'open':1,'in_progress':1,'done':1,'deferred':1,'blocked':0,'cancelled':0,'cancelled_by_user':0},
  'items':[
   {'id':'R1','text_preview':'fix the parser','source':'ask','origin':'user','requires_completion':true,'status':'done','reason':null,'turn_id':'q1','turn':1,'todo_ids':['T1'],'attachments':[],'audit':[]},
   {'id':'R2','text_preview':'also update the docs','source':'steer','origin':'user','requires_completion':true,'status':'in_progress','reason':null,'turn_id':'q1','turn':1,'todo_ids':['T2'],'attachments':['/w/a.txt'],'audit':[{'turn_id':'q1','quote':'update the docs'}]},
   {'id':'R3','text_preview':'bump version','source':'queue','origin':'user','requires_completion':true,'status':'deferred','reason':'needs owner input','turn_id':'q2','turn':2,'todo_ids':[],'attachments':[],'audit':[]},
   {'id':'R4','text_preview':'write tests','source':'ask','origin':'user','requires_completion':true,'status':'open','reason':null,'turn_id':null,'turn':null,'todo_ids':[],'attachments':[],'audit':[]}]})";
const char *kTodos = R"({'event':'todos','turn_id':'q1','open':1,'items':[
   {'id':'T1','text':'fix parser','status':'completed','request_ids':['R1'],'note':''},
   {'id':'T2','text':'update README','status':'in_progress','request_ids':['R2'],'note':''},
   {'id':'T3','text':'tidy imports','status':'pending','request_ids':[],'note':''}]})";
}  // namespace

class RequestsTests : public QObject {
    Q_OBJECT
private slots:
    void parsesRequestsAndTodos() {
        RequestLedgerModel model;
        int changes = 0;
        model.onChanged = [&] { ++changes; };
        QVERIFY(model.isEmpty());
        QVERIFY(model.handle(json(kRequests)));
        QVERIFY(model.handle(json(kTodos)));
        QCOMPARE(changes, 2);
        QCOMPARE(model.requests().size(), 4);
        QCOMPARE(model.total(), 4);
        QCOMPARE(model.openCount(), 2);
        QCOMPARE(model.count(QStringLiteral("deferred")), 1);
        QCOMPARE(model.openTodos(), 2);
        const auto *r2 = model.find(QStringLiteral("R2"));
        QVERIFY(r2);
        QCOMPARE(r2->source, QStringLiteral("steer"));
        QCOMPARE(r2->auditQuotes, QStringList{QStringLiteral("update the docs")});
        QCOMPARE(r2->attachments, QStringList{QStringLiteral("/w/a.txt")});
        QCOMPARE(model.find(QStringLiteral("R3"))->reason, QStringLiteral("needs owner input"));
        QVERIFY(model.find(QStringLiteral("R4"))->reason.isEmpty());   // null reason
        QCOMPARE(model.todosFor(QStringLiteral("R2")).size(), 1);
        QCOMPARE(model.unlinkedTodos().size(), 1);
        // Tasks are the model's todos and nothing else: T1 completed, T2 in progress, T3 pending.
        // R3 (deferred) and R4 (queued) have no todos, and a request is never a task of its own.
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 1/3"));
        QCOMPARE(model.chipState(), QStringLiteral("running"));
        QVERIFY(model.chipToolTip().contains(QStringLiteral("1 of 3 completed")));
        for (const auto &task : model.tasks()) QVERIFY2(task.key.startsWith('T'), qPrintable(task.key));
        // The ledger itself is still parsed in full: it links todos and survives compaction.
        QVERIFY(model.chipToolTip().split('\n').first().startsWith(QStringLiteral("Current task list")));
        QVERIFY(!model.handle(json("{'event':'done'}")));
    }

    // The complaint this split fixes: typing one ask used to show "Tasks 0/1" → "Tasks 1/1" with
    // the user's own command as the task text. With no todos there is no task list and no chip.
    void aPromptIsNeverATask() {
        RequestLedgerModel model;
        model.handle(json("{'event':'requests','items':[{'id':'R1','text_preview':'ls the repo','status':'in_progress','delivered':true,'turn_id':'q1'}]}"));
        QVERIFY(model.tasks().isEmpty());
        QVERIFY(!model.hasTasks());
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));
        QCOMPARE(model.chipState(), QStringLiteral("none"));
        QVERIFY(model.turnEndLine().isEmpty());
        model.handle(json("{'event':'requests','items':[{'id':'R1','text_preview':'ls the repo','status':'done','delivered':true,'turn_id':'q1'}]}"));
        QVERIFY(!model.hasTasks());
        QVERIFY(model.turnEndLine().isEmpty());
        // The ledger kept the entry all along; it is simply not a user-facing task.
        QCOMPARE(model.requests().size(), 1);
        QCOMPARE(model.find(QStringLiteral("R1"))->status, QStringLiteral("done"));
    }

    void verbatimTextSurvivesRefresh() {
        RequestLedgerModel model;
        model.handle(json(kRequests));
        QCOMPARE(model.find(QStringLiteral("R4"))->fullText(), QStringLiteral("write tests"));
        model.handle(json("{'event':'request','id':'x','item':{'id':'R4','text':'write tests\\nfor the parser'}}"));
        QCOMPARE(model.find(QStringLiteral("R4"))->fullText(), QStringLiteral("write tests\nfor the parser"));
        model.handle(json(kRequests));
        QCOMPARE(model.find(QStringLiteral("R4"))->text, QStringLiteral("write tests\nfor the parser"));
    }

    void chipWhenAllSettled() {
        RequestLedgerModel model;
        model.handle(json("{'event':'requests','total':3,'open':0,'counts':{'done':3},'items':[]}"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));
        QVERIFY(!model.hasTasks());
        QVERIFY(!model.isEmpty());
        QCOMPARE(model.chipState(), QStringLiteral("none"));
    }

    // Every status mapping, loaded at once (one turn, so one batch).
    void outcomeMappings() {
        RequestLedgerModel model;
        model.handle(json(R"({'event':'requests','total':10,'open':1,'items':[
          {'id':'R1','text_preview':'a','status':'done','turn_id':'q1','delivered':true},
          {'id':'R2','text_preview':'b','status':'cancelled_by_user','turn_id':'q1','delivered':true},
          {'id':'R3','text_preview':'c','status':'blocked','reason':'no access','turn_id':'q1','delivered':true},
          {'id':'R4','text_preview':'d','status':'deferred','turn_id':'q1','delivered':true},
          {'id':'R5','text_preview':'e','status':'cancelled','turn_id':'q1','delivered':true},
          {'id':'R6','text_preview':'f','status':'open','turn_id':'q1','delivered':true},
          {'id':'R7','text_preview':'g','status':'blocked','turn_id':'q1','delivered':true},
          {'id':'R8','text_preview':'wake','status':'done','origin':'relay','requires_completion':false,'turn_id':'q1','delivered':true},
          {'id':'R9','text_preview':'h','status':'done','turn_id':'q1','delivered':true},
          {'id':'R10','text_preview':'i','status':'cancelled_by_user','turn_id':'q1','delivered':true}]})"));
        model.handle(json(R"({'event':'todos','items':[
          {'id':'T1','text':'one','status':'completed','request_ids':['R7']},
          {'id':'T2','text':'two','status':'blocked','note':'x','request_ids':['R7']},
          {'id':'T3','text':'three','status':'deferred','note':'x','request_ids':['R7']},
          {'id':'T4','text':'four','status':'cancelled','note':'x','request_ids':['R7']},
          {'id':'T5','text':'five','status':'pending','request_ids':['R7']},
          {'id':'T6','text':'six','status':'pending','request_ids':['R9']},
          {'id':'T7','text':'seven','status':'in_progress','request_ids':['R10']}]})"));
        QHash<QString, relay::TaskOutcome> outcome;
        for (const auto &task : model.tasks()) outcome.insert(task.key, task.outcome);
        using O = relay::TaskOutcome;
        QCOMPARE(outcome.size(), 7);                       // the seven todos; no request is a task
        for (const QString &key : outcome.keys()) QVERIFY2(key.startsWith('T'), qPrintable(key));
        QCOMPARE(outcome.value(QStringLiteral("T1")), O::Completed);
        QCOMPARE(outcome.value(QStringLiteral("T2")), O::Failed);
        QCOMPARE(outcome.value(QStringLiteral("T3")), O::Deferred);
        QCOMPARE(outcome.value(QStringLiteral("T4")), O::Cancelled);
        QCOMPARE(outcome.value(QStringLiteral("T5")), O::Unfinished);
        QCOMPARE(outcome.value(QStringLiteral("T6")), O::Completed);    // R9 marked done by the user
        QCOMPARE(outcome.value(QStringLiteral("T7")), O::Cancelled);    // R10 cancelled by the user
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 2/7 (1 failed, 1 deferred, 2 cancelled, 1 unfinished)"));
        QCOMPARE(model.chipState(), QStringLiteral("attention"));
        const QString line = model.turnEndLine(400);
        QVERIFY2(line.startsWith(QStringLiteral("Tasks 2/7 (1 failed, 1 deferred, 2 cancelled, 1 unfinished) · T2 “two” failed, T3 “three” deferred, T4 “four” cancelled")), qPrintable(line));
        QVERIFY(model.turnEndLine(60).contains(QStringLiteral("more")));
        QCOMPARE(RequestLedgerModel::statusLabel(QStringLiteral("blocked")), QStringLiteral("failed"));

        // While a turn runs, open tasks are just not done yet and the suffix waits.
        model.handle(json(R"({'event':'requests','total':1,'open':1,'items':[
          {'id':'R1','text_preview':'a','status':'in_progress','turn_id':'q1','delivered':true},
          {'id':'R2','text_preview':'b','status':'blocked','turn_id':'q1','delivered':true},
          {'id':'R7','text_preview':'g','status':'open','turn_id':'q1','delivered':true}]})"));
        // (The highest id went from R10 to R7: treated as another ledger, batches rebuilt.)
        model.handle(json(R"({'event':'todos','items':[{'id':'T5','text':'five','status':'pending','request_ids':['R7']}]})"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 0/1"));
        QCOMPARE(model.chipState(), QStringLiteral("running"));
        // R2 is blocked, but a request is not a task: one active todo alone is not worth a line.
        QVERIFY(model.turnEndLine().isEmpty());
        // A queued (never delivered) request is not unfinished even when nothing runs.
        model.handle(json(R"({'event':'requests','total':2,'open':2,'items':[
          {'id':'R1','text_preview':'a','status':'done','turn_id':'q1','delivered':true},
          {'id':'R2','text_preview':'b','status':'blocked','turn_id':'q1','delivered':true},
          {'id':'R7','text_preview':'g','status':'open','turn_id':'q1','delivered':true},
          {'id':'R8','text_preview':'later','status':'open','delivered':false}]})"));
        for (const auto &task : model.tasks())
            if (task.key == QStringLiteral("T5")) QCOMPARE(task.outcome, O::Unfinished);   // R8 waiting does not make R7's todo active
    }

    // A live session: counting restarts when everything is settled and a new request arrives.
    void batchesRestartWhenSettled() {
        RequestLedgerModel model;
        auto requests = [&](const char *items) {
            model.handle(json((QByteArray("{'event':'requests','items':[") + items + "]}").constData()));
        };
        requests("{'id':'R1','text_preview':'three things','status':'open'}");
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));       // a queued ask is not a task
        QCOMPARE(model.chipState(), QStringLiteral("none"));
        QVERIFY(!model.hasTasks());
        requests("{'id':'R1','text_preview':'three things','status':'in_progress','delivered':true,'turn_id':'q1'}");
        model.handle(json(R"({'event':'todos','items':[{'id':'T1','text':'a','status':'in_progress','request_ids':['R1']},
            {'id':'T2','text':'b','status':'pending','request_ids':['R1']},{'id':'T3','text':'c','status':'pending','request_ids':['R1']}]})"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 0/3"));
        model.handle(json(R"({'event':'todos','items':[{'id':'T1','text':'a','status':'completed','request_ids':['R1']},
            {'id':'T2','text':'b','status':'completed','request_ids':['R1']},{'id':'T3','text':'c','status':'blocked','note':'no key','request_ids':['R1']}]})"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 2/3"));            // R1's turn still runs
        QCOMPARE(model.chipState(), QStringLiteral("running"));
        requests("{'id':'R1','text_preview':'three things','status':'blocked','delivered':true,'turn_id':'q1'}");
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 2/3 (1 failed)"));
        QCOMPARE(model.chipState(), QStringLiteral("attention"));
        QCOMPARE(model.turnEndLine(), QStringLiteral("Tasks 2/3 (1 failed) · T3 “c” failed"));

        // Settled + new request → a new list; the old one is "earlier".
        requests("{'id':'R1','text_preview':'three things','status':'blocked','delivered':true,'turn_id':'q1'},{'id':'R2','text_preview':'one thing','status':'open'}");
        QCOMPARE(model.currentBatch(), 2);                          // the ledger still starts a new list
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));        // which has no todos yet
        QVERIFY(!model.inCurrentBatch(QStringLiteral("R1")));
        QCOMPARE(model.earlierSummary().progress(true), QStringLiteral("2/3 (1 failed)"));
        requests("{'id':'R1','text_preview':'three things','status':'blocked','delivered':true,'turn_id':'q1'},{'id':'R2','text_preview':'one thing','status':'in_progress','delivered':true,'turn_id':'q2'}");
        // A steer while R2 runs joins the same list.
        requests("{'id':'R1','text_preview':'three things','status':'blocked','delivered':true,'turn_id':'q1'},{'id':'R2','text_preview':'one thing','status':'in_progress','delivered':true,'turn_id':'q2'},{'id':'R3','text_preview':'and this','status':'in_progress','delivered':true,'turn_id':'q2','source':'steer'}");
        QCOMPARE(model.currentBatch(), 2);
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));
        requests("{'id':'R1','text_preview':'three things','status':'blocked','delivered':true,'turn_id':'q1'},{'id':'R2','text_preview':'one thing','status':'done','delivered':true,'turn_id':'q2'},{'id':'R3','text_preview':'and this','status':'done','delivered':true,'turn_id':'q2'}");
        // Two asks answered without a todo list: nothing to show, and no end-of-turn line.
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));
        QCOMPARE(model.chipState(), QStringLiteral("none"));
        QVERIFY(model.turnEndLine().isEmpty());

        // Next request: todos, then the step limit leaves one unfinished.
        const char *base = "{'id':'R1','text_preview':'three things','status':'blocked','delivered':true,'turn_id':'q1'},{'id':'R2','text_preview':'one thing','status':'done','delivered':true,'turn_id':'q2'},{'id':'R3','text_preview':'and this','status':'done','delivered':true,'turn_id':'q2'}";
        requests((QByteArray(base) + ",{'id':'R4','text_preview':'two more','status':'in_progress','delivered':true,'turn_id':'q3'}").constData());
        QCOMPARE(model.currentBatch(), 3);
        model.handle(json(R"({'event':'todos','items':[{'id':'T4','text':'d','status':'completed','request_ids':['R4']},
            {'id':'T5','text':'e','status':'pending','request_ids':['R4']}]})"));   // T1–T3 dropped by the model
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 1/2"));
        QCOMPARE(model.earlierSummary().progress(true), QStringLiteral("2/3 (1 failed)"));   // settled todos are kept
        requests((QByteArray(base) + ",{'id':'R4','text_preview':'two more','status':'open','delivered':true,'turn_id':'q3'}").constData());
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 1/2 (1 unfinished)"));
        QCOMPARE(model.turnEndLine(), QStringLiteral("Tasks 1/2 (1 unfinished) · T5 “e” unfinished"));

        // "Continue": a new list that carries the unfinished task.
        requests((QByteArray(base) + ",{'id':'R4','text_preview':'two more','status':'open','delivered':true,'turn_id':'q3'},{'id':'R5','text_preview':'Continue','status':'open'}").constData());
        QCOMPARE(model.currentBatch(), 4);
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 0/1 (1 unfinished)"));   // T5 carried over
        QVERIFY(model.inCurrentBatch(QStringLiteral("R4")));
        requests((QByteArray(base) + ",{'id':'R4','text_preview':'two more','status':'done','delivered':true,'turn_id':'q4'},{'id':'R5','text_preview':'Continue','status':'done','delivered':true,'turn_id':'q4'}").constData());
        model.handle(json(R"({'event':'todos','items':[{'id':'T4','text':'d','status':'completed','request_ids':['R4']},
            {'id':'T5','text':'e','status':'completed','request_ids':['R4']}]})"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 1/1"));

        // Re-asking an earlier request starts a new list with it.
        requests((QByteArray(base).replace("'one thing','status':'done'", "'one thing','status':'open','queue_item':'k9'")
                  + ",{'id':'R4','text_preview':'two more','status':'done','delivered':true,'turn_id':'q4'},{'id':'R5','text_preview':'Continue','status':'done','delivered':true,'turn_id':'q4'}").constData());
        QCOMPARE(model.currentBatch(), 5);                          // the re-ask still opens a new list
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));
        QVERIFY(model.inCurrentBatch(QStringLiteral("R2")));
        // Reopened (o) without re-asking: unfinished, and it joins the current list.
        requests((QByteArray(base).replace("'three things','status':'blocked'", "'three things','status':'open'").replace("'one thing','status':'done'", "'one thing','status':'done','queue_item':'k9'")
                  + ",{'id':'R4','text_preview':'two more','status':'done','delivered':true,'turn_id':'q4'},{'id':'R5','text_preview':'Continue','status':'done','delivered':true,'turn_id':'q4'}").constData());
        QCOMPARE(model.currentBatch(), 5);
        QVERIFY(model.inCurrentBatch(QStringLiteral("R1")));
        // R1's todos are all settled, so reopening R1 adds no task: the ledger moved, the list did not.
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));

        // A new chat: the ledger starts over.
        requests("");
        model.handle(json("{'event':'todos','items':[]}"));
        QVERIFY(!model.hasTasks());
        requests("{'id':'R1','text_preview':'fresh','status':'open'}");
        QCOMPARE(model.currentBatch(), 1);
        QCOMPARE(model.chipText(), QStringLiteral("Tasks"));
    }

    // After a restart or /resume the whole ledger arrives at once.
    void batchesRebuiltOnLoad() {
        RequestLedgerModel model;
        model.handle(json(R"({'event':'requests','items':[
          {'id':'R1','text_preview':'a','status':'done','delivered':true,'turn_id':'q1'},
          {'id':'R2','text_preview':'b','status':'done','delivered':true,'turn_id':'q1','source':'steer'},
          {'id':'R3','text_preview':'c','status':'open','delivered':true,'turn_id':'q2'},
          {'id':'R4','text_preview':'d','status':'done','delivered':true,'turn_id':'q3'}]})"));
        model.handle(json(R"({'event':'todos','items':[
          {'id':'T1','text':'a','status':'completed','request_ids':['R1']},
          {'id':'T2','text':'b','status':'completed','request_ids':['R2']},
          {'id':'T3','text':'c','status':'pending','request_ids':['R3']},
          {'id':'T4','text':'d','status':'completed','request_ids':['R4']}]})"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 1/2 (1 unfinished)"));
        QVERIFY(model.inCurrentBatch(QStringLiteral("R3")));
        QVERIFY(!model.inCurrentBatch(QStringLiteral("R2")));
        QCOMPARE(model.earlierSummary().progress(true), QStringLiteral("2/2"));
        // A different session with the same ids replaces the batches.
        model.handle(json(R"({'event':'requests','items':[
          {'id':'R1','text_preview':'other','status':'done','delivered':true,'turn_id':'q1'},
          {'id':'R2','text_preview':'x','status':'done','delivered':true,'turn_id':'q2'},
          {'id':'R3','text_preview':'y','status':'done','delivered':true,'turn_id':'q2'},
          {'id':'R4','text_preview':'z','status':'done','delivered':true,'turn_id':'q2'}]})"));
        QVERIFY(!model.hasTasks());                 // the reset cleared the todos with the ledger
        model.handle(json(R"({'event':'todos','items':[
          {'id':'T1','text':'a','status':'completed','request_ids':['R1']},
          {'id':'T2','text':'b','status':'completed','request_ids':['R2']}]})"));
        QCOMPARE(model.chipText(), QStringLiteral("Tasks 1/1"));
        QCOMPARE(model.earlierSummary().total, 1);
    }

    void earlierInPanel() {
        RequestLedgerModel model;
        model.handle(json(R"({'event':'requests','items':[
          {'id':'R1','text_preview':'first','status':'done','delivered':true,'turn_id':'q1'},
          {'id':'R2','text_preview':'second','status':'open'}]})"));
        model.handle(json(R"({'event':'requests','items':[
          {'id':'R1','text_preview':'first','status':'done','delivered':true,'turn_id':'q1'},
          {'id':'R2','text_preview':'second','status':'in_progress','delivered':true,'turn_id':'q2'}]})"));
        model.handle(json(R"({'event':'todos','items':[
          {'id':'T1','text':'the first list','status':'completed','request_ids':['R1']},
          {'id':'T2','text':'the current one','status':'in_progress','request_ids':['R2']}]})"));
        QCOMPARE(model.currentBatch(), 2);
        RequestsPanel panel(&model);
        model.onChanged = [&] { panel.refresh(); };
        panel.resize(500, 400);
        panel.show();
        panel.enter();
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("requestsList"));
        // The current list's tasks, then a folded Earlier group. No request rows anywhere.
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("◐  T2  the current one"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Earlier · 1/1"));
        QVERIFY(!tree->topLevelItem(1)->isExpanded());
        QCOMPARE(panel.selectedId(), QStringLiteral("T2"));
        panel.select(QStringLiteral("T1"));
        QVERIFY(tree->topLevelItem(1)->isExpanded());
        QCOMPARE(panel.selectedId(), QStringLiteral("T1"));
        QTest::keyClick(tree, Qt::Key_Up);                     // the Earlier row itself: not a task
        QCOMPARE(panel.selectedId(), QString());
    }

    void glyphs() {
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("done")), QStringLiteral("✓"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("in_progress")), QStringLiteral("◐"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("open")), QStringLiteral("○"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("cancelled_by_user")), QStringLiteral("✕"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("blocked")), QStringLiteral("✗"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("deferred")), QStringLiteral("⏸"));
        QCOMPARE(RequestLedgerModel::todoGlyph(QStringLiteral("completed")), QStringLiteral("✓"));
    }

    void inlineLines() {
        const auto items = RequestLedgerModel::parseOpenItems(json(R"({'a':[
            {'kind':'request','id':'R3','status':'open','preview':'also update the docs'},
            {'kind':'request','id':'R4','status':'open','preview':'write tests'},
            {'kind':'todo','id':'T2','status':'pending','preview':'README','request_ids':['R3']}]})").value(QStringLiteral("a")).toArray());
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(2).requestIds, QStringList{QStringLiteral("R3")});
        // Only the model's todos are named: open requests are ledger state the user never sees.
        QCOMPARE(RequestLedgerModel::openItemsLine(items), QStringLiteral("1 task still open: T2 “README”"));
        QVERIFY(RequestLedgerModel::openItemsLine(items.mid(0, 2)).isEmpty());   // two open requests, no todo
        QVERIFY(RequestLedgerModel::openItemsLine({}).isEmpty());
        // Long lists are cut with a count.
        QList<relay::LedgerOpenItem> many;
        for (int i = 0; i < 10; ++i) {
            relay::LedgerOpenItem item;
            item.kind = QStringLiteral("todo"); item.id = QStringLiteral("T%1").arg(i); item.preview = QString(40, 'x');
            many << item;
        }
        QVERIFY(RequestLedgerModel::openItemsLine(many).contains(QStringLiteral("more")));
        // recap.open_items has no kind: requests.
        QCOMPARE(RequestLedgerModel::parseOpenItems(json("{'a':[{'id':'R1','status':'blocked','reason':'x','preview':'p'}]}").value(QStringLiteral("a")).toArray()).first().kind,
                 QStringLiteral("request"));

        QCOMPARE(RequestLedgerModel::limitLine(json("{'stop_reason':'limit','limit':{'which':'steps','steps':50,'max_steps':50,'tool_calls':12,'max_tool_calls':150}}")),
                 QStringLiteral("‖ Stopped at the step limit (50 model steps, limit 50) · unfinished tasks stay open"));
        QVERIFY(RequestLedgerModel::limitLine(json("{'limit':{'which':'tool_calls','tool_calls':151,'max_tool_calls':150}}")).contains(QStringLiteral("tool-call limit (151 tool calls")));
        QCOMPARE(RequestLedgerModel::completionCheckLine(json("{'reminder':1,'max_reminders':2}")), QStringLiteral("✦ checking open items (1/2)"));
        QCOMPARE(RequestLedgerModel::auditLine(json("{'unaddressed':[{'request_id':'R2','quote':'update the docs'}]}")),
                 QStringLiteral("may be unaddressed: “update the docs”"));   // the user's words, not the ledger id
        QVERIFY(RequestLedgerModel::auditLine(json("{'unaddressed':[]}")).isEmpty());
    }

    // The panel is the task list: todos only, no request rows and no ledger actions.
    void panelShowsTasks() {
        RequestLedgerModel model;
        model.handle(json(kRequests));
        model.handle(json(kTodos));
        RequestsPanel panel(&model);
        bool closed = false;
        panel.onClose = [&] { closed = true; };
        model.onChanged = [&] { panel.refresh(); };
        panel.resize(500, 400);
        panel.show();
        panel.enter();
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("requestsList"));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 3);                // T1, T2, T3 — no request rows
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("✓  T1  fix parser"));
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("◐  T2  update README"));
        QCOMPARE(tree->topLevelItem(2)->text(0), QStringLiteral("○  T3  tidy imports"));
        QCOMPARE(panel.selectedId(), QStringLiteral("T2"));    // the first task still running
        auto *detail = panel.findChild<QLabel *>(QStringLiteral("requestDetail"));
        QVERIFY(detail->text().contains(QStringLiteral("update README")));
        QVERIFY2(!detail->text().contains(QStringLiteral("R2")), qPrintable(detail->text()));
        panel.select(QStringLiteral("T3"));
        QCOMPARE(panel.selectedId(), QStringLiteral("T3"));
        QVERIFY(detail->text().contains(QStringLiteral("tidy imports")));
        // A refresh keeps the selection.
        model.handle(json(kRequests));
        QCOMPARE(panel.selectedId(), QStringLiteral("T3"));
        QTest::keyClick(tree, Qt::Key_Escape);
        QVERIFY(closed);
    }

    // With no todos the panel says so instead of listing what the user typed.
    void panelWithoutATaskList() {
        RequestLedgerModel model;
        model.handle(json("{'event':'requests','items':[{'id':'R1','text_preview':'ls the repo','status':'done','delivered':true,'turn_id':'q1'}]}"));
        RequestsPanel panel(&model);
        panel.resize(500, 400);
        panel.show();
        panel.enter();
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("requestsList"));
        QCOMPARE(tree->topLevelItemCount(), 0);
        QCOMPARE(panel.selectedId(), QString());
        auto *detail = panel.findChild<QLabel *>(QStringLiteral("requestDetail"));
        QVERIFY(detail->text().startsWith(QStringLiteral("No task list")));
        QVERIFY2(!detail->text().contains(QStringLiteral("ls the repo")), qPrintable(detail->text()));
    }
};

QTEST_MAIN(RequestsTests)
#include "requests_test.moc"
