// SPDX-License-Identifier: GPL-3.0-or-later
#include "SubagentTranscript.h"
#include "SubagentsPanel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTest>

using relay::SubagentModel;
using relay::SubagentsPanel;
using relay::SubagentTranscriptView;

namespace {
// Test JSON uses single quotes to keep the C++ strings readable.
QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }

struct Harness {
    SubagentModel model;
    qint64 now = 1000;
    QStringList inlineLines, finished, transcript, statuses;
    int changes = 0;
    Harness() {
        model.clock = [this] { return now; };
        model.onInline = [this](const QString &line) { inlineLines << line; };
        model.onFinished = [this](const relay::SubagentRow &row) { finished << row.id + QLatin1Char(':') + row.status; };
        model.onTranscript = [this](const QString &id, const QJsonObject &e) { transcript << id + QLatin1Char(':') + e.value(QStringLiteral("event")).toString(); };
        model.onStatus = [this](const QString &text) { statuses << text; };
        model.onChanged = [this] { ++changes; };
    }
    void start(const char *id, bool background = true) {
        model.handle(QJsonObject{{"event", "subagent_started"}, {"id", id}, {"type", "explore"},
                                 {"description", "Summarize fixture"}, {"background", background}, {"model", "kimi-k3"}, {"effort", "low"}});
    }
};
}  // namespace

class SubagentsTests : public QObject {
    Q_OBJECT
private slots:
    void startProgressFinishLifecycle() {
        Harness h;
        h.start("a1");
        QCOMPARE(h.model.rows().size(), 1);
        QCOMPARE(h.model.rows().first().status, QStringLiteral("waiting"));
        QCOMPARE(h.inlineLines.size(), 1);
        QVERIFY(h.inlineLines.first().startsWith(QStringLiteral("✦ explore a1 started in the background · Summarize fixture")));
        QVERIFY(h.model.handle(json("{'event':'subagent_progress','id':'a1','status':'running','tools':2,'tokens':224,'tokens_estimated':true,'elapsed_ms':5000,'last_activity':'run_command: ls'}")));
        const auto *row = h.model.row(QStringLiteral("a1"));
        QVERIFY(row);
        QCOMPARE(row->tools, 2);
        QCOMPARE(row->tokens, qint64(224));
        QCOMPARE(row->lastActivity, QStringLiteral("run_command: ls"));
        // Elapsed keeps counting locally between progress events while the agent is live.
        h.now += 2500;
        QCOMPARE(h.model.elapsedNow(*row), qint64(7500));
        QCOMPARE(h.model.liveCount(), 1);
        // Tool activity never produces terminal lines.
        QCOMPARE(h.inlineLines.size(), 1);
        QVERIFY(h.model.handle(json("{'event':'subagent_finished','id':'a1','type':'explore','outcome':'done','summary':'three files','handoff':'wake','wakeups':1,'max_auto_turns':50,'tools':2,'tokens':224,'elapsed_ms':24403}")));
        QCOMPARE(h.inlineLines.size(), 2);
        QCOMPARE(h.inlineLines.last(), QStringLiteral("✦ explore a1 done · 0:24 · 2 tools · ~224 tok · background agent finished → main agent continues"));
        QCOMPARE(h.finished, QStringList{QStringLiteral("a1:done")});
        row = h.model.row(QStringLiteral("a1"));
        QCOMPARE(h.model.elapsedNow(*row), qint64(24403));   // frozen once finished
        QCOMPARE(h.model.liveCount(), 0);
    }

    void handoffPendingBeyondCap() {
        Harness h;
        h.start("a2");
        h.model.handle(json("{'event':'subagent_finished','id':'a2','type':'explore','outcome':'done','handoff':'pending','wakeups':1,'max_auto_turns':1,'tools':0,'tokens':10,'elapsed_ms':1000}"));
        QVERIFY(h.inlineLines.last().contains(QStringLiteral("automatic-turn limit reached (1/1)")));
        QVERIFY(h.model.handle(json("{'event':'subagent_handoff','id':'a2','handoff':'wake','wakeups':1,'max_auto_turns':0}")));
        QCOMPARE(h.inlineLines.last(), QStringLiteral("✦ a2 result → main agent continues"));
        QCOMPARE(h.model.row(QStringLiteral("a2"))->handoff, QStringLiteral("wake"));
    }

    void transcriptEventsAreRoutedAndConsumed() {
        Harness h;
        h.start("a1");
        QVERIFY(h.model.handle(json("{'event':'subagent_transcript','id':'a1','status':'running','messages':[]}")));
        QVERIFY(h.model.handle(json("{'event':'subagent_event','id':'a1','payload':{'event':'delta','text':'hi'}}")));
        QCOMPARE(h.transcript, (QStringList{QStringLiteral("a1:subagent_transcript"), QStringLiteral("a1:subagent_event")}));
        QVERIFY(h.model.handle(json("{'event':'agent_stopped','ids':['a1']}")));
        QVERIFY(h.model.handle(json("{'event':'agent_message_delivered','id':'a1','delivered':'resumed','status':'running'}")));
        QVERIFY(h.statuses.last().contains(QStringLiteral("resumed")));
    }

    void mainEventsAreObservedNotConsumed() {
        Harness h;
        QVERIFY(!h.model.handle(json("{'event':'agent_started','id':'x'}")));
        QVERIFY(h.model.mainBusy());
        QVERIFY(!h.model.handle(json("{'event':'context','used_tokens':38200,'window':262144}")));
        h.start("a1");
        h.model.handle(json("{'event':'subagent_progress','id':'a1','status':'running','tokens':1234,'tokens_estimated':true,'elapsed_ms':1}"));
        QCOMPARE(h.model.tokenSplit(), QStringLiteral("main ctx 38.2k · agents ~1.2k tok"));
        QVERIFY(!h.model.handle(json("{'event':'agent_finished','id':'x','outcome':'done'}")));
        QVERIFY(!h.model.mainBusy());
        QVERIFY(!h.model.handle(json("{'event':'delta','text':'x'}")));
    }

    void dismissAndClearKeepLiveRows() {
        Harness h;
        h.start("a1"); h.start("a2");
        h.model.handle(json("{'event':'subagent_finished','id':'a1','outcome':'stopped','handoff':'returned'}"));
        h.model.dismiss(QStringLiteral("a2"));   // live rows cannot be dismissed
        QCOMPARE(h.model.rows().size(), 2);
        h.model.clearFinished();
        QCOMPARE(h.model.rows().size(), 1);
        QCOMPARE(h.model.rows().first().id, QStringLiteral("a2"));
        h.model.handle(json("{'event':'ready'}"));
        QVERIFY(h.model.isEmpty());
    }

    void resumedAgentRestartsRow() {
        Harness h;
        h.start("a1", false);
        h.model.handle(json("{'event':'subagent_finished','id':'a1','outcome':'done','handoff':'returned','elapsed_ms':3000}"));
        h.model.handle(json("{'event':'subagent_started','id':'a1','type':'explore','description':'Summarize fixture','background':true,'resumed':true}"));
        QCOMPARE(h.model.rows().size(), 1);
        QVERIFY(h.model.row(QStringLiteral("a1"))->live());
        QVERIFY(h.inlineLines.last().startsWith(QStringLiteral("✦ explore a1 resumed")));
    }

    void formatting() {
        QCOMPARE(SubagentModel::formatElapsed(41200), QStringLiteral("0:41"));
        QCOMPARE(SubagentModel::formatElapsed(3723000), QStringLiteral("1:02:03"));
        QCOMPARE(SubagentModel::formatTokens(999, false), QStringLiteral("999"));
        QCOMPARE(SubagentModel::formatTokens(22000, true), QStringLiteral("~22k"));
        QCOMPARE(SubagentModel::formatTokens(250000, false), QStringLiteral("250k"));
        QCOMPARE(SubagentModel::statusIcon(QStringLiteral("failed")), QStringLiteral("✗"));
    }

    void definitionsList() {
        Harness h;
        QVERIFY(h.model.handle(json("{'event':'agents','items':[{'name':'explore','source':'builtin','model':'inherit','tools':['read_file']}],'duplicates':[{'name':'x','source':'/a','kept':'/b'}],'skipped':[{'path':'/c','reason':'bad'}]}")));
        QCOMPARE(h.model.definitions().size(), 1);
        QCOMPARE(h.model.definitionWarnings().size(), 2);
    }

    void panelKeyboardNavigation() {
        Harness h;
        SubagentsPanel panel(&h.model);
        QString opened, stopped; int exits = 0;
        panel.onOpen = [&](const QString &id) { opened = id; };
        panel.onStop = [&](const QString &id) { stopped = id; };
        panel.onExit = [&] { ++exits; };
        panel.refresh();
        QVERIFY(panel.isHidden());   // hidden with no subagents
        h.start("a1"); h.start("a2");
        panel.refresh();
        panel.resize(700, panel.sizeHint().height());
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.enter();
        QCOMPARE(panel.selectedId(), QStringLiteral("a1"));
        QTest::keyClick(&panel, Qt::Key_Down);
        QCOMPARE(panel.selectedId(), QStringLiteral("a2"));
        QTest::keyClick(&panel, Qt::Key_Down);   // stays on the last row
        QCOMPARE(panel.selectedId(), QStringLiteral("a2"));
        QTest::keyClick(&panel, Qt::Key_Return);
        QCOMPARE(opened, QStringLiteral("a2"));
        QTest::keyClick(&panel, Qt::Key_X);
        QCOMPARE(stopped, QStringLiteral("a2"));
        QCOMPARE(h.model.rows().size(), 2);      // live: stop, not dismiss
        h.model.handle(json("{'event':'subagent_finished','id':'a2','outcome':'stopped','handoff':'returned'}"));
        QTest::keyClick(&panel, Qt::Key_Delete);  // finished: dismiss
        QCOMPARE(h.model.rows().size(), 1);
        QCOMPARE(panel.selectedId(), QStringLiteral("a1"));
        QTest::keyClick(&panel, Qt::Key_Up);      // main row
        QCOMPARE(panel.selectedRow(), 0);
        QTest::keyClick(&panel, Qt::Key_Up);      // past the top: back to the composer
        QCOMPARE(exits, 1);
        QTest::keyClick(&panel, Qt::Key_Escape);
        QCOMPARE(exits, 2);
    }

    void panelShowsAtMostFiveRows() {
        Harness h;
        SubagentsPanel panel(&h.model);
        h.start("a1");
        panel.refresh();
        const int one = panel.sizeHint().height();
        for (const char *id : {"a2", "a3", "a4", "a5", "a6", "a7", "a8"}) h.start(id);
        panel.refresh();
        // main + 5 rows + "+N more"
        QCOMPARE(panel.sizeHint().height() - one, 5 * (panel.fontMetrics().height() + 6));
    }

    void transcriptViewRendersSnapshotAndStream() {
        SubagentTranscriptView view(QStringLiteral("a1"));
        QStringList sent; int closes = 0;
        view.onSend = [&](const QString &text) { sent << text; };
        view.onClose = [&] { ++closes; };
        view.handleEvent(json("{'event':'subagent_transcript','id':'a1','status':'running','messages':[{'role':'user','content':'List the fixture files'},{'role':'assistant','content':'','tool_calls':['run_command']},{'role':'tool','content':'alpha.txt\\nbeta.txt'}]}"));
        view.handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'tool_started','tool':'run_command','preview':'RUN COMMAND\\n\\nWorking directory: /w\\nTimeout: 30s\\n\\nls fixture'}}"));
        view.handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'tool_output','text':'notes.md\\n'}}"));
        view.handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'tool_result','tool':'run_command','result':{'exit_code':0}}}"));
        view.handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'delta','text':'Found three \\u001b[31mfiles.'}}"));
        const QString text = view.plainText();
        QVERIFY(text.contains(QStringLiteral("› List the fixture files")));
        QVERIFY(text.contains(QStringLiteral("⚙ run_command")));
        QVERIFY(text.contains(QStringLiteral("⚙ $ ls fixture")));
        QVERIFY(!text.contains(QStringLiteral("Working directory")));
        QVERIFY(text.contains(QStringLiteral("exit 0")));
        QVERIFY(text.contains(QStringLiteral("Found three [31mfiles.")));   // escape byte stripped
        relay::SubagentRow row; row.id = QStringLiteral("a1"); row.type = QStringLiteral("explore"); row.description = QStringLiteral("Summarize");
        row.status = QStringLiteral("running"); row.tools = 2; row.tokens = 1500;
        view.setRow(row, 61000);
        QCOMPARE(view.title(), QStringLiteral("✦ explore a1 · Summarize"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.activateWindow();
        view.focusInput();
        auto *input = view.findChild<QLineEdit *>(QStringLiteral("subagentInput"));
        QVERIFY(input);
        QTest::keyClicks(input, QStringLiteral("also count lines"));
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(sent, QStringList{QStringLiteral("also count lines")});
        QVERIFY(input->text().isEmpty());
        QTest::keyClick(input, Qt::Key_Escape);
        QCOMPARE(closes, 1);
    }
};

QTEST_MAIN(SubagentsTests)
#include "subagents_test.moc"
