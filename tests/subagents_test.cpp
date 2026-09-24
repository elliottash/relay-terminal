// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SubagentTranscript.h"
#include "SubagentsPanel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextBlock>
#include "Theme.h"
#include <QTabBar>
#include <QTest>
#include <QToolButton>

using relay::SubagentModel;
using relay::SubagentsPanel;
using relay::SubagentTabsView;
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
        model.onInline = [this](const QString &line, const QString &) { inlineLines << line; };
        model.onFinished = [this](const relay::SubagentRow &row) { finished << row.id + QLatin1Char(':') + row.status; };
        model.onTranscript = [this](const QString &id, const QJsonObject &e) { transcript << id + QLatin1Char(':') + e.value(QStringLiteral("event")).toString(); };
        model.onStatus = [this](const QString &text) { statuses << text; };
        model.onChanged = [this] { ++changes; };
    }
    void start(const char *id, bool background = true, const char *todoId = "") {
        model.handle(QJsonObject{{"event", "subagent_started"}, {"id", id}, {"type", "explore"},
                                 {"description", "Summarize fixture"}, {"background", background}, {"model", "kimi-k3"},
                                 {"effort", "low"}, {"todo_id", todoId}});
    }
};

// One `todos` event for the strip's task half: each entry is "id:status[:subagent[:running]]".
void feedTodos(relay::RequestLedgerModel *ledger, const QStringList &items) {
    QByteArray body;
    for (const QString &item : items) {
        const QStringList parts = item.split(QLatin1Char(':'));
        if (!body.isEmpty()) body += ",";
        body += "{'id':'" + parts.value(0).toLatin1() + "','text':'text of " + parts.value(0).toLatin1()
                + "','status':'" + parts.value(1).toLatin1() + "','request_ids':[]";
        if (parts.size() > 2) body += ",'subagent':'" + parts.value(2).toLatin1() + "'";
        if (parts.size() > 3) body += ",'subagent_running':true";
        body += "}";
    }
    ledger->handle(json(("{'event':'todos','items':[" + body + "]}").constData()));
}
}  // namespace

class SubagentsTests : public QObject {
    Q_OBJECT
private slots:
    void transcriptMarkdownAndThinkingMatchPane() {
        if (!qEnvironmentVariableIsEmpty("RELAY_SUBAGENT_SCREENSHOT")) relay::theme::applyTheme(*qApp);
        QSettings settings;
        const QVariant previous = settings.value(QStringLiteral("agent/thinking_display"));
        settings.setValue(QStringLiteral("agent/thinking_display"), QStringLiteral("collapse"));
        SubagentTranscriptView view(QStringLiteral("a1"));
        view.resize(720, 600); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *log = view.findChild<QPlainTextEdit *>(QStringLiteral("transcriptView"));
        QVERIFY(log);
        auto send = [&](const QString &kind, const QString &text = QString()) {
            view.handleEvent(QJsonObject{{"event", "subagent_event"}, {"payload", QJsonObject{{"event", kind}, {"text", text}, {"elapsed_ms", 2100}}}});
        };
        auto clickLine = [&](const QString &text) {
            QTextCursor cursor = log->document()->find(text);
            QVERIFY(!cursor.isNull());
            cursor.clearSelection(); cursor.movePosition(QTextCursor::StartOfBlock);
            log->setTextCursor(cursor); log->ensureCursorVisible();
            QTest::mouseClick(log->viewport(), Qt::LeftButton, Qt::NoModifier, log->cursorRect(cursor).center() + QPoint(5, 0));
        };
        send(QStringLiteral("thinking_delta"), QStringLiteral("Compare **both** rendering paths."));
        QVERIFY(view.plainText().contains(QStringLiteral("▾ ✦ thinking…")));
        QVERIFY(view.plainText().contains(QStringLiteral("Compare both rendering paths.")));
        send(QStringLiteral("thinking_delta"), QStringLiteral(" Then inspect the tool folds."));
        QTRY_VERIFY(view.plainText().simplified().contains(QStringLiteral("Then inspect the tool folds.")));
        send(QStringLiteral("thinking_done"));
        QVERIFY(view.plainText().contains(QStringLiteral("▸ ✦ thought for 2.1 s")));
        QVERIFY(!view.plainText().contains(QStringLiteral("Compare both")));
        QCOMPARE(view.toolCallCount(), 0);
        send(QStringLiteral("delta"), QStringLiteral("## Result\nA **bo"));
        send(QStringLiteral("delta"), QStringLiteral("ld** answer with `code`."));
        QVERIFY(view.plainText().endsWith(QStringLiteral("Result\nA bold answer with code.")));
        QVERIFY(!view.plainText().contains(QStringLiteral("**")));
        QTextCursor bold = log->document()->find(QStringLiteral("bold"));
        QVERIFY(!bold.isNull());
        QCOMPARE(bold.charFormat().fontWeight(), int(QFont::Bold));
        QTextCursor code = log->document()->find(QStringLiteral("code"));
        QCOMPARE(code.charFormat().fontWeight(), int(QFont::Bold));
        // Opening an earlier fold must not move the live prose tail to before its detail.
        clickLine(QStringLiteral("thought for"));
        QVERIFY(view.plainText().contains(QStringLiteral("Compare both")));
        send(QStringLiteral("delta"), QStringLiteral(" More text."));
        QVERIFY(view.plainText().contains(QStringLiteral("Compare both")));
        QVERIFY(view.plainText().contains(QStringLiteral("answer with code. More text.")));
        QCOMPARE(view.plainText().count(QStringLiteral("A bold answer")), 1);
        send(QStringLiteral("thinking_delta"), QStringLiteral("Second block"));
        clickLine(QStringLiteral("thinking…"));
        send(QStringLiteral("thinking_delta"), QStringLiteral(" stays hidden"));
        send(QStringLiteral("thinking_done"));
        QVERIFY(!view.plainText().contains(QStringLiteral("stays hidden")));
        view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_started','tool':'run_command','call_id':'c1','preview':'echo ok\\n'}}"));
        view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_output','text':'ok'}}"));
        view.toggleToolCall(0);
        QCOMPARE(view.toolCallCount(), 1);
        QVERIFY(view.plainText().contains(QStringLiteral("    echo ok")));
        // The other display modes share the regular pane's preference.
        settings.setValue(QStringLiteral("agent/thinking_display"), QStringLiteral("always"));
        send(QStringLiteral("thinking_delta"), QStringLiteral("Keep this reasoning visible."));
        send(QStringLiteral("thinking_done"));
        QVERIFY(view.plainText().contains(QStringLiteral("Keep this reasoning visible.")));
        settings.setValue(QStringLiteral("agent/thinking_display"), QStringLiteral("never"));
        send(QStringLiteral("thinking_delta"), QStringLiteral("Hidden by preference."));
        send(QStringLiteral("thinking_done"));
        QVERIFY(!view.plainText().contains(QStringLiteral("Hidden by preference.")));
        if (previous.isValid()) settings.setValue(QStringLiteral("agent/thinking_display"), previous);
        else settings.remove(QStringLiteral("agent/thinking_display"));
        const QString screenshot = qEnvironmentVariable("RELAY_SUBAGENT_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(view.grab().save(screenshot));
    }

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

    void blockedReportIsNotSuccessfulCompletion() {
        Harness h;
        h.start("a1");
        h.model.handle(json("{'event':'subagent_finished','id':'a1','outcome':'blocked','summary':'Cannot read checkout','tools':1,'elapsed_ms':20000}"));
        QCOMPARE(h.model.row(QStringLiteral("a1"))->status, QStringLiteral("blocked"));
        QCOMPARE(h.model.row(QStringLiteral("a1"))->summary, QStringLiteral("Cannot read checkout"));
        QCOMPARE(h.model.liveCount(), 0);
        QCOMPARE(h.finished, QStringList{QStringLiteral("a1:blocked")});
        QCOMPARE(SubagentModel::statusIcon(QStringLiteral("blocked")), QStringLiteral("!"));
        QVERIFY(h.inlineLines.last().contains(QStringLiteral("blocked")));
        h.model.handle(json("{'event':'subagent_started','id':'a1','type':'general','description':'Inspect checkout','resumed':true}"));
        QCOMPARE(h.model.liveCount(), 1);
    }

    void trackerOmitsRoleTagsIncludingHistoricalRows() {
        auto render = [](bool legacyTypes) {
            Harness h;
            SubagentsPanel panel(&h.model);
            const QStringList types{QStringLiteral("general"), QStringLiteral("explore"), QStringLiteral("signal")};
            for (int i = 0; i < 3; ++i) {
                const QString id = QStringLiteral("a%1").arg(i + 1);
                h.model.handle(QJsonObject{{"event", "subagent_started"}, {"id", id},
                    {"type", legacyTypes ? types[i] : QStringLiteral("general")},
                    {"description", QStringLiteral("Inspect checkout %1").arg(i + 1)}, {"model", "fixture"}});
                h.model.handle(QJsonObject{{"event", "subagent_finished"}, {"id", id},
                    {"outcome", i == 1 ? "blocked" : "done"}, {"elapsed_ms", 20000}, {"tools", 1}});
            }
            panel.refresh();
            panel.resize(1000, panel.sizeHint().height());
            panel.show();
            QTest::qWaitForWindowExposed(&panel);
            return panel.grab().toImage();
        };
        const auto general = render(false);
        const auto historical = render(true);
        QCOMPARE(historical, general);
        const QString screenshot = qEnvironmentVariable("RELAY_TRACKER_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(historical.save(screenshot));
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

    // A subagent working on a todo says which one, in the strip, its tab and the ✦ line (card #QHR1).
    void todoSubagentNamesItsTask() {
        Harness h;
        h.model.handle(json("{'event':'subagent_started','id':'a2','type':'general','description':'write the docs','background':true,'todo_id':'T3'}"));
        QCOMPARE(h.model.row(QStringLiteral("a2"))->todoId, QStringLiteral("T3"));
        QCOMPARE(h.model.row(QStringLiteral("a2"))->description, QStringLiteral("T3 · write the docs"));
        QVERIFY2(h.inlineLines.last().endsWith(QStringLiteral("· T3 · write the docs")), qPrintable(h.inlineLines.last()));
        h.model.handle(json("{'event':'agents_status','items':[{'id':'a2','type':'general','description':'write the docs','todo_id':'T3','status':'running'}]}"));
        QCOMPARE(h.model.row(QStringLiteral("a2"))->description, QStringLiteral("T3 · write the docs"));
        h.model.handle(json("{'event':'subagent_started','id':'a4','type':'general','description':'plain','background':true}"));
        QCOMPARE(h.model.row(QStringLiteral("a4"))->description, QStringLiteral("plain"));
        QVERIFY(h.model.row(QStringLiteral("a4"))->todoId.isEmpty());
    }

    // A live foreground subagent blocks the main turn by itself (protocol § 8).
    void foregroundSubagentsAreTrackedForTheWaitLine() {
        Harness h;
        QVERIFY(!h.model.hasLiveForeground());
        h.start("a1");                 // background
        QVERIFY(!h.model.hasLiveForeground());
        h.start("a2", /*background=*/false);
        QVERIFY(h.model.hasLiveForeground());
        QCOMPARE(h.model.liveCount(), 2);
        h.model.handle(json("{'event':'subagent_finished','id':'a2','outcome':'done','summary':'ok'}"));
        QVERIFY(!h.model.hasLiveForeground());
        QCOMPARE(h.model.liveCount(), 1);
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

    void panelModelPicker() {
        Harness h;
        SubagentsPanel panel(&h.model);
        QStringList picked;
        panel.onPickModel = [&](const QString &id, const QPoint &) { picked << id; };
        h.start("a1"); h.start("a2");
        panel.refresh();
        panel.resize(700, panel.sizeHint().height());
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.enter();
        QTest::keyClick(&panel, Qt::Key_Down);
        QTest::keyClick(&panel, Qt::Key_M);       // m on the row
        QCOMPARE(picked, QStringList{QStringLiteral("a2")});
        // A click on a1's chip: it sits left of the metrics, right of the description.
        const QImage image = panel.grab().toImage();
        if (!qEnvironmentVariableIsEmpty("RELAY_SUBAGENTS_SHOT")) image.save(qEnvironmentVariable("RELAY_SUBAGENTS_SHOT"));
        const int rowY = 2 + (panel.fontMetrics().height() + 6) * 3 / 2;
        for (int x = panel.width() - 30; x > panel.width() / 2 && picked.size() < 2; x -= 4)
            QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(x, rowY));
        QCOMPARE(picked.size(), 2);
        QCOMPARE(picked.last(), QStringLiteral("a1"));
        // The worker's answer updates the row and says when it applies.
        h.model.handle(json("{'event':'subagent_model','id':'a1','model':'glm-5.3','applies':'next_step'}"));
        QCOMPARE(h.model.row(QStringLiteral("a1"))->model, QStringLiteral("glm-5.3"));
        QVERIFY(h.statuses.last().contains(QStringLiteral("glm-5.3 from its next step")));
        QVERIFY(h.model.handle(json("{'event':'subagent_model','id':'a9','model':'x'}")));   // unknown: consumed, ignored
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
        // A worker from before protocol § 23 sends no label: the line is built from the preview,
        // and the preview itself stays behind the fold rather than in the log.
        QVERIFY(text.contains(QStringLiteral("▸ ran ls fixture · exit 0")));
        QVERIFY(!text.contains(QStringLiteral("Working directory")));
        QVERIFY(!text.contains(QStringLiteral("notes.md")));   // the output is the fold's, not the log's
        QCOMPARE(view.toolCallCount(), 1);
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

    // A worker built after protocol 23 pairs each landed call with its message (backend
    // transcript_items): the snapshot opens on the same folded tool row the live view shows, the
    // raw json.dumps result stays one fold away, and a call still running keeps its ⚙ name.
    void transcriptSnapshotOpensOnToolRows() {
        SubagentTranscriptView view(QStringLiteral("a1"));
        view.handleEvent(json("{'event':'subagent_transcript','id':'a1','status':'running','messages':["
            "{'role':'user','content':'Fix the failing test in foo.py'},"
            "{'role':'assistant','content':'Reading it first.'},"
            "{'role':'tool','tool':'read_file','tool_call_id':'c1',"
            "'content':'{\\\"ok\\\":true,\\\"path\\\":\\\"/w/tests/test_foo.py\\\",\\\"lines\\\":[\\\"assert 1 == 2\\\"]}',"
            "'label':{'kind':'read','running':'reading tests/test_foo.py','title':'read tests/test_foo.py','ok':true}},"
            "{'role':'assistant','content':'','tool_calls':['edit_file (pending)']},"
            "{'role':'assistant','content':'Done.'}]}"));
        const QString text = view.plainText();
        QVERIFY(text.contains(QStringLiteral("▸ read tests/test_foo.py")));
        QVERIFY(text.contains(QStringLiteral("⚙ edit_file (pending)")));
        QVERIFY(!text.contains(QStringLiteral("assert 1 == 2")));   // not the first paint any more
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("▸ read tests/test_foo.py")});
        QCOMPARE(view.toolCallCount(), 1);
        view.toggleToolCall(0);                                      // the fold holds the raw result
        QVERIFY(view.plainText().contains(QStringLiteral("assert 1 == 2")));
        view.toggleToolCall(0);
        QVERIFY(!view.plainText().contains(QStringLiteral("assert 1 == 2")));
    }

    // Card #TK9C, protocol § 23: one concise line per tool call, rewritten in place when the call
    // lands, with the detail one click away.
    void transcriptDrawsOneLinePerToolCall() {
        SubagentTranscriptView view(QStringLiteral("a1"));
        view.handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'tool_started',"
                              "'tool':'run_command','call_id':'c1','preview':'RUN COMMAND\\n\\npytest -q',"
                              "'label':{'kind':'run','running':'running pytest','title':'ran pytest'}}}"));
        // While it runs the row is the present tense, and it is the only tool line.
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("▸ running pytest")});
        view.handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'tool_result',"
                              "'tool':'run_command','call_id':'c1','ms':8100,'result':{'exit_code':1},"
                              "'label':{'kind':'run','running':'running pytest','title':'ran pytest',"
                              "'stats':['212 lines','exit 1','8 s'],'ok':false,'open':{'type':'fold'}}}}"));
        // The same row, rewritten: one line, not two, and ✗ because it failed.
        QCOMPARE(view.toolCallCount(), 1);
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("✗ ran pytest · 212 lines · exit 1 · 8 s")});
        QVERIFY(!view.plainText().contains(QStringLiteral("running pytest")));
        QVERIFY(!view.plainText().contains(QStringLiteral("pytest -q")));   // the script is behind the fold

        // A click folds the detail open underneath, and a second click folds it shut again.
        view.toggleToolCall(0);
        QVERIFY(view.plainText().contains(QStringLiteral("pytest -q")));
        // A failed row keeps its ✗ while it is open: the detail underneath already says it is.
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("✗ ran pytest · 212 lines · exit 1 · 8 s")});
        view.toggleToolCall(0);
        QVERIFY(!view.plainText().contains(QStringLiteral("pytest -q")));
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("✗ ran pytest · 212 lines · exit 1 · 8 s")});
    }

    void transcriptMergesARunOfReadsAndPrintsAShortDiff() {
        SubagentTranscriptView view(QStringLiteral("a1"));
        auto read = [&](const char *id, const char *name, int lines) {
            const QString started = QStringLiteral(
                "{'event':'subagent_event','payload':{'event':'tool_started','tool':'read_file','call_id':'%1',"
                "'preview':'READ FILE\\n\\n/w/%2','label':{'kind':'read','running':'reading %2','title':'read %2',"
                "'merge':{'key':'read','singular':'file','plural':'files'}}}}").arg(QLatin1String(id), QLatin1String(name));
            const QString landed = QStringLiteral(
                "{'event':'subagent_event','payload':{'event':'tool_result','tool':'read_file','call_id':'%1',"
                "'label':{'kind':'read','running':'reading %2','title':'read %2','stats':['%3 lines'],'ok':true,"
                "'open':{'type':'file','path':'%2'},"
                "'merge':{'key':'read','singular':'file','plural':'files','lines':%3}}}}")
                .arg(QLatin1String(id), QLatin1String(name)).arg(lines);
            view.handleEvent(json(started.toUtf8().constData()));
            view.handleEvent(json(landed.toUtf8().constData()));
        };
        read("c1", "a.py", 100);
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("▸ read a.py · 100 lines")});
        read("c2", "b.py", 200);
        read("c3", "c.py", 700);
        // Three consecutive reads, one line (§ 23.7).
        QCOMPARE(view.toolCallCount(), 1);
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("▸ read 3 files · 1,000 lines")});

        // An edit ends the run, and a diff of at most 12 changed lines prints with no click at all.
        view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_started','tool':'edit_file',"
                              "'call_id':'c4','preview':'EDIT FILE\\n\\n/w/x.py',"
                              "'label':{'kind':'edit','running':'editing x.py','title':'edited x.py','path':'x.py'}}}"));
        view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_result','tool':'edit_file',"
                              "'call_id':'c4','diff':'--- a/x.py\\n+++ b/x.py\\n@@ -1,2 +1,2 @@\\n-old = 1\\n+new = 1\\n',"
                              "'label':{'kind':'edit','running':'editing x.py','title':'edited x.py',"
                              "'stats':['+1 −1'],'ok':true,'path':'x.py','inline_diff':true,'open':{'type':'fold'}}}}"));
        QCOMPARE(view.toolCallCount(), 2);
        QCOMPARE(view.toolLines().last(), QStringLiteral("▸ edited x.py · +1 −1"));
        // #WXT6: a small diff stays behind the row's click too — nothing auto-expands.
        QVERIFY(!view.plainText().contains(QStringLiteral("-old = 1")));
        QVERIFY(!view.plainText().contains(QStringLiteral("@@")));
        view.toggleToolCall(1);
        QCOMPARE(view.toolLines().last(), QStringLiteral("▾ edited x.py · +1 −1"));
        const QString text = view.plainText();
        QVERIFY(text.contains(QStringLiteral("-old = 1")));
        QVERIFY(text.contains(QStringLiteral("+new = 1")));
        QVERIFY(text.contains(QStringLiteral("@@ -1,2 +1,2 @@")));   // the fold shows the hunk headers
    }

    void aBigDiffGoesToTheHostRatherThanTheLog() {
        SubagentTranscriptView view(QStringLiteral("a1"));
        QString title, diff;
        view.onOpenDiff = [&](const QString &t, const QString &d) { title = t; diff = d; };
        view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_started','tool':'write_file',"
                              "'call_id':'c1','label':{'kind':'edit','running':'editing Pane.h','title':'edited Pane.h'}}}"));
        view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_result','tool':'write_file',"
                              "'call_id':'c1','diff':'--- a/src/Pane.h\\n+++ b/src/Pane.h\\n@@ -1 +1 @@\\n-a\\n+b\\n',"
                              "'label':{'kind':'edit','running':'editing Pane.h','title':'edited Pane.h',"
                              "'stats':['+212 −87'],'ok':true,'path':'src/Pane.h','inline_diff':false,"
                              "'open':{'type':'diff'}}}}"));
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("▸ edited Pane.h · +212 −87")});
        QVERIFY(!view.plainText().contains(QStringLiteral("+b")));   // too big to print inline
        view.toggleToolCall(0);
        QCOMPARE(title, QStringLiteral("src/Pane.h"));
        QVERIFY(diff.contains(QStringLiteral("+b")));
        QCOMPARE(view.toolLines(), QStringList{QStringLiteral("▸ edited Pane.h · +212 −87")});   // not folded
    }

    // Card #WD83: a click on a row opens its tab; with the subagent pane open the list is one line.
    void clickOpensAndTheListFolds() {
        Harness h;
        SubagentsPanel panel(&h.model);
        QStringList opened; int panes = 0, mouse = 0, below = 0, exits = 0;
        panel.onOpen = [&](const QString &id) { opened << id; };
        panel.onOpenPane = [&] { ++panes; };
        panel.onMouseOpen = [&] { ++mouse; };
        panel.onBelow = [&] { ++below; };
        panel.onExit = [&] { ++exits; };
        h.start("a1"); h.start("a2");
        panel.refresh();
        panel.resize(700, panel.sizeHint().height());
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        const int rowH = panel.fontMetrics().height() + 6;
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(60, 2 + rowH * 2 + rowH / 2));   // a2's name
        QCOMPARE(opened, QStringList{QStringLiteral("a2")});
        QCOMPARE(mouse, 1);
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(60, 2 + rowH / 2));             // main: no tab
        QCOMPARE(opened.size(), 1);
        const int full = panel.sizeHint().height();
        panel.setFolded(true, QStringLiteral("Alt+A"));
        QCOMPARE(panel.sizeHint().height(), rowH + 6);
        QVERIFY(panel.sizeHint().height() < full);
        QCOMPARE(panel.foldedText(), QStringLiteral("2 subagents running · Alt+A to open"));
        h.model.handle(json("{'event':'subagent_finished','id':'a1','type':'explore','outcome':'done','tools':1,'tokens':10,'elapsed_ms':1000}"));
        QCOMPARE(panel.foldedText(), QStringLiteral("1 subagent running · 1 finished · Alt+A to open"));
        panel.enter();
        QTest::keyClick(&panel, Qt::Key_Return);
        QCOMPARE(panes, 1);
        QTest::keyClick(&panel, Qt::Key_Down);
        QCOMPARE(below, 1);
        QTest::keyClick(&panel, Qt::Key_Escape);
        QCOMPARE(exits, 1);
        panel.resize(700, panel.sizeHint().height());
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(80, 2 + rowH / 2));
        QCOMPARE(panes, 2);
        QCOMPARE(opened.size(), 1);   // folded: a click goes to the pane, not a row
        if (!qEnvironmentVariableIsEmpty("RELAY_SUBAGENTS_FOLD_SHOT")) panel.grab().save(qEnvironmentVariable("RELAY_SUBAGENTS_FOLD_SHOT"));
        panel.setFolded(false);
        QCOMPARE(panel.sizeHint().height(), full);
    }

    void finishedClearedIsTheListsRuleNotAWorkerRestart() {
        Harness h;
        int cleared = 0;
        h.model.onFinishedCleared = [&] { ++cleared; };
        h.start("a1");
        h.model.clearFinished();
        QCOMPARE(cleared, 1);
        h.model.handle(json("{'event':'ready'}"));
        QCOMPARE(cleared, 1);
        h.model.handle(json("{'event':'reset'}"));
        QCOMPARE(cleared, 2);
    }

    void tabsOpenSwitchFollowTheListAndClose() {
        Harness h;
        SubagentTabsView tabs;
        QStringList created; int empties = 0, backs = 0, clicks = 0;
        tabs.onViewCreated = [&](SubagentTranscriptView *view) { created << view->agentId(); };
        tabs.onEmpty = [&] { ++empties; };
        tabs.onBackToMain = [&] { ++backs; };
        tabs.onBackClicked = [&] { ++clicks; };
        h.start("a1"); h.start("a2");
        QVERIFY(tabs.showTab(QStringLiteral("a1")));
        tabs.syncRows(h.model);
        QVERIFY(tabs.showTab(QStringLiteral("a2")));
        tabs.syncRows(h.model);
        QCOMPARE(tabs.ids(), (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
        QCOMPARE(tabs.currentId(), QStringLiteral("a2"));
        // Opening an open one switches to it; nothing is subscribed twice.
        tabs.showTab(QStringLiteral("a1"));
        QCOMPARE(tabs.currentId(), QStringLiteral("a1"));
        QCOMPARE(created, (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
        auto *bar = tabs.findChild<QTabBar *>(QStringLiteral("subagentTabBar"));
        QVERIFY(bar);
        QCOMPARE(bar->tabText(0), QStringLiteral("○ explore a1"));
        h.model.handle(json("{'event':'subagent_progress','id':'a1','status':'running','tools':1}"));
        tabs.syncRows(h.model);
        QCOMPARE(bar->tabText(0), QStringLiteral("● explore a1"));
        QCOMPARE(tabs.title(), QStringLiteral("✦ explore a1 · Summarize fixture"));
        // The ← control and Esc go back to the main agent; the pane stays.
        auto *back = tabs.findChild<QToolButton *>(QStringLiteral("subagentBack"));
        QVERIFY(back);
        QVERIFY(back->text().contains(QStringLiteral("main agent")));
        back->click();
        QCOMPARE(backs, 1); QCOMPARE(clicks, 1);
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        tabs.activateWindow();
        tabs.focusInput();
        auto *input = tabs.current()->findChild<QLineEdit *>(QStringLiteral("subagentInput"));
        QTest::keyClick(input, Qt::Key_Escape);
        QCOMPARE(backs, 2);
        QCOMPARE(tabs.count(), 2);
        if (!qEnvironmentVariableIsEmpty("RELAY_SUBAGENTS_TABS_SHOT")) tabs.grab().save(qEnvironmentVariable("RELAY_SUBAGENTS_TABS_SHOT"));
        // A finished row stays a tab; dismissing it from the list closes the tab.
        h.model.handle(json("{'event':'subagent_finished','id':'a2','type':'explore','outcome':'done','tools':1,'tokens':10,'elapsed_ms':1000}"));
        tabs.syncRows(h.model);
        QCOMPARE(bar->tabText(1), QStringLiteral("✓ explore a2"));
        h.model.dismiss(QStringLiteral("a2"));
        tabs.syncRows(h.model);
        QCOMPARE(tabs.ids(), QStringList{QStringLiteral("a1")});
        QCOMPARE(empties, 0);
        // The tab's × closes it; the last one closes the pane.
        emit bar->tabCloseRequested(0);
        QCOMPARE(tabs.count(), 0);
        QCOMPARE(empties, 1);
    }

    void tabsAutomaticallyIncludeEveryAgentWithoutStealingFocus() {
        if (!qEnvironmentVariableIsEmpty("RELAY_SUBAGENTS_ALL_TABS_SHOT")) relay::theme::applyTheme(*qApp);
        Harness h;
        SubagentTabsView tabs;
        QStringList subscribed;
        tabs.onViewCreated = [&](SubagentTranscriptView *view) {
            subscribed << view->agentId();
            view->appendNote(QStringLiteral("Transcript for ") + view->agentId());
        };
        h.start("a1"); h.start("a2");
        tabs.syncRows(h.model); // the owner's adoptSubagentTabs call, before selecting a row
        QCOMPARE(tabs.ids(), (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
        tabs.showTab(QStringLiteral("a2"));
        tabs.resize(800, 480); tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));
        tabs.activateWindow(); tabs.focusInput();
        auto *input = tabs.current()->findChild<QLineEdit *>(QStringLiteral("subagentInput"));
        input->setText(QStringLiteral("keep my draft"));
        QTRY_VERIFY(input->hasFocus());
        auto *bar = tabs.findChild<QTabBar *>(QStringLiteral("subagentTabBar"));
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->count(), 2);
        if (!qEnvironmentVariableIsEmpty("RELAY_SUBAGENTS_ALL_TABS_SHOT"))
            QVERIFY(tabs.grab().save(qEnvironmentVariable("RELAY_SUBAGENTS_ALL_TABS_SHOT")));
        h.start("a3");
        tabs.syncRows(h.model); tabs.syncRows(h.model);
        QCOMPARE(tabs.count(), 3);
        QCOMPARE(tabs.currentId(), QStringLiteral("a2"));
        QVERIFY(input->hasFocus());
        QCOMPARE(input->text(), QStringLiteral("keep my draft"));
        QCOMPARE(subscribed, (QStringList{QStringLiteral("a1"), QStringLiteral("a2"), QStringLiteral("a3")}));
        // Selecting each tab shows its own view, not the previously selected transcript.
        bar->setCurrentIndex(0);
        QVERIFY(tabs.tab(QStringLiteral("a1"))->isVisible());
        QVERIFY(!tabs.tab(QStringLiteral("a2"))->isVisible());
        bar->setCurrentIndex(1);
        QVERIFY(tabs.tab(QStringLiteral("a2"))->isVisible());
        // A deliberate close stays closed through progress updates; an explicit open revives it.
        tabs.closeTabByUser(QStringLiteral("a1"));
        tabs.syncRows(h.model);
        QCOMPARE(tabs.count(), 2);
        QVERIFY(!tabs.tab(QStringLiteral("a1")));
        tabs.showTab(QStringLiteral("a1"));
        QCOMPARE(tabs.count(), 3);
        QCOMPARE(subscribed.count(QStringLiteral("a1")), 2);
        tabs.closeTabByUser(QStringLiteral("a3"));
        h.model.handle(json("{'event':'ready'}"));
        tabs.syncRows(h.model);
        h.start("a3"); tabs.syncRows(h.model);
        QVERIFY(tabs.tab(QStringLiteral("a3"))); // a new worker may reuse an id
    }

    // Closing a finished agent's tab dismisses its row; a running agent's tab closes alone.
    void closingAFinishedTabDismissesItsRow() {
        Harness h;
        SubagentTabsView tabs;
        tabs.onUserClosed = [&](const QString &id) {   // what Pane wires
            if (const auto *row = h.model.row(id); row && !row->live()) h.model.dismiss(id);
        };
        h.start("a1"); h.start("a2");
        tabs.showTab(QStringLiteral("a1")); tabs.showTab(QStringLiteral("a2"));
        tabs.syncRows(h.model);
        h.model.handle(json("{'event':'subagent_finished','id':'a2','type':'explore','outcome':'done','tools':1,'tokens':10,'elapsed_ms':1000}"));
        tabs.syncRows(h.model);
        tabs.closeTabByUser(QStringLiteral("a2"));
        QVERIFY(!h.model.row(QStringLiteral("a2")));
        QCOMPARE(tabs.ids(), QStringList{QStringLiteral("a1")});
        auto *close = qobject_cast<QToolButton *>(tabs.findChild<QTabBar *>(QStringLiteral("subagentTabBar"))->tabButton(0, QTabBar::RightSide));
        QVERIFY(close);
        close->click();                                  // a1 is running: its row stays
        QVERIFY(h.model.row(QStringLiteral("a1")));
        QCOMPARE(tabs.count(), 0);
    }

    void tabsSurviveARestartAsText() {
        Harness h;
        QJsonObject saved;
        {
            SubagentTabsView tabs;
            tabs.setOwnerKey(QStringLiteral("pane-7")); tabs.setCwd(QStringLiteral("/w"));
            h.start("a1"); h.start("a2");
            tabs.showTab(QStringLiteral("a1"))->handleEvent(json("{'event':'subagent_event','id':'a1','payload':{'event':'delta','text':'three files\\n'}}"));
            tabs.showTab(QStringLiteral("a2"));
            tabs.syncRows(h.model);
            tabs.showTab(QStringLiteral("a1"));
            saved = tabs.node().value(QStringLiteral("subagents")).toObject();
        }
        QCOMPARE(saved.value(QStringLiteral("owner")).toString(), QStringLiteral("pane-7"));
        QCOMPARE(saved.value(QStringLiteral("current")).toString(), QStringLiteral("a1"));
        QCOMPARE(saved.value(QStringLiteral("tabs")).toArray().size(), 2);
        SubagentTabsView restored;
        int empties = 0;
        restored.onEmpty = [&] { ++empties; };
        restored.restore(saved);
        QCOMPARE(restored.ownerKey(), QStringLiteral("pane-7"));
        QCOMPARE(restored.ids(), (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
        QCOMPARE(restored.currentId(), QStringLiteral("a1"));
        QVERIFY(restored.current()->ended());
        QCOMPARE(restored.current()->statusText(), QStringLiteral("stopped"));   // it was running when Relay quit
        QCOMPARE(restored.findChild<QTabBar *>(QStringLiteral("subagentTabBar"))->tabText(0), QStringLiteral("■ explore a1"));
        QVERIFY(restored.current()->plainText().contains(QStringLiteral("three files")));
        // Saved again, the restart note is not stacked.
        QVERIFY(!restored.node().value(QStringLiteral("subagents")).toObject().value(QStringLiteral("tabs")).toArray().first()
                     .toObject().value(QStringLiteral("text")).toString().contains(SubagentTranscriptView::restoredMark()));
        QVERIFY(!restored.current()->findChild<QLineEdit *>(QStringLiteral("subagentInput"))->isEnabled());
        // A new worker's list does not close tabs it never had.
        Harness fresh;
        restored.syncRows(fresh.model);
        QCOMPARE(restored.count(), 2);
        // Selecting a gone agent keeps its text; a live agent with the same id replaces the tab.
        QVERIFY(restored.showTab(QStringLiteral("a2"), false)->ended());
        fresh.start("a2");
        QVERIFY(!restored.showTab(QStringLiteral("a2"), true)->ended());
        QCOMPARE(restored.ids(), (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));   // in place
        // The list's rules (a new prompt) drop what is left from before the restart.
        restored.dropEnded();
        QCOMPARE(restored.ids(), QStringList{QStringLiteral("a2")});
        QCOMPARE(empties, 0);
    }

    // ----- the strip under the composer, task half (owner, 2026-09-19) --------------------------

    // "the open task list could be nice to have underneat the prompt": the strip comes up for tasks
    // alone and goes away when neither half has anything.
    void stripShowsTasksWithoutSubagents() {
        Harness h;
        relay::RequestLedgerModel ledger;
        SubagentsPanel panel(&h.model, &ledger);
        QStringList openedTasks;
        panel.onOpenTask = [&](const QString &id) { openedTasks << id; };
        panel.refresh();
        QVERIFY(panel.isHidden());                     // no subagents, no tasks
        feedTodos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:completed")});
        panel.refresh();
        QVERIFY(panel.isHidden());                     // a finished list is not an open task list
        feedTodos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:in_progress"), QStringLiteral("T3:pending")});
        panel.refresh();
        QVERIFY(!panel.isHidden());
        QCOMPARE(panel.stripLayout().taskRows(), 3);
        QCOMPARE(panel.stripLayout().subagentRows(), 0);
        panel.resize(700, panel.sizeHint().height());
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.enter();
        QCOMPARE(panel.selectedColumn(), int(SubagentsPanel::Tasks));
        QCOMPARE(panel.selectedRow(), 1);
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T1"));
        QVERIFY(panel.selectedId().isEmpty());         // no subagent is selected in the task column
        QTest::keyClick(&panel, Qt::Key_Down);
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T2"));
        QTest::keyClick(&panel, Qt::Key_Return);       // no subagent on T2: the task list, on T2
        QCOMPARE(openedTasks, QStringList{QStringLiteral("T2")});
        QTest::keyClick(&panel, Qt::Key_End);
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T3"));
        // main + 3 task rows, nothing hidden.
        QCOMPARE(panel.sizeHint().height(), 4 * (panel.fontMetrics().height() + 6) + 6);
        // Nothing left open: the strip goes away again.
        feedTodos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:completed"), QStringLiteral("T3:completed")});
        panel.refresh();
        QVERIFY(panel.isHidden());
    }

    // "with subagents on the left and tasks on the right": Left/Right cross between the columns,
    // Enter opens the task's subagent when it has one, and S hands a delegable task to a new one.
    void stripPairsSubagentsWithTasks() {
        Harness h;
        relay::RequestLedgerModel ledger;
        SubagentsPanel panel(&h.model, &ledger);
        QStringList opened, openedTasks, handed;
        int mouseTask = 0;
        panel.onOpen = [&](const QString &id) { opened << id; };
        panel.onOpenTask = [&](const QString &id) { openedTasks << id; };
        panel.onRunTaskAsSubagent = [&](const QString &id) { handed << id; };
        panel.onMouseOpenTask = [&] { ++mouseTask; };
        h.start("a2", true, "T2");
        feedTodos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:in_progress:a2:running"),
                            QStringLiteral("T3:pending")});
        panel.refresh();
        QCOMPARE(panel.stripLayout().subagentRows(), 1);
        QCOMPARE(panel.stripLayout().taskRows(), 3);
        panel.resize(700, panel.sizeHint().height());
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.enter();
        // Row 1 has no agent, so the selection lands on what is there: T1, in the task column.
        QCOMPARE(panel.selectedColumn(), int(SubagentsPanel::Tasks));
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T1"));
        QTest::keyClick(&panel, Qt::Key_Left);         // the nearest left cell below: a2, on row 2
        QCOMPARE(panel.selectedColumn(), int(SubagentsPanel::Subagents));
        QCOMPARE(panel.selectedRow(), 2);
        QCOMPARE(panel.selectedId(), QStringLiteral("a2"));
        QTest::keyClick(&panel, Qt::Key_Return);       // a subagent cell still opens its tab
        QCOMPARE(opened, QStringList{QStringLiteral("a2")});
        QTest::keyClick(&panel, Qt::Key_Right);
        QCOMPARE(panel.selectedColumn(), int(SubagentsPanel::Tasks));
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T2"));
        QTest::keyClick(&panel, Qt::Key_Return);       // T2 has a listed subagent: open it
        QCOMPARE(opened.size(), 2);
        QCOMPARE(opened.last(), QStringLiteral("a2"));
        QVERIFY(openedTasks.isEmpty());
        QTest::keyClick(&panel, Qt::Key_S);            // a2 is already running it: nothing to hand
        QVERIFY(handed.isEmpty());
        QTest::keyClick(&panel, Qt::Key_Down);
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T3"));
        QTest::keyClick(&panel, Qt::Key_X);            // a task is not dismissible
        QTest::keyClick(&panel, Qt::Key_Delete);
        QCOMPARE(h.model.rows().size(), 1);
        QCOMPARE(panel.stripLayout().taskRows(), 3);
        QTest::keyClick(&panel, Qt::Key_S);
        QCOMPARE(handed, QStringList{QStringLiteral("T3")});
        QTest::keyClick(&panel, Qt::Key_Home);
        QCOMPARE(panel.selectedRow(), 0);
        QTest::keyClick(&panel, Qt::Key_S);            // the main row hands out nothing
        QCOMPARE(handed.size(), 1);
        // A click in the right half selects that task and does what Enter does, then teaches the keys.
        const int rowH = panel.fontMetrics().height() + 6;
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(520, 2 + rowH * 3 + rowH / 2));
        QCOMPARE(panel.selectedColumn(), int(SubagentsPanel::Tasks));
        QCOMPARE(panel.selectedTodoId(), QStringLiteral("T3"));
        QCOMPARE(openedTasks, QStringList{QStringLiteral("T3")});
        QCOMPARE(mouseTask, 1);
        // A click in the left half stays on the subagent side.
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(60, 2 + rowH * 2 + rowH / 2));
        QCOMPARE(panel.selectedColumn(), int(SubagentsPanel::Subagents));
        QCOMPARE(opened.size(), 3);
        QCOMPARE(mouseTask, 1);
        if (!qEnvironmentVariableIsEmpty("RELAY_STRIP_SHOT")) panel.grab().save(qEnvironmentVariable("RELAY_STRIP_SHOT"));
    }

    // The strip is as tall as the taller column, never more than five rows, plus the "+N" line.
    void stripHeightFollowsTheTallerColumn() {
        Harness h;
        relay::RequestLedgerModel ledger;
        SubagentsPanel panel(&h.model, &ledger);
        const int rowH = panel.fontMetrics().height() + 6;
        h.start("a1"); h.start("a2");
        panel.refresh();
        QCOMPARE(panel.sizeHint().height(), 3 * rowH + 6);           // main + 2 agents
        feedTodos(&ledger, {QStringLiteral("T1:in_progress"), QStringLiteral("T2:pending"), QStringLiteral("T3:pending"),
                            QStringLiteral("T4:pending")});
        panel.refresh();
        QCOMPARE(panel.stripLayout().rows.size(), 4);                // max(2 agents, 4 tasks)
        QCOMPARE(panel.sizeHint().height(), 5 * rowH + 6);
        feedTodos(&ledger, {QStringLiteral("T1:in_progress"), QStringLiteral("T2:pending"), QStringLiteral("T3:pending"),
                            QStringLiteral("T4:pending"), QStringLiteral("T5:pending"), QStringLiteral("T6:pending"),
                            QStringLiteral("T7:pending")});
        panel.refresh();
        QCOMPARE(panel.stripLayout().rows.size(), 5);                // capped at kMaxVisible
        QCOMPARE(panel.stripLayout().hiddenTasks, 2);
        QCOMPARE(panel.sizeHint().height(), 7 * rowH + 6);           // main + 5 rows + "+2 tasks"
    }

    // The fold exists because the subagent pane is open; with no subagents there is nothing to fold
    // to, so a task-only strip stays open. With both, the one line says how many tasks are open.
    void stripFoldsOnlyWhileSubagentsExist() {
        Harness h;
        relay::RequestLedgerModel ledger;
        SubagentsPanel panel(&h.model, &ledger);
        const int rowH = panel.fontMetrics().height() + 6;
        feedTodos(&ledger, {QStringLiteral("T1:in_progress"), QStringLiteral("T2:pending")});
        panel.refresh();
        panel.setFolded(true, QStringLiteral("Alt+A"));
        QVERIFY(panel.folded());
        QCOMPARE(panel.sizeHint().height(), 3 * rowH + 6);           // still the full strip
        QVERIFY(!panel.isHidden());
        h.start("a1");
        panel.refresh();
        QCOMPARE(panel.sizeHint().height(), rowH + 6);               // now there is a pane to fold into
        QCOMPARE(panel.foldedText(), QStringLiteral("1 subagent running · 2 tasks open · Alt+A to open"));
        feedTodos(&ledger, {QStringLiteral("T1:in_progress"), QStringLiteral("T2:completed")});
        panel.refresh();
        QCOMPARE(panel.foldedText(), QStringLiteral("1 subagent running · 1 task open · Alt+A to open"));
        feedTodos(&ledger, {QStringLiteral("T1:completed"), QStringLiteral("T2:completed")});
        panel.refresh();
        QCOMPARE(panel.foldedText(), QStringLiteral("1 subagent running · Alt+A to open"));   // unchanged wording
    }
};

QTEST_MAIN(SubagentsTests)
#include "subagents_test.moc"
