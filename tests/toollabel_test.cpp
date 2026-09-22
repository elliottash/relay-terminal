// SPDX-License-Identifier: AGPL-3.0-or-later
// The label parser every surface shares (src/ToolLabel.h, docs/AGENT-SESSIONS-PROTOCOL.md § 23).
#include "ToolLabel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using relay::toollabel::Label;
using relay::toollabel::MergeRun;

namespace {
// Test JSON uses single quotes to keep the C++ strings readable (as tests/subagents_test.cpp does).
QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }

// A run that exited 1: the § 23.2 example.
QJsonObject failedRun() {
    return json("{'event': 'tool_result', 'tool': 'run_command', 'ms': 8100, 'label': {"
                "'kind': 'run', 'running': 'running pytest', 'title': 'ran pytest',"
                "'stats': ['212 lines', 'exit 1', '8 s'], 'ok': false, 'open': {'type': 'fold'}}}");
}

QJsonObject read(int lines) {
    QJsonObject label{{"kind", "read"}, {"running", "reading agent.py"}, {"title", "read agent.py"},
                      {"stats", QJsonArray{QString::number(lines) + QStringLiteral(" lines")}},
                      {"ok", true}, {"path", "backend/relay_core/agent.py"},
                      {"open", QJsonObject{{"type", "file"}, {"path", "backend/relay_core/agent.py"}}},
                      {"merge", QJsonObject{{"key", "read"}, {"singular", "file"}, {"plural", "files"},
                                            {"lines", lines}}}};
    return QJsonObject{{"event", "tool_result"}, {"tool", "read_file"}, {"label", label}};
}
}  // namespace

class ToolLabelTests : public QObject {
    Q_OBJECT
private slots:
    void refusalGradeKeepsFailureAndRejectsStringCodes() {
        const Label refused = relay::toollabel::parse(json(
            "{'kind':'edit','title':'edit x.py','ok':false,'refused':true,'error':'guard refused'}"));
        QVERIFY(refused.hasRefused);
        QVERIFY(refused.refused);
        QVERIFY(refused.failed());
        QCOMPARE(refused.line(), QStringLiteral("edit x.py · guard refused"));
        QVERIFY(!relay::toollabel::parse(json("{'title':'runtime','ok':false,'refused':'busy'}")).refused);
        QVERIFY(!relay::toollabel::fromEvent(failedRun()).refused);
    }

    // ---- the line ----------------------------------------------------------------------------

    void lineIsTitleThenStats() {
        const Label label = relay::toollabel::fromEvent(failedRun());
        QVERIFY(label.valid);
        QVERIFY(!label.fallback);
        QCOMPARE(label.kind, QStringLiteral("run"));
        QCOMPARE(label.line(), QStringLiteral("ran pytest · 212 lines · exit 1 · 8 s"));
        QCOMPARE(label.runningLine(), QStringLiteral("running pytest"));
        QVERIFY(label.failed());          // ok: false — the surface draws ✗
        QCOMPARE(label.openType, QStringLiteral("fold"));
    }

    void aCallThatNeverRanSaysWhyOnTheSameLine() {
        const Label label = relay::toollabel::parse(json(
            "{'kind': 'edit', 'running': 'editing x.py', 'title': 'edit x.py', 'ok': false,"
            " 'error': 'old_string was not found in the file', 'open': {'type': 'fold'}}"));
        QCOMPARE(label.line(), QStringLiteral("edit x.py · old_string was not found in the file"));
        QVERIFY(label.failed());
    }

    void aRunThatFailedDoesNotRepeatItsExitCode() {
        // Ran and exited 1: `stats` already says so, and there is no `error` (§ 23.2).
        const Label label = relay::toollabel::fromEvent(failedRun());
        QVERIFY(label.error.isEmpty());
        QVERIFY(!label.line().contains(QStringLiteral("exit 1 · exit 1")));
    }

    void anInlineDiffAndAPathAreReadBack() {
        const Label label = relay::toollabel::parse(json(
            "{'kind': 'edit', 'title': 'edited x.py', 'running': 'editing x.py', 'stats': ['+3 −1'],"
            " 'ok': true, 'path': 'src/x.py', 'inline_diff': true, 'open': {'type': 'fold'}}"));
        QCOMPARE(label.line(), QStringLiteral("edited x.py · +3 −1"));
        QVERIFY(label.hasInlineDiff);
        QVERIFY(label.inlineDiff);
        QCOMPARE(label.path, QStringLiteral("src/x.py"));
        QVERIFY(!label.failed());
    }

    void aBigDiffOpensTheDiffPane() {
        const Label label = relay::toollabel::parse(json(
            "{'kind': 'edit', 'title': 'edited Pane.h', 'running': 'editing Pane.h',"
            " 'stats': ['+212 −87'], 'ok': true, 'path': 'src/Pane.h', 'inline_diff': false,"
            " 'open': {'type': 'diff'}}"));
        QVERIFY(label.hasInlineDiff);
        QVERIFY(!label.inlineDiff);
        QCOMPARE(label.openType, QStringLiteral("diff"));
    }

    void openCarriesItsSubject() {
        const Label subagent = relay::toollabel::parse(json(
            "{'kind': 'agent', 'title': 'started subagent', 'open': {'type': 'subagent', 'id': 'a1'}}"));
        QCOMPARE(subagent.openType, QStringLiteral("subagent"));
        QCOMPARE(subagent.openId, QStringLiteral("a1"));
        const Label card = relay::toollabel::parse(json(
            "{'kind': 'board', 'title': 'moved card #K7Q2 → done', 'open': {'type': 'card', 'id': 'K7Q2'}}"));
        QCOMPARE(card.openId, QStringLiteral("K7Q2"));
        const Label file = relay::toollabel::parse(json(
            "{'kind': 'read', 'title': 'read x.py', 'open': {'type': 'file', 'path': 'src/x.py'}}"));
        QCOMPARE(file.openPath, QStringLiteral("src/x.py"));
    }

    void aTurnSummaryItemTakesItsVerdictFromBesideTheLabel() {
        // turn_summary.tools[] keeps `ok` of its own; the label on an item has none.
        const QJsonObject item = json(
            "{'call_id': 'c1', 'name': 'run_command', 'ok': false, 'preview': 'RUN COMMAND\\n\\npytest',"
            " 'label': {'kind': 'run', 'running': 'running pytest', 'title': 'ran pytest',"
            " 'stats': ['212 lines']}}");
        const Label label = relay::toollabel::fromEvent(item);
        QVERIFY(label.hasOk);
        QVERIFY(label.failed());
        QCOMPARE(label.line(), QStringLiteral("ran pytest · 212 lines"));
    }

    // ---- the legacy fallback -----------------------------------------------------------------

    void aWorkerWithoutLabelsStillGetsOneLine() {
        const Label run = relay::toollabel::fromEvent(json(
            "{'event': 'tool_started', 'tool': 'run_command',"
            " 'preview': 'RUN COMMAND\\n\\ngit status\\n\\nWorking directory: .'}"));
        QVERIFY(run.valid);
        QVERIFY(run.fallback);
        QCOMPARE(run.kind, QStringLiteral("run"));
        QCOMPARE(run.line(), QStringLiteral("ran git status"));
        QCOMPARE(run.runningLine(), QStringLiteral("running git status"));

        const Label write = relay::toollabel::fromEvent(json(
            "{'event': 'tool_started', 'tool': 'write_file',"
            " 'preview': 'WRITE FILE\\n\\n/home/e/p/x.py\\n\\nOld bytes: 0.'}"));
        QCOMPARE(write.kind, QStringLiteral("edit"));
        QCOMPARE(write.line(), QStringLiteral("wrote /home/e/p/x.py"));
        QCOMPARE(write.path, QStringLiteral("/home/e/p/x.py"));

        const Label list = relay::toollabel::fromEvent(json(
            "{'event': 'tool_started', 'tool': 'list_directory', 'preview': 'LIST DIRECTORY\\n\\nsrc/'}"));
        QCOMPARE(list.line(), QStringLiteral("listed src/"));
    }

    void aLongPathInAFallbackShortensToItsBaseName() {
        const Label read = relay::toollabel::fromEvent(json(
            "{'event': 'tool_started', 'tool': 'read_file',"
            " 'preview': 'READ FILE\\n\\n/home/elliott/repos/relay-terminal/src/components/Widget.tsx'}"));
        QCOMPARE(read.line(), QStringLiteral("read Widget.tsx"));
        // `path` keeps the whole thing, so the surface can still open the file (§ 23.2).
        QCOMPARE(read.path, QStringLiteral("/home/elliott/repos/relay-terminal/src/components/Widget.tsx"));
    }

    void anUnknownToolFallsBackToItsHumanisedName() {
        const Label label = relay::toollabel::fromEvent(json(
            "{'event': 'tool_started', 'tool': 'update_todos', 'preview': '[ ] write the test'}"));
        // #SHE3: the humanised fallback says "tasks" for update_todos, like the labelled row.
        QCOMPARE(label.line(), QStringLiteral("update tasks [ ] write the test"));
        QCOMPARE(label.runningLine(), QStringLiteral("running update tasks"));
    }

    void aFallbackTakesTheExitCodeAndTheError() {
        const Label exited = relay::toollabel::fromEvent(json(
            "{'call_id': 'c1', 'name': 'run_command', 'preview': 'RUN COMMAND\\n\\npytest', 'exit_code': 1}"));
        QCOMPARE(exited.line(), QStringLiteral("ran pytest · exit 1"));
        QVERIFY(exited.failed());
        const Label errored = relay::toollabel::fromEvent(json(
            "{'event': 'tool_result', 'tool': 'edit_file', 'result': {'error': 'no such file'}}"));
        QVERIFY(errored.failed());
    }

    void anEmptyEventIsNotALabel() {
        QVERIFY(!relay::toollabel::fromEvent(QJsonObject{}).valid);
        QVERIFY(!relay::toollabel::parse(QJsonObject{}).valid);
        QCOMPARE(Label{}.line(), QString());
    }

    // ---- merged runs (§ 23.7) ----------------------------------------------------------------

    void consecutiveReadsBecomeOneLine() {
        MergeRun run;
        QVERIFY(!run.active());
        qint64 total = 0;
        for (const int lines : {412, 1200, 900, 700, 488, 400}) {
            const Label label = relay::toollabel::fromEvent(read(lines));
            QCOMPARE(run.accepts(label), run.active());
            run.add(label);
            total += lines;
        }
        QCOMPARE(run.count(), 6);
        QCOMPARE(run.total(), total);              // 4,100
        QCOMPARE(run.line(), QStringLiteral("read 6 files · 4,100 lines"));
    }

    void aListingRunCountsEntries() {
        MergeRun run;
        for (const int entries : {40, 40, 12}) {
            const QJsonObject label{{"kind", "list"}, {"title", "listed src/"}, {"running", "listing src/"},
                                    {"ok", true},
                                    {"merge", QJsonObject{{"key", "list"}, {"singular", "folder"},
                                                          {"plural", "folders"}, {"entries", entries}}}};
            run.add(relay::toollabel::parse(label));
        }
        QCOMPARE(run.line(), QStringLiteral("listed 3 folders · 92 entries"));
    }

    void aSingleMemberReadsInTheSingular() {
        MergeRun run;
        run.add(relay::toollabel::fromEvent(read(1)));
        QCOMPARE(run.count(), 1);
        QCOMPARE(run.line(), QStringLiteral("read 1 file · 1 line"));
    }

    void aCallOfAnotherKindEndsTheRun() {
        MergeRun run;
        run.add(relay::toollabel::fromEvent(read(10)));
        run.add(relay::toollabel::fromEvent(read(10)));
        QCOMPARE(run.count(), 2);
        const Label command = relay::toollabel::fromEvent(failedRun());
        QVERIFY(!run.accepts(command));
        run.add(command);                         // a label with no merge clears the run
        QVERIFY(!run.active());
        QCOMPARE(run.line(), QString());
    }

    void aFailedCallNeverMerges() {
        // "A failed call carries no merge, so it stands on its own line … and it also breaks the
        // run either side of it" (§ 23.7).
        MergeRun run;
        run.add(relay::toollabel::fromEvent(read(10)));
        QJsonObject broken = read(0);
        QJsonObject label = broken.value(QStringLiteral("label")).toObject();
        label[QStringLiteral("ok")] = false;
        label[QStringLiteral("error")] = QStringLiteral("no such file");
        broken[QStringLiteral("label")] = label;
        const Label failed = relay::toollabel::fromEvent(broken);
        QVERIFY(!run.accepts(failed));
        run.add(failed);
        QVERIFY(!run.active());
    }

    void aDifferentKeyStartsAFreshRun() {
        MergeRun run;
        run.add(relay::toollabel::fromEvent(read(10)));
        const QJsonObject listing{{"kind", "list"}, {"title", "listed src/"}, {"ok", true},
                                  {"merge", QJsonObject{{"key", "list"}, {"singular", "folder"},
                                                        {"plural", "folders"}, {"entries", 7}}}};
        const Label label = relay::toollabel::parse(listing);
        QVERIFY(!run.accepts(label));
        run.add(label);
        QCOMPARE(run.count(), 1);
        QCOMPARE(run.key(), QStringLiteral("list"));
    }

    // ---- small helpers -----------------------------------------------------------------------

    void thousandsAreGroupedWhateverTheLocale() {
        QCOMPARE(relay::toollabel::thousands(0), QStringLiteral("0"));
        QCOMPARE(relay::toollabel::thousands(999), QStringLiteral("999"));
        QCOMPARE(relay::toollabel::thousands(1000), QStringLiteral("1,000"));
        QCOMPARE(relay::toollabel::thousands(4100), QStringLiteral("4,100"));
        QCOMPARE(relay::toollabel::thousands(1234567), QStringLiteral("1,234,567"));
    }

    void shortPathKeepsFortyCharacters() {
        const QString fits = QStringLiteral("src/components/Widget.tsx");
        QCOMPARE(relay::toollabel::shortPath(fits), fits);
        QCOMPARE(relay::toollabel::shortPath(QStringLiteral("a/very/long/path/that/goes/on/and/on/and/on/Widget.tsx")),
                 QStringLiteral("Widget.tsx"));
        QCOMPARE(relay::toollabel::shortPath(QString()), QString());
    }
};

QTEST_MAIN(ToolLabelTests)
#include "toollabel_test.moc"
