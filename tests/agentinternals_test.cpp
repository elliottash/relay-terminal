// SPDX-License-Identifier: GPL-3.0-or-later
// The agent internals pane's log (src/AgentInternalsView.h, card #QT8C): a rule per turn, a
// reasoning block rewritten in place while it streams, one row per tool call that a click folds
// open, and a run of reads merged into one row.
#include "AgentInternalsView.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using relay::AgentInternalsView;

namespace {
QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }
}  // namespace

class AgentInternalsTests : public QObject {
    Q_OBJECT
private slots:
    void aTurnIsARuleWithTheRequestsFirstLine() {
        AgentInternalsView view;
        view.beginTurn("t1", "fix the build\nand then the tests");
        view.beginTurn("t1", "fix the build");   // the same turn again draws nothing
        QCOMPARE(view.turnCount(), 1);
        QVERIFY(view.plainText().contains(QStringLiteral("── fix the build ──")));
        QVERIFY(!view.plainText().contains(QStringLiteral("and then the tests")));
        view.beginTurn("t2", "");
        QCOMPARE(view.turnCount(), 2);
        QVERIFY(view.plainText().contains(QStringLiteral("── agent turn ──")));
    }

    void reasoningStreamsIntoOneBlockRewrittenInPlace() {
        AgentInternalsView view;
        view.beginTurn("t1", "think");
        view.setThinking("t1", "thinking", "First the **plan**", false, 0);
        QVERIFY(view.plainText().contains(QStringLiteral("✦ thinking…")));
        QVERIFY(view.plainText().contains(QStringLiteral("plan")));
        view.setThinking("t1", "thinking", "First the **plan**\n\nthen the code", false, 0);
        // One header, not one per delta: the block was rewritten, not appended.
        QCOMPARE(view.plainText().count(QStringLiteral("✦ thinking…")), 1);
        QVERIFY(view.plainText().contains(QStringLiteral("then the code")));
        view.setThinking("t1", "thinking", "First the **plan**\n\nthen the code", true, 4200);
        QCOMPARE(view.plainText().count(QStringLiteral("✦ thinking…")), 0);
        QCOMPARE(view.plainText().count(QStringLiteral("✦ thought for 4 s")), 1);
        // A second block of the same turn is its own header under the first.
        view.setThinking("t1", "thinking-2", "more", true, 900);
        QVERIFY(view.plainText().contains(QStringLiteral("✦ thought for 1 s")));
        QCOMPARE(view.plainText().count(QStringLiteral("✦ thought for")), 2);
    }

    void aToolCallIsOneRowRunningThenSettled() {
        AgentInternalsView view;
        view.beginTurn("t1", "run the tests");
        view.toolStarted(json("{'call_id':'c1','turn_id':'t1','tool':'run_command',"
                              " 'label':{'kind':'run','running':'running pytest','title':'ran pytest'}}"));
        QCOMPARE(view.toolRowCount(), 1);
        QCOMPARE(view.toolLines().first(), QStringLiteral("▸ running pytest…"));
        view.toolOutput("line one\nline two\n");
        QVERIFY(view.toolLines().first().contains(QStringLiteral("2 lines")));
        view.toolResult(json("{'call_id':'c1','turn_id':'t1','tool':'run_command','ok':true,"
                             " 'label':{'kind':'run','running':'running pytest','title':'ran pytest',"
                             "          'stats':['212 lines','exit 1','8 s'],'ok':true}}"));
        QCOMPARE(view.toolRowCount(), 1);
        QCOMPARE(view.toolLines().first(), QStringLiteral("▸ ran pytest · 212 lines · exit 1 · 8 s"));
        // The settled row is in the log once, where the running one was.
        QCOMPARE(view.plainText().count(QStringLiteral("ran pytest")), 1);
        QCOMPARE(view.plainText().count(QStringLiteral("running pytest")), 0);
    }

    void aFailedCallTakesTheCross() {
        AgentInternalsView view;
        view.toolResult(json("{'call_id':'c1','turn_id':'t1','tool':'edit_file','ok':false,"
                             " 'label':{'kind':'edit','running':'editing x.py','title':'edit x.py',"
                             "          'error':'old_string was not found','ok':false}}"));
        QCOMPARE(view.toolLines().first(), QStringLiteral("▸ edit x.py ✗ · old_string was not found"));
    }

    void aRunOfReadsIsOneRow() {
        AgentInternalsView view;
        const char *first = "{'call_id':'c1','turn_id':'t1','tool':'read_file','ok':true,"
                            " 'label':{'kind':'read','running':'reading a.py','title':'read a.py','stats':['40 lines'],"
                            "          'ok':true,'merge':{'key':'read','singular':'file','plural':'files','lines':40}}}";
        const char *second = "{'call_id':'c2','turn_id':'t1','tool':'read_file','ok':true,"
                             " 'label':{'kind':'read','running':'reading b.py','title':'read b.py','stats':['50 lines'],"
                             "          'ok':true,'merge':{'key':'read','singular':'file','plural':'files','lines':50}}}";
        view.toolStarted(json(first));
        view.toolResult(json(first));
        QCOMPARE(view.toolRowCount(), 1);
        view.toolStarted(json(second));   // inside the run: no row of its own
        QCOMPARE(view.toolRowCount(), 1);
        view.toolResult(json(second));
        QCOMPARE(view.toolRowCount(), 1);
        QVERIFY2(view.toolLines().first().contains(QStringLiteral("read 2 files")), qPrintable(view.toolLines().first()));
        QVERIFY(view.toolLines().first().contains(QStringLiteral("90 lines")));
        // Unfolded, the run lists its members.
        view.toggleToolCall(0);
        QVERIFY(view.plainText().contains(QStringLiteral("read a.py")));
        QVERIFY(view.plainText().contains(QStringLiteral("read b.py")));
        QCOMPARE(view.toolLines().first().left(1), QStringLiteral("▾"));
        view.toggleToolCall(0);
        QVERIFY(!view.plainText().contains(QStringLiteral("read a.py")));
    }

    void aCallIdReusedInALaterTurnIsItsOwnRow() {
        // A worker numbers calls per turn: c1 in turn 2 must not rewrite turn 1's c1 row.
        AgentInternalsView view;
        view.beginTurn("t1", "first");
        view.toolResult(json("{'call_id':'c1','turn_id':'t1','tool':'run_command','ok':true,"
                             " 'label':{'kind':'run','title':'ran first','ok':true}}"));
        view.beginTurn("t2", "second");
        view.toolStarted(json("{'call_id':'c1','turn_id':'t2','tool':'run_command',"
                              " 'label':{'kind':'run','running':'running second','title':'ran second'}}"));
        view.toolResult(json("{'call_id':'c1','turn_id':'t2','tool':'run_command','ok':true,"
                             " 'label':{'kind':'run','title':'ran second','ok':true}}"));
        QCOMPARE(view.toolRowCount(), 2);
        QCOMPARE(view.toolLines().at(0), QStringLiteral("▸ ran first"));
        QCOMPARE(view.toolLines().at(1), QStringLiteral("▸ ran second"));
    }

    void aClickAsksTheWorkerAndTheReplyFoldsOpen() {
        AgentInternalsView view;
        QString askedTurn, askedCall;
        view.onOpenOutput = [&](const QString &turn, const QString &call) { askedTurn = turn; askedCall = call; };
        view.toolResult(json("{'call_id':'c1','turn_id':'t1','tool':'run_command','ok':true,"
                             " 'label':{'kind':'run','running':'running pytest','title':'ran pytest','ok':true}}"));
        view.toggleToolCall(0);
        QCOMPARE(askedTurn, QString("t1"));
        QCOMPARE(askedCall, QString("c1"));
        QVERIFY(view.plainText().contains(QStringLiteral("asking the agent")));
        view.setToolOutput(json("{'call_id':'c1','turn_id':'t1','stored':true,'tool':'run_command',"
                                " 'label':{'kind':'run','title':'ran pytest','ok':true},"
                                " 'detail':[{'heading':'output','style':'output','text':'14 passed in 0.3 s'}]}"));
        QVERIFY2(view.plainText().contains(QStringLiteral("14 passed")), qPrintable(view.plainText()));
        QVERIFY(!view.plainText().contains(QStringLiteral("asking the agent")));
        QCOMPARE(view.toolLines().first().left(1), QStringLiteral("▾"));
        // Folded shut again: the detail leaves, the row stays.
        view.toggleToolCall(0);
        QVERIFY(!view.plainText().contains(QStringLiteral("14 passed")));
        QCOMPARE(view.toolRowCount(), 1);
        // The worker could not answer a later click: the fold says so.
        view.setToolOutputError("c1", "Unknown turn_id (only the last 50 turns are kept)");
        QVERIFY(view.plainText().contains(QStringLiteral("only the last 50 turns")));
    }

    void aBigDiffGoesToTheDiffPane() {
        AgentInternalsView view;
        QString openedTitle, openedDiff;
        view.onOpenDiff = [&](const QString &title, const QString &diff) { openedTitle = title; openedDiff = diff; };
        view.toolResult(json("{'call_id':'c1','turn_id':'t1','tool':'write_file','ok':true,"
                             " 'diff':'--- a/x.py\\n+++ b/x.py\\n@@ -1 +1 @@\\n-a\\n+b\\n',"
                             " 'label':{'kind':'edit','title':'wrote x.py','path':'x.py','ok':true,'open':{'type':'diff'}}}"));
        view.toggleToolCall(0);
        QCOMPARE(openedTitle, QString("x.py"));
        QVERIFY(openedDiff.contains(QStringLiteral("+b")));
        QCOMPARE(view.toolLines().first().left(1), QStringLiteral("▸"));   // nothing folded open here
    }

    void aNoteIsSaidOnce() {
        AgentInternalsView view;
        view.note("Reasoning display is off");
        view.note("Reasoning display is off");
        QCOMPARE(view.plainText().count(QStringLiteral("Reasoning display is off")), 1);
    }
};

QTEST_MAIN(AgentInternalsTests)
#include "agentinternals_test.moc"
