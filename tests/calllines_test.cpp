// SPDX-License-Identifier: GPL-3.0-or-later
// The terminal pane's tool-call lines (src/CallLines.h, card #TK9C): the anchor URI, the row and
// how far it may be cut, the rewrite-or-new-row state machine, and a fold's rows. Everything the
// pane decides before it writes a byte, tested without a terminal.
#include "CallLines.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace relay::calllines;
using relay::FoldLine;
using relay::toollabel::Label;

namespace {

QJsonObject json(const char *text) { return QJsonDocument::fromJson(QByteArray(text).replace('\'', '"')).object(); }

Label label(const char *text) { return relay::toollabel::parse(json(text)); }

// "ran pytest ✗ · 212 lines · exit 1 · 8 s"
Label pytest() {
    return label("{'kind': 'run', 'running': 'running pytest', 'title': 'ran pytest',"
                 "'stats': ['212 lines', 'exit 1', '8 s'], 'ok': false, 'open': {'type': 'fold'}}");
}

Label readFile(const char *name, int lines) {
    QJsonObject object{{"kind", "read"}, {"running", QStringLiteral("reading ") + QLatin1String(name)},
                       {"title", QStringLiteral("read ") + QLatin1String(name)},
                       {"stats", QJsonArray{QString::number(lines) + QStringLiteral(" lines")}},
                       {"ok", true}, {"path", QStringLiteral("src/") + QLatin1String(name)},
                       {"open", QJsonObject{{"type", "file"}, {"path", QStringLiteral("src/") + QLatin1String(name)}}},
                       {"merge", QJsonObject{{"key", "read"}, {"singular", "file"}, {"plural", "files"},
                                             {"lines", lines}}}};
    return relay::toollabel::parse(object);
}

Palette palette() {
    Palette p;
    p.text = QColor(0xe6, 0xe8, 0xec);
    p.muted = QColor(0x8b, 0x91, 0x9c);
    p.code = QColor(0x3e, 0xc5, 0xf0);
    p.add = QColor(0x7e, 0xc8, 0x8c);
    p.remove = QColor(0xe0, 0x6c, 0x75);
    p.addBg = QColor(0x1a, 0x28, 0x1d);
    p.removeBg = QColor(0x2a, 0x1a, 0x1c);
    p.error = QColor(0xf0, 0x71, 0x78);
    return p;
}

QString textOf(const FoldLine &line) {
    QString out;
    for (const relay::FoldSpan &span : line.spans) out += span.text;
    return out;
}

QStringList textsOf(const QVector<FoldLine> &lines) {
    QStringList out;
    for (const FoldLine &line : lines) out << textOf(line);
    return out;
}

}  // namespace

class CallLinesTests : public QObject {
    Q_OBJECT
private slots:
    // ---- the anchor URI ------------------------------------------------------------------------

    void uriRoundTrips() {
        const QString uri = foldUri(QStringLiteral("pane-1"), QStringLiteral("turn/7"), QStringLiteral("call 3"));
        QVERIFY(uri.startsWith(QStringLiteral("relay://call/")));
        QVERIFY(!uri.contains(QStringLiteral("turn/7")));   // the ids are percent-encoded
        const Ref ref = parseUri(uri);
        QVERIFY(ref.valid);
        QVERIFY(ref.fold);
        QCOMPARE(ref.pane, QStringLiteral("pane-1"));
        QCOMPARE(ref.turn, QStringLiteral("turn/7"));
        QCOMPARE(ref.call, QStringLiteral("call 3"));
        QCOMPARE(ref.extra, 0);
        QVERIFY(!ref.merged());
    }

    void mergedUriCarriesItsCount() {
        const QString uri = foldUri(QStringLiteral("p"), QStringLiteral("t"), QStringLiteral("c1"), 6);
        const Ref ref = parseUri(uri);
        QVERIFY(ref.valid);
        QCOMPARE(ref.call, QStringLiteral("c1"));
        QCOMPARE(ref.extra, 6);
        QVERIFY(ref.merged());
    }

    // A call id with a `+` of its own is encoded, so it can never be read as a run's count.
    void plusInACallIdIsNotACount() {
        const Ref ref = parseUri(foldUri(QStringLiteral("p"), QStringLiteral("t"), QStringLiteral("c+2")));
        QVERIFY(ref.valid);
        QCOMPARE(ref.call, QStringLiteral("c+2"));
        QCOMPARE(ref.extra, 0);
    }

    void openUriIsItsOwnScheme() {
        const QString uri = openUri(QStringLiteral("p"), QStringLiteral("t"), QStringLiteral("c1"));
        QVERIFY(uri.startsWith(QStringLiteral("relay://open-call/")));
        const Ref ref = parseUri(uri);
        QVERIFY(ref.valid);
        QVERIFY(!ref.fold);
        QCOMPARE(ref.call, QStringLiteral("c1"));
    }

    void otherUrisAreRefused() {
        QVERIFY(!parseUri(QStringLiteral("relay://turn/p/t")).valid);
        QVERIFY(!parseUri(QStringLiteral("relay://call/p/t")).valid);         // too few parts
        QVERIFY(!parseUri(QStringLiteral("relay://call/p/t/c/extra")).valid); // too many
        QVERIFY(!parseUri(QString()).valid);
    }

    // ---- the row -------------------------------------------------------------------------------

    void finishedRowSplitsTitleFromStats() {
        const Row row = finishedRow(pytest(), 0);
        QVERIFY(row.failed);
        QCOMPARE(row.title, QStringLiteral("ran pytest ✗"));
        QCOMPARE(row.rest, QStringLiteral(" · 212 lines · exit 1 · 8 s"));
    }

    void aCallThatNeverRanSaysWhy() {
        const Row row = finishedRow(label("{'kind': 'edit', 'title': 'edit x.py',"
                                          "'error': 'old_string was not found in the file'}"), 0);
        QVERIFY(row.failed);
        QCOMPARE(row.title, QStringLiteral("edit x.py ✗"));
        QCOMPARE(row.rest, QStringLiteral(" · old_string was not found in the file"));
    }

    void aNarrowPaneLosesTheStatsFirst() {
        const Row row = finishedRow(pytest(), 20);
        QCOMPARE(row.title, QStringLiteral("ran pytest ✗"));
        QCOMPARE(row.title.size() + row.rest.size(), 20);
        QVERIFY(row.rest.endsWith(QStringLiteral("…")));
    }

    void aPaneTooNarrowForTheTitleCutsTheTitle() {
        const Row row = finishedRow(pytest(), 6);
        QCOMPARE(row.text(), QStringLiteral("ran p…"));
        QVERIFY(row.rest.isEmpty());
    }

    void widthZeroNeverCuts() {
        QCOMPARE(finishedRow(pytest(), 0).text(), QStringLiteral("ran pytest ✗ · 212 lines · exit 1 · 8 s"));
    }

    void runningRowCountsLiveOutput() {
        const Row row = runningRow(pytest(), 0, 4100);
        QCOMPARE(row.title, QStringLiteral("running pytest…"));
        QCOMPARE(row.rest, QStringLiteral(" · 4,100 lines"));
    }

    // ---- what a click does -----------------------------------------------------------------------

    void clickFollowsOpenType() {
        QCOMPARE(clickFor(label("{'open': {'type': 'fold'}}")), Click::Fold);
        QCOMPARE(clickFor(label("{'open': {'type': 'file', 'path': 'x.py'}}")), Click::File);
        QCOMPARE(clickFor(label("{'open': {'type': 'diff'}}")), Click::Diff);
        QCOMPARE(clickFor(label("{'open': {'type': 'subagent', 'id': 'a1'}}")), Click::Subagent);
        QCOMPARE(clickFor(label("{'open': {'type': 'card', 'id': 'K7Q2'}}")), Click::Card);
        QCOMPARE(clickFor(label("{'open': {'type': 'plan'}}")), Click::Plan);
        QCOMPARE(clickFor(label("{'open': {'type': 'todos'}}")), Click::Todos);
        QCOMPARE(clickFor(label("{'open': {'type': 'something new'}}")), Click::Fold);
        QCOMPARE(clickFor(Label()), Click::Fold);
    }

    void aFailedCallAlwaysFolds() {
        QCOMPARE(clickFor(label("{'ok': false, 'open': {'type': 'diff'}}")), Click::Fold);
        QCOMPARE(clickFor(label("{'error': 'no such file', 'open': {'type': 'file', 'path': 'x'}}")), Click::Fold);
    }

    // ---- the state machine -----------------------------------------------------------------------

    void aResultRewritesItsOwnRunningRow() {
        LineCursor cursor;
        cursor.setCells(0);
        const Step started = cursor.start(QStringLiteral("c1"), pytest());
        QVERIFY(started.newRow);
        QVERIFY(started.hold);
        QCOMPARE(started.row.title, QStringLiteral("running pytest…"));
        QVERIFY(cursor.holding());

        const Step done = cursor.result(QStringLiteral("c1"), pytest(), 0);
        QVERIFY(done.rewrite);
        QVERIFY(!done.newRow);
        QVERIFY(!done.hold);            // not mergeable: the row gets its newline
        QVERIFY(!done.endRun);
        QCOMPARE(done.call, QStringLiteral("c1"));
        QCOMPARE(done.row.title, QStringLiteral("ran pytest ✗"));
        QVERIFY(!cursor.holding());
    }

    void anythingPrintedBetweenForcesANewRow() {
        LineCursor cursor;
        cursor.start(QStringLiteral("c1"), pytest());
        const Step interrupted = cursor.other();
        QVERIFY(interrupted.endRun);    // the running row is finished where it stands
        QVERIFY(!cursor.holding());

        const Step done = cursor.result(QStringLiteral("c1"), pytest(), 0);
        QVERIFY(done.newRow);
        QVERIFY(!done.rewrite);
        QVERIFY(!done.endRun);
    }

    void aResultWithNoStartedRowStartsOne() {
        LineCursor cursor;
        const Step done = cursor.result(QStringLiteral("c1"), pytest(), 0);
        QVERIFY(done.newRow);
        QVERIFY(!done.rewrite);
    }

    void consecutiveReadsBecomeOneRow() {
        LineCursor cursor;
        cursor.start(QStringLiteral("c1"), readFile("a.py", 100));
        const Step first = cursor.result(QStringLiteral("c1"), readFile("a.py", 100), 0);
        QVERIFY(first.rewrite);
        QVERIFY(first.hold);            // the run may still grow, so no newline yet
        QVERIFY(!first.merged);
        QCOMPARE(first.row.title, QStringLiteral("read a.py"));
        QCOMPARE(first.call, QStringLiteral("c1"));

        // The second read says nothing of its own: the run's row is already there.
        const Step started = cursor.start(QStringLiteral("c2"), readFile("b.py", 200));
        QVERIFY(started.nothing);
        QVERIFY(!started.endRun);

        const Step second = cursor.result(QStringLiteral("c2"), readFile("b.py", 200), 0);
        QVERIFY(second.rewrite);
        QVERIFY(second.hold);
        QVERIFY(second.merged);
        QCOMPARE(second.call, QStringLiteral("c1+2"));
        QCOMPARE(second.row.title, QStringLiteral("read 2 files"));
        QCOMPARE(second.row.rest, QStringLiteral(" · 300 lines"));
        QCOMPARE(cursor.openCall(), QStringLiteral("c1+2"));
        QCOMPARE(cursor.members().size(), 2);
        QCOMPARE(cursor.members().at(1).path, QStringLiteral("src/b.py"));
    }

    void aCommandEndsTheRunOfReads() {
        LineCursor cursor;
        cursor.result(QStringLiteral("c1"), readFile("a.py", 100), 0);
        cursor.result(QStringLiteral("c2"), readFile("b.py", 200), 0);
        QVERIFY(cursor.holding());
        const Step command = cursor.start(QStringLiteral("c3"), pytest());
        QVERIFY(command.endRun);        // the merged row gets its newline here
        QVERIFY(command.newRow);
        QVERIFY(cursor.members().isEmpty());
    }

    void proseEndsTheRunToo() {
        LineCursor cursor;
        cursor.result(QStringLiteral("c1"), readFile("a.py", 100), 0);
        cursor.result(QStringLiteral("c2"), readFile("b.py", 200), 0);
        const Step prose = cursor.other();
        QVERIFY(prose.endRun);
        QVERIFY(!cursor.holding());
        QVERIFY(cursor.openCall().isEmpty());
    }

    void aFailedReadBreaksTheRun() {
        LineCursor cursor;
        cursor.result(QStringLiteral("c1"), readFile("a.py", 100), 0);
        const Step failed = cursor.result(QStringLiteral("c2"),
                                          label("{'kind': 'read', 'title': 'read b.py',"
                                                "'error': 'No such file'}"), 0);
        QVERIFY(failed.endRun);         // the run's row is closed
        QVERIFY(failed.newRow);
        QVERIFY(!failed.hold);
        QVERIFY(failed.row.failed);
    }

    void liveTicksRewriteOnlyTheirOwnRow() {
        LineCursor cursor;
        cursor.start(QStringLiteral("c1"), pytest());
        const Step tick = cursor.live(QStringLiteral("c1"), pytest(), 120, 0);
        QVERIFY(tick.rewrite);
        QVERIFY(tick.hold);
        QCOMPARE(tick.row.rest, QStringLiteral(" · 120 lines"));
        QVERIFY(cursor.live(QStringLiteral("c2"), pytest(), 3, 0).nothing);
        cursor.other();
        QVERIFY(cursor.live(QStringLiteral("c1"), pytest(), 9, 0).nothing);
    }

    // ---- the fold ------------------------------------------------------------------------------

    void oneSectionHasNoHeading() {
        const QJsonObject reply = json("{'detail': [{'heading': 'contents', 'style': 'output',"
                                       "'text': 'alpha\\nbeta'}]}");
        const QStringList rows = textsOf(foldForReply(reply, palette(), {}));
        QCOMPARE(rows, (QStringList{QStringLiteral("alpha"), QStringLiteral("beta")}));
    }

    void twoSectionsGetTheirHeadings() {
        const QJsonObject reply = json("{'detail': [{'heading': 'command', 'style': 'code', 'text': 'ls -la'},"
                                       "{'heading': 'output', 'style': 'output', 'text': 'x', 'truncated': true}]}");
        const QStringList rows = textsOf(foldForReply(reply, palette(), {}));
        QCOMPARE(rows, (QStringList{QStringLiteral("command"), QStringLiteral("$ ls -la"),
                                    QStringLiteral("output"), QStringLiteral("x"),
                                    QStringLiteral("(truncated)")}));
    }

    void aDiffKeepsItsColoursAndDropsTheFileRows() {
        const QJsonObject reply = json("{'detail': [{'heading': 'diff', 'style': 'diff',"
                                       "'text': '--- a/x.py\\n+++ b/x.py\\n@@ -1,2 +1,2 @@\\n keep\\n-old\\n+new\\n'}]}");
        const QVector<FoldLine> rows = foldForReply(reply, palette(), {});
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("@@ -1,2 +1,2 @@"), QStringLiteral(" keep"),
                                             QStringLiteral("-old"), QStringLiteral("+new")}));
        QCOMPARE(rows.at(2).spans.first().fg, palette().remove);
        QCOMPARE(rows.at(2).spans.first().bg, palette().removeBg);
        QCOMPARE(rows.at(3).spans.first().fg, palette().add);
        QCOMPARE(rows.at(3).spans.first().bg, palette().addBg);
    }

    void aDiffAlreadyInAPaneIsNotRepeated() {
        const QJsonObject reply = json("{'detail': [{'heading': 'diff', 'style': 'diff',"
                                       "'text': '@@ -1 +1 @@\\n-old\\n+new\\n'}]}");
        FoldOptions options;
        options.diffToPane = true;
        QCOMPARE(textsOf(foldForReply(reply, palette(), options)),
                 (QStringList{QStringLiteral("(opened in a diff pane)")}));
    }

    void escapesNeverReachTheGrid() {
        QCOMPARE(stripAnsi(QStringLiteral("\x1b[31mred\x1b[0m done")), QStringLiteral("red done"));
        QCOMPARE(stripAnsi(QStringLiteral("a\x1b]0;title\x07"
                                          "b")), QStringLiteral("ab"));
        QCOMPARE(stripAnsi(QStringLiteral("keep\ttabs\nand rows")), QStringLiteral("keep\ttabs\nand rows"));
    }

    void aLongFoldIsCappedAndSaysSo() {
        QString text;
        for (int at = 0; at < 30; ++at) text += QStringLiteral("row %1\n").arg(at);
        const QJsonObject reply{{"detail", QJsonArray{QJsonObject{{"heading", "output"}, {"style", "output"},
                                                                  {"text", text}}}}};
        FoldOptions options;
        options.maxLines = 10;
        options.openInPane = QStringLiteral("relay://open-call/p/t/c1");
        const QStringList rows = textsOf(foldForReply(reply, palette(), options));
        QCOMPARE(rows.size(), 12);   // 10 rows, the "… more" row, the links row
        QVERIFY(rows.at(10).startsWith(QStringLiteral("… 21 more lines")));
        QCOMPARE(rows.at(11), QStringLiteral("open in pane"));
    }

    void anEmptyReplySaysNothingWasRecorded() {
        QCOMPARE(textsOf(foldForReply(QJsonObject(), palette(), {})),
                 (QStringList{QStringLiteral("(No output recorded for this call.)")}));
    }

    void anOlderWorkersTextStillFolds() {
        QCOMPARE(textsOf(foldForReply(json("{'text': 'one\\ntwo'}"), palette(), {})),
                 (QStringList{QStringLiteral("one"), QStringLiteral("two")}));
    }

    void theLinksRowCarriesBothLinks() {
        FoldOptions options;
        options.openInPane = QStringLiteral("relay://open-call/p/t/c1");
        options.openPath = QStringLiteral("/home/me/src/x.py");
        const QVector<FoldLine> rows = foldForReply(json("{'text': 'x'}"), palette(), options);
        const FoldLine &last = rows.last();
        QCOMPARE(last.spans.size(), 3);
        QCOMPARE(last.spans.at(0).link, options.openInPane);
        QCOMPARE(last.spans.at(2).text, QStringLiteral("open x.py"));
        QCOMPARE(last.spans.at(2).link, options.openPath);
    }

    void aMergedRunListsItsMembers() {
        const QVector<RunMember> members{{QStringLiteral("read a.py · 100 lines"), QStringLiteral("/w/src/a.py"), QStringLiteral("c1")},
                                         {QStringLiteral("read b.py · 200 lines"), QString(), QStringLiteral("c2")}};
        const QVector<FoldLine> rows = foldForRun(members, palette(), {});
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("read a.py · 100 lines"),
                                             QStringLiteral("read b.py · 200 lines")}));
        QCOMPARE(rows.at(0).spans.first().link, QStringLiteral("/w/src/a.py"));
        QVERIFY(rows.at(1).spans.first().link.isEmpty());
    }

    void aNoteIsOneMutedRow() {
        const QVector<FoldLine> rows = foldForNote(QStringLiteral("That turn is no longer kept."), palette());
        QCOMPARE(rows.size(), 1);
        QCOMPARE(textOf(rows.first()), QStringLiteral("That turn is no longer kept."));
        QCOMPARE(rows.first().spans.first().fg, palette().muted);
    }
};

QTEST_MAIN(CallLinesTests)
#include "calllines_test.moc"
