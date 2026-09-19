// SPDX-License-Identifier: GPL-3.0-or-later
// The turn pane's tool list and its detail log (src/TurnTranscript.h, protocol § 23).
#include "TurnTranscript.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItem>

using relay::TurnTranscriptView;

namespace {
// Test JSON uses single quotes to keep the C++ strings readable.
QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }

QStringList rows(const TurnTranscriptView &view) {
    const auto *tree = view.findChild<QTreeWidget *>(QStringLiteral("turnTools"));
    QStringList out;
    for (int at = 0; tree && at < tree->topLevelItemCount(); ++at) out << tree->topLevelItem(at)->text(0);
    return out;
}

QTreeWidgetItem *row(const TurnTranscriptView &view, int at) {
    const auto *tree = view.findChild<QTreeWidget *>(QStringLiteral("turnTools"));
    return tree ? tree->topLevelItem(at) : nullptr;
}

QString log(const TurnTranscriptView &view) {
    const auto *edit = view.findChild<QPlainTextEdit *>(QStringLiteral("turnLog"));
    return edit ? edit->toPlainText() : QString();
}
}  // namespace

class TurnTranscriptTests : public QObject {
    Q_OBJECT
private slots:
    void rowsAreTheLabelsLineNotTheToolName() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setSummary(json(
            "{'turn_id':'t1','elapsed_ms':12000,'tools':["
            "{'call_id':'c1','name':'run_command','ok':true,'ms':1200,'preview':'RUN COMMAND\\n\\npytest',"
            " 'label':{'kind':'run','running':'running pytest','title':'ran pytest','stats':['14 lines','exit 0','1.2 s']}},"
            "{'call_id':'c2','name':'edit_file','ok':false,'ms':40,"
            " 'label':{'kind':'edit','running':'editing x.py','title':'edit x.py',"
            "          'error':'old_string was not found in the file','open':{'type':'fold'}}}]}"));
        QCOMPARE(view.toolCount(), 2);
        QCOMPARE(rows(view), (QStringList{QStringLiteral("✓ ran pytest · 14 lines · exit 0 · 1.2 s"),
                                          QStringLiteral("✗ edit x.py · old_string was not found in the file")}));
        // The exact duration is the second column, finer than the stats' rounded seconds.
        QCOMPARE(row(view, 0)->text(1), QStringLiteral("1.2 s"));
        QCOMPARE(row(view, 1)->text(1), QStringLiteral("40 ms"));
        QCOMPARE(row(view, 0)->data(0, Qt::UserRole).toString(), QStringLiteral("c1"));
        QCOMPARE(view.title(), QStringLiteral("Turn · 2 tool calls"));
        // No preview line is spelled out beside the label any more.
        QVERIFY(!rows(view).first().contains(QStringLiteral("run_command")));
    }

    void aRunOfReadsIsOneRowWithItsCallsUnderIt() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setSummary(json(
            "{'tools':["
            "{'call_id':'c1','name':'read_file','ok':true,'label':{'kind':'read','title':'read a.py',"
            " 'running':'reading a.py','stats':['100 lines'],"
            " 'merge':{'key':'read','singular':'file','plural':'files','lines':100}}},"
            "{'call_id':'c2','name':'read_file','ok':true,'label':{'kind':'read','title':'read b.py',"
            " 'running':'reading b.py','stats':['300 lines'],"
            " 'merge':{'key':'read','singular':'file','plural':'files','lines':300}}},"
            "{'call_id':'c3','name':'read_file','ok':true,'label':{'kind':'read','title':'read c.py',"
            " 'running':'reading c.py','stats':['600 lines'],"
            " 'merge':{'key':'read','singular':'file','plural':'files','lines':600}}},"
            "{'call_id':'c4','name':'run_command','ok':true,'label':{'kind':'run','title':'ran git status',"
            " 'running':'running git status','stats':['6 lines']}}]}"));
        // Three reads, one row — but the turn still had four calls, and each read is still openable.
        QCOMPARE(view.toolCount(), 4);
        QCOMPARE(rows(view), (QStringList{QStringLiteral("▸ read 3 files · 1,000 lines"),
                                          QStringLiteral("✓ ran git status · 6 lines")}));
        QCOMPARE(row(view, 0)->childCount(), 3);
        QCOMPARE(row(view, 0)->child(0)->data(0, Qt::UserRole).toString(), QStringLiteral("c1"));
        QCOMPARE(row(view, 0)->child(2)->text(0), QStringLiteral("✓ read c.py · 600 lines"));
        QVERIFY(row(view, 0)->data(0, Qt::UserRole).toString().isEmpty());   // the run itself opens nothing
    }

    void aFailedReadStandsOnItsOwn() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setSummary(json(
            "{'tools':["
            "{'call_id':'c1','name':'read_file','ok':true,'label':{'kind':'read','title':'read a.py',"
            " 'merge':{'key':'read','singular':'file','plural':'files','lines':100}}},"
            "{'call_id':'c2','name':'read_file','ok':false,'label':{'kind':'read','title':'read gone.py',"
            " 'error':'no such file','ok':false}},"
            "{'call_id':'c3','name':'read_file','ok':true,'label':{'kind':'read','title':'read b.py',"
            " 'merge':{'key':'read','singular':'file','plural':'files','lines':50}}}]}"));
        QCOMPARE(rows(view), (QStringList{QStringLiteral("✓ read a.py"),
                                          QStringLiteral("✗ read gone.py · no such file"),
                                          QStringLiteral("✓ read b.py")}));
        // The failure spells its reason out underneath as well.
        QCOMPARE(row(view, 1)->child(0)->text(0), QStringLiteral("no such file"));
    }

    void aWorkerWithoutLabelsKeepsItsPreview() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setSummary(json(
            "{'tools':[{'call_id':'c1','name':'run_command','ok':true,'exit_code':0,"
            " 'preview':'RUN COMMAND\\n\\ngit status\\n\\nWorking directory: /w\\nTimeout: 30s'}]}"));
        QCOMPARE(rows(view), QStringList{QStringLiteral("✓ ran git status · exit 0")});
        QCOMPARE(row(view, 0)->childCount(), 1);            // the whole preview, as before § 23
        QVERIFY(row(view, 0)->child(0)->text(0).contains(QStringLiteral("Working directory")));
    }

    void theLogRendersTheDetailSections() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setToolOutput(json(
            "{'call_id':'c1','label':{'kind':'run','title':'ran grep','running':'running grep',"
            " 'stats':['37 lines'],'ok':true,'open':{'type':'fold'}},"
            " 'detail':[{'heading':'command','style':'code','text':'grep -rn needle backend/'},"
            "           {'heading':'output','style':'output','text':'backend/x.py:1:needle','truncated':true}]}"));
        const QString text = log(view);
        QVERIFY(text.contains(QStringLiteral("✓ ran grep · 37 lines")));
        QVERIFY(text.contains(QStringLiteral("command")));
        QVERIFY(text.contains(QStringLiteral("grep -rn needle backend/")));
        QVERIFY(text.contains(QStringLiteral("output")));
        QVERIFY(text.contains(QStringLiteral("backend/x.py:1:needle")));
        QVERIFY(text.contains(QStringLiteral("(truncated)")));
    }

    void aBigDiffGoesToTheHostsDiffPane() {
        TurnTranscriptView view(QStringLiteral("t1"));
        QString title, diff;
        view.onOpenDiff = [&](const QString &t, const QString &d) { title = t; diff = d; };
        view.setToolOutput(json(
            "{'call_id':'c1','label':{'kind':'edit','title':'edited Pane.h','running':'editing Pane.h',"
            " 'stats':['+212 −87'],'ok':true,'path':'src/Pane.h','inline_diff':false,'open':{'type':'diff'}},"
            " 'diff':'--- a/src/Pane.h\\n+++ b/src/Pane.h\\n@@ -1 +1 @@\\n-a\\n+b\\n',"
            " 'detail':[{'heading':'diff','style':'diff','text':'--- a/src/Pane.h\\n+++ b/src/Pane.h\\n-a\\n+b'}]}"));
        QCOMPARE(title, QStringLiteral("src/Pane.h"));
        QVERIFY(diff.contains(QStringLiteral("+b")));
        // It is not written into the log twice.
        QVERIFY(log(view).contains(QStringLiteral("(opened in a diff pane)")));
        QVERIFY(!log(view).contains(QStringLiteral("@@")));
    }

    void aSmallDiffStaysInTheLog() {
        TurnTranscriptView view(QStringLiteral("t1"));
        int opened = 0;
        view.onOpenDiff = [&](const QString &, const QString &) { ++opened; };
        view.setToolOutput(json(
            "{'call_id':'c1','label':{'kind':'edit','title':'edited x.py','running':'editing x.py',"
            " 'stats':['+1 −1'],'ok':true,'path':'x.py','inline_diff':true,'open':{'type':'fold'}},"
            " 'detail':[{'heading':'diff','style':'diff','text':'-old = 1\\n+new = 1'}]}"));
        QCOMPARE(opened, 0);
        QVERIFY(log(view).contains(QStringLiteral("-old = 1")));
        QVERIFY(log(view).contains(QStringLiteral("+new = 1")));
    }

    void aWorkerWithoutSectionsStillShowsItsText() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setToolOutput(json("{'call_id':'c1','text':'alpha.txt\\nbeta.txt'}"));
        QVERIFY(log(view).contains(QStringLiteral("alpha.txt")));
        view.setToolOutput(json("{'call_id':'c1'}"));
        QVERIFY(log(view).contains(QStringLiteral("No output recorded")));
    }

    // #K48R, owner's report: "the open in pane link on the thinking fold doesnt work". It opened
    // this pane — and the reasoning was not in it. The host writes the thinking it has and *then*
    // asks the worker for the transcript; the reply arrived a round trip later and cleared the
    // log. The view holds the reasoning now and redraws it above the messages, whichever of the
    // two arrives first.
    void theReasoningSurvivesTheTranscriptReply() {
        TurnTranscriptView view(QStringLiteral("t1"));
        view.setThinking(QStringLiteral("First I weighed the ponies.\nThen the weather."));
        QVERIFY(log(view).contains(QStringLiteral("First I weighed the ponies.")));
        view.setTranscript(json("{'turn_id':'t1','items':[{'role':'user','content':'ponies?'},"
                                "{'role':'assistant','content':'Done.'}]}"));
        QVERIFY2(log(view).contains(QStringLiteral("First I weighed the ponies.")),
                 qPrintable(log(view)));
        QVERIFY(log(view).contains(QStringLiteral("Then the weather.")));
        QVERIFY(log(view).contains(QStringLiteral("ponies?")));
        QVERIFY(log(view).contains(QStringLiteral("Done.")));
        // The reasoning comes first: it is what the link was clicked for.
        QVERIFY(log(view).indexOf(QStringLiteral("First I weighed")) < log(view).indexOf(QStringLiteral("ponies?")));
        // The other order works too — a pane opened before any reasoning had arrived.
        TurnTranscriptView second(QStringLiteral("t2"));
        second.setTranscript(json("{'turn_id':'t2','items':[{'role':'user','content':'ponies?'}]}"));
        second.setThinking(QStringLiteral("Weighing it up."));
        QVERIFY(log(second).contains(QStringLiteral("Weighing it up.")));
        QVERIFY(log(second).contains(QStringLiteral("ponies?")));
        // A block still streaming calls this again with the longer text: it replaces, never doubles.
        second.setThinking(QStringLiteral("Weighing it up. And again."));
        QCOMPARE(log(second).count(QStringLiteral("Weighing it up.")), 1);
        QVERIFY(log(second).contains(QStringLiteral("And again.")));
    }

    void activatingARowAsksTheHostForItsOutput() {
        TurnTranscriptView view(QStringLiteral("t1"));
        QStringList asked;
        view.onOpenOutput = [&](const QString &callId) { asked << callId; };
        view.setSummary(json("{'tools':[{'call_id':'c1','name':'run_command','ok':true,"
                             "'label':{'kind':'run','title':'ran pytest','running':'running pytest'}}]}"));
        view.focusInput();
        auto *tree = view.findChild<QTreeWidget *>(QStringLiteral("turnTools"));
        QVERIFY(tree);
        tree->setCurrentItem(tree->topLevelItem(0));
        QTest::keyClick(tree, Qt::Key_Return);
        QCOMPARE(asked, QStringList{QStringLiteral("c1")});
    }
};

QTEST_MAIN(TurnTranscriptTests)
#include "turntranscript_test.moc"
