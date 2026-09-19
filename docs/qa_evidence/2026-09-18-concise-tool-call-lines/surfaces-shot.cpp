// SPDX-License-Identifier: AGPL-3.0-or-later
// Evidence harness for card #TK9C, package F: the subagent transcript and the turn pane, fed the
// exact events of docs/AGENT-SESSIONS-PROTOCOL.md § 23 and photographed. No worker and no provider
// — the surfaces are what is under test, and they take their events straight from this file.
//
//   g++ -fPIC $(pkg-config --cflags Qt5Widgets) surfaces-shot.cpp \
//       ../../../src/TurnTranscript.cpp -I../../../src -I../../../build \
//       ../../../build/librelay-subagents.a ../../../build/librelay-toollabel.a \
//       $(pkg-config --libs Qt5Widgets) -o /tmp/surfaces-shot
//   xvfb-run -a /tmp/surfaces-shot <out-dir>
#include "SubagentTranscript.h"
#include "TurnTranscript.h"

#include <QApplication>
#include <QTreeWidget>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>

static QJsonObject json(const char *text) {
    return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object();
}

static void shoot(QWidget &widget, const QString &path) {
    widget.grab().save(path);
    qInfo("wrote %s", qPrintable(path));
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(out);

    // ---- the subagent transcript -------------------------------------------------------------
    relay::SubagentTranscriptView view(QStringLiteral("a1"));
    view.resize(760, 420);
    view.show();
    view.handleEvent(json("{'event':'subagent_transcript','id':'a1','status':'running','messages':["
        "{'role':'user','content':'Find why the router sends bare words to the shell, and fix it'}]}"));
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'delta',"
        "'text':'Reading the router and its tests first.\\n'}}"));
    auto read = [&](const char *id, const char *name, int lines) {
        view.handleEvent(json(QStringLiteral(
            "{'event':'subagent_event','payload':{'event':'tool_started','tool':'read_file','call_id':'%1',"
            "'preview':'READ FILE\\n\\n%2','label':{'kind':'read','running':'reading %2','title':'read %2',"
            "'merge':{'key':'read','singular':'file','plural':'files'}}}}")
            .arg(QLatin1String(id), QLatin1String(name)).toUtf8().constData()));
        view.handleEvent(json(QStringLiteral(
            "{'event':'subagent_event','payload':{'event':'tool_result','tool':'read_file','call_id':'%1',"
            "'label':{'kind':'read','running':'reading %2','title':'read %2','stats':['%3 lines'],'ok':true,"
            "'open':{'type':'file','path':'%2'},"
            "'merge':{'key':'read','singular':'file','plural':'files','lines':%3}}}}")
            .arg(QLatin1String(id), QLatin1String(name)).arg(lines).toUtf8().constData()));
    };
    read("c1", "router.py", 244);
    read("c2", "test_router.py", 168);
    read("c3", "classify.py", 91);
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_started','tool':'run_command',"
        "'call_id':'c4','preview':'RUN COMMAND\\n\\npytest -q tests/test_router.py\\n\\nWorking directory: .',"
        "'label':{'kind':'run','running':'running pytest','title':'ran pytest'}}}"));
    shoot(view, out + QStringLiteral("/subagent-01-a-call-while-it-runs.png"));
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_output',"
        "'text':'tests/test_router.py::test_a_bare_word FAILED\\n1 failed, 12 passed\\n'}}"));
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_result','tool':'run_command',"
        "'call_id':'c4','ms':2100,'result':{'exit_code':1},"
        "'label':{'kind':'run','running':'running pytest','title':'ran pytest',"
        "'stats':['12 lines','exit 1','2.1 s'],'ok':false,'open':{'type':'fold'}}}}"));
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_started','tool':'edit_file',"
        "'call_id':'c5','preview':'EDIT FILE\\n\\nbackend/relay_core/router.py',"
        "'label':{'kind':'edit','running':'editing router.py','title':'edited router.py','path':'backend/relay_core/router.py'}}}"));
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'tool_result','tool':'edit_file',"
        "'call_id':'c5','ms':60,"
        "'diff':'--- a/backend/relay_core/router.py\\n+++ b/backend/relay_core/router.py\\n@@ -41,3 +41,4 @@\\n"
        " def classify(text):\\n-    if \\'/\\' in text:\\n+    if text.startswith(\\'/\\') or \\'/\\' in text.split()[0]:\\n"
        "+        # a bare word with a slash is a path, not a question\\n',"
        "'label':{'kind':'edit','running':'editing router.py','title':'edited router.py',"
        "'stats':['+2 −1'],'ok':true,'path':'backend/relay_core/router.py','inline_diff':true,"
        "'open':{'type':'fold'}}}}"));
    view.handleEvent(json("{'event':'subagent_event','payload':{'event':'delta',"
        "'text':'\\nThe heuristic only looked for a slash anywhere in the line.\\n'}}"));
    shoot(view, out + QStringLiteral("/subagent-02-merged-reads-a-failure-and-an-inline-diff.png"));
    view.toggleToolCall(1);          // the failed pytest row
    shoot(view, out + QStringLiteral("/subagent-03-the-detail-folded-open-in-place.png"));

    // ---- the turn pane -----------------------------------------------------------------------
    relay::TurnTranscriptView turn(QStringLiteral("t1"));
    turn.resize(760, 420);
    turn.show();
    turn.setSummary(json("{'turn_id':'t1','elapsed_ms':9400,'thinking_ms':900,'tools':["
        "{'call_id':'c1','name':'read_file','ok':true,'ms':40,'label':{'kind':'read','title':'read router.py',"
        " 'running':'reading router.py','stats':['244 lines'],"
        " 'merge':{'key':'read','singular':'file','plural':'files','lines':244}}},"
        "{'call_id':'c2','name':'read_file','ok':true,'ms':35,'label':{'kind':'read','title':'read test_router.py',"
        " 'running':'reading test_router.py','stats':['168 lines'],"
        " 'merge':{'key':'read','singular':'file','plural':'files','lines':168}}},"
        "{'call_id':'c3','name':'read_file','ok':true,'ms':28,'label':{'kind':'read','title':'read classify.py',"
        " 'running':'reading classify.py','stats':['91 lines'],"
        " 'merge':{'key':'read','singular':'file','plural':'files','lines':91}}},"
        "{'call_id':'c4','name':'run_command','ok':false,'ms':2100,'label':{'kind':'run','title':'ran pytest',"
        " 'running':'running pytest','stats':['12 lines','exit 1','2.1 s']}},"
        "{'call_id':'c5','name':'edit_file','ok':true,'ms':60,'label':{'kind':'edit','title':'edited router.py',"
        " 'running':'editing router.py','stats':['+2 −1'],'path':'backend/relay_core/router.py'}},"
        "{'call_id':'c6','name':'edit_file','ok':false,'ms':12,'label':{'kind':'edit','title':'edit gone.py',"
        " 'running':'editing gone.py','error':'old_string was not found in the file','ok':false}}]}"));
    turn.setTranscript(json("{'items':[{'role':'user','content':'why does the router send this to the shell?'},"
        "{'role':'assistant','content':'The heuristic only looked for a slash anywhere in the line.'}]}"));
    turn.findChild<QTreeWidget *>(QStringLiteral("turnTools"))->expandAll();
    shoot(turn, out + QStringLiteral("/turn-01-rows-are-the-labels-lines.png"));
    turn.setToolOutput(json("{'call_id':'c4','label':{'kind':'run','title':'ran pytest','running':'running pytest',"
        " 'stats':['12 lines','exit 1','2.1 s'],'ok':false,'open':{'type':'fold'}},"
        " 'detail':[{'heading':'command','style':'code','text':'pytest -q tests/test_router.py'},"
        "           {'heading':'output','style':'output','text':'tests/test_router.py::test_a_bare_word FAILED\\n"
        "1 failed, 12 passed in 2.06s','truncated':true}]}"));
    shoot(turn, out + QStringLiteral("/turn-02-the-detail-sections-in-the-log.png"));
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
}
