// SPDX-License-Identifier: GPL-3.0-or-later
#include "RequestLedger.h"
#include "RequestsPanel.h"

#include <QJsonArray>
#include <QJsonDocument>
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
        QCOMPARE(model.chipText(), QStringLiteral("Requests 2 open"));
        QVERIFY(model.chipToolTip().contains(QStringLiteral("1 deferred")));
        QVERIFY(!model.handle(json("{'event':'done'}")));
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
        QCOMPARE(model.chipText(), QStringLiteral("Requests ✓ 3"));
        QVERIFY(!model.isEmpty());
    }

    void glyphs() {
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("done")), QStringLiteral("✓"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("in_progress")), QStringLiteral("◐"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("open")), QStringLiteral("○"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("cancelled_by_user")), QStringLiteral("✕"));
        QCOMPARE(RequestLedgerModel::statusGlyph(QStringLiteral("blocked")), QStringLiteral("⏸"));
        QCOMPARE(RequestLedgerModel::todoGlyph(QStringLiteral("completed")), QStringLiteral("✓"));
    }

    void inlineLines() {
        const auto items = RequestLedgerModel::parseOpenItems(json(R"({'a':[
            {'kind':'request','id':'R3','status':'open','preview':'also update the docs'},
            {'kind':'request','id':'R4','status':'open','preview':'write tests'},
            {'kind':'todo','id':'T2','status':'pending','preview':'README','request_ids':['R3']}]})").value(QStringLiteral("a")).toArray());
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(2).requestIds, QStringList{QStringLiteral("R3")});
        QCOMPARE(RequestLedgerModel::openItemsLine(items),
                 QStringLiteral("2 requests still open: R3 “also update the docs”, R4 “write tests” · 1 todo open"));
        QVERIFY(RequestLedgerModel::openItemsLine({}).isEmpty());
        // Long lists are cut with a count.
        QList<relay::LedgerOpenItem> many;
        for (int i = 0; i < 10; ++i) { relay::LedgerOpenItem item; item.id = QStringLiteral("R%1").arg(i); item.preview = QString(40, 'x'); many << item; }
        QVERIFY(RequestLedgerModel::openItemsLine(many).contains(QStringLiteral("more")));
        // recap.open_items has no kind: requests.
        QCOMPARE(RequestLedgerModel::parseOpenItems(json("{'a':[{'id':'R1','status':'blocked','reason':'x','preview':'p'}]}").value(QStringLiteral("a")).toArray()).first().kind,
                 QStringLiteral("request"));

        QCOMPARE(RequestLedgerModel::limitLine(json("{'stop_reason':'limit','limit':{'which':'steps','steps':50,'max_steps':50,'tool_calls':12,'max_tool_calls':150}}")),
                 QStringLiteral("‖ Stopped at the step limit (50 model steps, limit 50) · the request stays open"));
        QVERIFY(RequestLedgerModel::limitLine(json("{'limit':{'which':'tool_calls','tool_calls':151,'max_tool_calls':150}}")).contains(QStringLiteral("tool-call limit (151 tool calls")));
        QCOMPARE(RequestLedgerModel::completionCheckLine(json("{'reminder':1,'max_reminders':2}")), QStringLiteral("✦ checking open items (1/2)"));
        QCOMPARE(RequestLedgerModel::auditLine(json("{'unaddressed':[{'request_id':'R2','quote':'update the docs'}]}")),
                 QStringLiteral("may be unaddressed: R2 “update the docs”"));
        QVERIFY(RequestLedgerModel::auditLine(json("{'unaddressed':[]}")).isEmpty());
    }

    void panelKeys() {
        RequestLedgerModel model;
        model.handle(json(kRequests));
        model.handle(json(kTodos));
        RequestsPanel panel(&model);
        QStringList calls;
        panel.onSetStatus = [&](const QString &id, const QString &status) { calls << id + ':' + status; };
        panel.onReask = [&](const QString &id) { calls << id + QStringLiteral(":reask"); };
        panel.onFetch = [&](const QString &id) { calls << id + QStringLiteral(":fetch"); };
        bool closed = false;
        panel.onClose = [&] { closed = true; };
        model.onChanged = [&] { panel.refresh(); };
        panel.resize(500, 400);
        panel.show();
        panel.enter();
        QCOMPARE(panel.selectedId(), QStringLiteral("R2"));   // first open request
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("requestsList"));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 5);                // 4 requests + other todos
        QCOMPARE(tree->topLevelItem(1)->childCount(), 2);      // audit flag + todo
        QVERIFY(tree->topLevelItem(1)->text(0).startsWith(QStringLiteral("◐  R2")));
        QTest::keyClick(tree, Qt::Key_Return);
        QVERIFY(tree->topLevelItem(1)->isExpanded());
        QTest::keyClick(tree, Qt::Key_R);                      // in progress: re-ask refused
        QTest::keyClick(tree, Qt::Key_D);
        QTest::keyClick(tree, Qt::Key_Down);                   // into the children
        QTest::keyClick(tree, Qt::Key_X);                      // acts on the parent request
        QCOMPARE(panel.selectedId(), QStringLiteral("R2"));
        panel.select(QStringLiteral("R1"));
        QTest::keyClick(tree, Qt::Key_D);                      // already done: nothing
        QTest::keyClick(tree, Qt::Key_O);
        QTest::keyClick(tree, Qt::Key_R);
        QCOMPARE(calls, (QStringList{QStringLiteral("R2:fetch"), QStringLiteral("R2:done"), QStringLiteral("R2:cancelled_by_user"),
                                     QStringLiteral("R1:fetch"), QStringLiteral("R1:open"), QStringLiteral("R1:reask")}));
        // A refresh keeps the selection and expansion.
        model.handle(json(kRequests));
        QCOMPARE(panel.selectedId(), QStringLiteral("R1"));
        QVERIFY(tree->topLevelItem(1)->isExpanded());
        QTest::keyClick(tree, Qt::Key_Escape);
        QVERIFY(closed);
    }
};

QTEST_MAIN(RequestsTests)
#include "requests_test.moc"
