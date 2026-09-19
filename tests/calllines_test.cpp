// SPDX-License-Identifier: AGPL-3.0-or-later
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
    p.link = QColor(0x12, 0xa4, 0x57);
    p.add = QColor(0x7e, 0xc8, 0x8c);
    p.remove = QColor(0xe0, 0x6c, 0x75);
    p.addBg = QColor(0x1a, 0x28, 0x1d);
    p.removeBg = QColor(0x2a, 0x1a, 0x1c);
    p.error = QColor(0xf0, 0x71, 0x78);
    p.accent = QColor(0x3e, 0xc5, 0xf0);
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

    // Issue #EC58: a pane restored from saved scrollback has no CallRecord for any of its rows, and
    // `Pane::rememberCall()` is bounded besides, so `foldRequested()`/`openCallTarget()` fall back
    // to the anchor itself for the `tool_output_get` they send. That only works if every anchor a
    // row can carry yields a usable turn *and* a single call id — including a merged run's anchor
    // (the first member's id) and ids that had to be percent-encoded.
    void everyAnchorCarriesEnoughToRefetchTheCall() {
        const QStringList ids{QStringLiteral("call_1"), QStringLiteral("a/b"), QStringLiteral("c+2"),
                              QStringLiteral("id with space"), QStringLiteral("%2F")};
        for (const QString &id : ids) {
            for (int extra : {0, 6}) {
                const Ref fold = parseUri(foldUri(QStringLiteral("pane1"), QStringLiteral("turn-9"), id, extra));
                QVERIFY(fold.valid);
                QCOMPARE(fold.turn, QStringLiteral("turn-9"));
                QCOMPARE(fold.call, id);          // never empty, so the fallback always has one
            }
            const Ref open = parseUri(openUri(QStringLiteral("pane1"), QStringLiteral("turn-9"), id));
            QVERIFY(open.valid);
            QCOMPARE(open.turn, QStringLiteral("turn-9"));
            QCOMPARE(open.call, id);
        }
    }

    // Issue #7FD3: the "open in pane" link at a fold's foot is not the row's anchor — it is
    // relay://open-call/…, while the pane's call records are keyed by the anchor — so
    // `openCallTarget()` never finds a record under it and always falls back to the ids the link
    // itself carries. That link is openUri(pane, ref.turn, ref.call) for the anchor's parsed ref
    // (`Pane::foldOptions()`), so it must read back to a non-empty turn and a single call id — the
    // first member's for a merged run, whose `+count` tail must not survive into the link — or the
    // fold's link fails the way #7FD3's did, on a call that was seconds old.
    void theFoldOpenInPaneLinkNamesOneRefetchableCall() {
        const QStringList ids{QStringLiteral("call_1"), QStringLiteral("a/b"), QStringLiteral("c+2"),
                              QStringLiteral("id with space"), QStringLiteral("%2F")};
        for (const QString &id : ids) {
            for (int extra : {0, 6}) {
                const QString anchor = foldUri(QStringLiteral("pane1"), QStringLiteral("turn-9"), id, extra);
                const Ref row = parseUri(anchor);
                // exactly what Pane::foldOptions() puts in FoldOptions::openInPane
                const QString link = openUri(QStringLiteral("pane1"), row.turn, row.call);
                QVERIFY(link.startsWith(QStringLiteral("relay://open-call/")));
                const Ref ref = parseUri(link);
                QVERIFY(ref.valid);
                QVERIFY(!ref.fold);
                QVERIFY(!ref.turn.isEmpty());
                QCOMPARE(ref.turn, QStringLiteral("turn-9"));
                QCOMPARE(ref.call, id);          // the id openCallTarget() falls back to
                QVERIFY(!ref.merged());          // one call, never a run of `extra`
            }
        }
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

    // #WXT6: a small diff folds under its row collapsed, and the fold is answered from the diff
    // the surface already holds — the same rows a `detail` section of style "diff" would give.
    void aStoredDiffFoldsWithoutAWorker() {
        const QString diff = QStringLiteral("--- a/x.py\n+++ b/x.py\n@@ -1,2 +1,2 @@\n keep\n-old\n+new\n");
        FoldOptions options;
        options.openInPane = QStringLiteral("relay://open-call/p/t/c1");
        options.openPath = QStringLiteral("/w/x.py");
        const QVector<FoldLine> rows = foldForDiff(diff, palette(), options);
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("@@ -1,2 +1,2 @@"), QStringLiteral(" keep"),
                                             QStringLiteral("-old"), QStringLiteral("+new"),
                                             QStringLiteral("open in pane  ·  open x.py")}));
        QCOMPARE(rows.at(2).spans.first().fg, palette().remove);
        QCOMPARE(rows.at(2).spans.first().bg, palette().removeBg);
        QCOMPARE(rows.at(3).spans.first().fg, palette().add);
        QCOMPARE(rows.at(3).spans.first().bg, palette().addBg);
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

    // ---- the task list of an update_todos call (card #BDXG) ---------------------------------

    void aTaskSectionBecomesOneGlyphedRowPerTask() {
        const QJsonObject reply = json(
            "{'detail': [{'heading': 'tasks', 'style': 'tasks',"
            "'text': '[completed] read the card\\n[in_progress] write the fold\\n"
            "[pending] run the tests\\n[blocked] ask the owner\\n[deferred] the icons\\n"
            "[cancelled] the old plan'}]}");
        const QVector<FoldLine> rows = foldForReply(reply, palette(), {});
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("\u2713  read the card"),
                                             QStringLiteral("\u25D0  write the fold"),
                                             QStringLiteral("\u25CB  run the tests"),
                                             QStringLiteral("\u2717  ask the owner"),
                                             QStringLiteral("\u23F8  the icons"),
                                             QStringLiteral("\u2715  the old plan")}));
        // The inks the tasks panel gives them: done and cancelled are behind you, one is live.
        QCOMPARE(rows.at(0).spans.first().fg, palette().muted);
        QCOMPARE(rows.at(1).spans.first().fg, palette().accent);
        QCOMPARE(rows.at(2).spans.first().fg, palette().text);
        QCOMPARE(rows.at(3).spans.first().fg, palette().error);
        QCOMPARE(rows.at(5).spans.first().fg, palette().muted);
    }

    void theGlyphsAreTheOnesTheTasksPanelDraws() {
        QCOMPARE(taskGlyph(QStringLiteral("pending")), QStringLiteral("\u25CB"));
        QCOMPARE(taskGlyph(QStringLiteral("in_progress")), QStringLiteral("\u25D0"));
        QCOMPARE(taskGlyph(QStringLiteral("completed")), QStringLiteral("\u2713"));
        QCOMPARE(taskGlyph(QStringLiteral("done")), QStringLiteral("\u2713"));
        QCOMPARE(taskGlyph(QStringLiteral("cancelled")), QStringLiteral("\u2715"));
        QCOMPARE(taskGlyph(QStringLiteral("cancelled_by_user")), QStringLiteral("\u2715"));
        QCOMPARE(taskGlyph(QStringLiteral("deferred")), QStringLiteral("\u23F8"));
        QCOMPARE(taskGlyph(QStringLiteral("blocked")), QStringLiteral("\u2717"));
        QCOMPARE(taskGlyph(QString()), QStringLiteral("\u25CB"));
    }

    void anEmptiedListSaysSoRatherThanFoldingToNothing() {
        const QJsonObject reply = json("{'detail': [{'heading': 'tasks', 'style': 'tasks', 'text': ''}]}");
        QCOMPARE(textsOf(foldForReply(reply, palette(), {})),
                 (QStringList{QStringLiteral("(The list was emptied.)")}));
    }

    // The anchor's scheme, both answers. On this machine every engine core reports the Folds
    // capability, so the `backendFolds == false` column cannot be reached by running the app: it is
    // the rule itself that is tested, and Pane::callAnchor() calls exactly this (#BDXG).
    void aTaskRowAnchorsAFoldOnlyWhenTheBackendHasOne() {
        QVERIFY(anchorsFold(Click::Todos, false, true));      // the row folds to its task list
        QVERIFY(anchorsFold(Click::Fold, false, true));
        QVERIFY(anchorsFold(Click::File, true, true));        // a merged run always folds
        QVERIFY(!anchorsFold(Click::File, false, true));      // opens a preview pane
        QVERIFY(!anchorsFold(Click::Diff, false, true));
        QVERIFY(!anchorsFold(Click::Subagent, false, true));
        // No fold layer: every row anchors relay://open-call, so a click still reaches the detail.
        for (const Click click : {Click::Todos, Click::Fold, Click::File, Click::Diff})
            QVERIFY(!anchorsFold(click, false, false));
        QVERIFY(!anchorsFold(Click::Fold, true, false));
    }

    // #EC58: a pane restored from saved scrollback holds no CallRecord, so Pane::handleFoldReply()
    // builds the label from the worker's reply before it asks for the fold's options. This is that
    // reply, exactly as the worker sends it for an update_todos call, and nothing else.
    void aRestoredPaneGetsTheTaskFoldFromTheReplyAlone() {
        const QJsonObject reply = json(
            "{'id': 'fold-7', 'turn_id': 't1', 'call_id': 'c1',"
            " 'label': {'kind': 'plan', 'running': 'updating tasks', 'title': 'updated tasks',"
            "           'stats': ['2 open'], 'ok': true, 'open': {'type': 'todos'}},"
            " 'detail': [{'heading': 'tasks', 'style': 'tasks',"
            "             'text': '[in_progress] write the fold\\n[pending] run the tests'}]}");
        const Label rebuilt = relay::toollabel::fromEvent(reply);
        QVERIFY(rebuilt.valid);
        QCOMPARE(rebuilt.title, QStringLiteral("updated tasks"));
        // The label alone is enough to decide that this row folds to the task list.
        QCOMPARE(clickFor(rebuilt), Click::Todos);
        QVERIFY(anchorsFold(clickFor(rebuilt), false, true));

        // And the reply alone carries the rows, with no record and no stored diff.
        FoldOptions options;
        options.openInPane = QStringLiteral("relay://tasks/p1");
        options.openInPaneText = QStringLiteral("open the task list");
        QCOMPARE(textsOf(foldForReply(reply, palette(), options)),
                 (QStringList{QStringLiteral("\u25D0  write the fold"),
                              QStringLiteral("\u25CB  run the tests"),
                              QStringLiteral("open the task list")}));
    }

    // A backend with no fold layer shows the call's detail in a preview pane instead
    // (Pane::openToolOutput): the same list, as text, and not the result's JSON (#BDXG).
    void aFoldlessBackendReadsTheSameTaskListAsText() {
        const QJsonObject reply = json(
            "{'id': 'turn-3', 'turn_id': 't1', 'call_id': 'c1', 'name': 'update_todos',"
            " 'label': {'kind': 'plan', 'running': 'updating tasks', 'title': 'updated tasks',"
            "           'stats': ['2 open'], 'ok': true, 'open': {'type': 'todos'}},"
            " 'result': {'items': [{'id': 'T1', 'text': 'write the fold', 'status': 'in_progress'}]},"
            " 'detail': [{'heading': 'tasks', 'style': 'tasks',"
            "             'text': '[in_progress] write the fold\\n[pending] run the tests'}]}");
        QCOMPARE(replyAsText(reply),
                 QStringLiteral("✓ updated tasks · 2 open\n\n◐  write the fold\n○  run the tests"));
        // A failed call: its line says so, and the error section follows in full.
        const QJsonObject failed = json(
            "{'name': 'update_todos',"
            " 'label': {'kind': 'plan', 'running': 'updating tasks', 'title': 'update tasks',"
            "           'ok': false, 'error': 'Task 2: status must be one of pending, in_progress.'},"
            " 'detail': [{'heading': 'error', 'style': 'error', 'text': 'Task 2: status must be one of pending, in_progress.'}]}");
        QVERIFY(replyAsText(failed).startsWith(QStringLiteral("✗ ")));
        QVERIFY(replyAsText(failed).endsWith(QStringLiteral("\n\nTask 2: status must be one of pending, in_progress.")));
    }

    void theTaskFoldsLastRowOpensTheTaskList() {
        FoldOptions options;
        options.openInPane = QStringLiteral("relay://tasks/p1");
        options.openInPaneText = QStringLiteral("open the task list");
        const QJsonObject reply = json("{'detail': [{'heading': 'tasks', 'style': 'tasks',"
                                       "'text': '[pending] one'}]}");
        const QVector<FoldLine> rows = foldForReply(reply, palette(), options);
        const FoldLine &last = rows.last();
        QCOMPARE(last.spans.size(), 1);
        QCOMPARE(last.spans.first().text, QStringLiteral("open the task list"));
        QCOMPARE(last.spans.first().link, QStringLiteral("relay://tasks/p1"));
        QVERIFY(last.spans.first().underline);
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

    // ---- reasoning as a fold (foldForMarkdown) --------------------------------------------------

    // A thinking fold's markdown keeps its shape — markers dropped, rows split at newlines — and
    // its styles become spans: prose muted (reasoning is chrome), bold kept, inline code bold in the
    // same muted ink (the terminal shows it bold and plain too, since 2026-09-19).
    void markdownReasoningKeepsItsShapeAndMutesItsProse() {
        const QVector<FoldLine> rows = foldForMarkdown(
            QStringLiteral("Plain **bold** and `code`\n"), palette(), {});
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("Plain bold and code")}));
        for (const relay::FoldSpan &span : rows.first().spans) {
            if (span.text.contains(QStringLiteral("bold"))) {
                QVERIFY(span.bold);
                QCOMPARE(span.fg, palette().muted);
            } else if (span.text.contains(QStringLiteral("code"))) {
                QVERIFY(span.bold);
                QCOMPARE(span.fg, palette().muted);
            } else {
                QCOMPARE(span.fg, palette().muted);
            }
        }
    }

    void markdownHeadingsLinksQuotesAndProblemsKeepTheirInks() {
        const QVector<FoldLine> rows = foldForMarkdown(
            QStringLiteral("## Plan\nsee [docs](https://x/y)\n**Problem:** broke\n> quoted\n"), palette(), {});
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("Plan"), QStringLiteral("see docs (https://x/y)"),
                                             QStringLiteral("Problem: broke"), QStringLiteral("▎ quoted")}));
        QVERIFY(rows.at(0).spans.first().bold);                       // a heading is bold ...
        QCOMPARE(rows.at(0).spans.first().fg, palette().text);        // ... in the plain text ink
        bool sawLink = false;
        for (const relay::FoldSpan &span : rows.at(1).spans) {
            if (span.text == QStringLiteral("docs")) {
                sawLink = true;
                QVERIFY(span.underline);
                QCOMPARE(span.fg, palette().link);   // the link ink, not the code ink (2026-09-19)
            }
        }
        QVERIFY(sawLink);
        QVERIFY(rows.at(2).spans.first().bold);
        QCOMPARE(rows.at(2).spans.first().fg, palette().error);       // **Problem:** is red
        // A quote: the ▎ marker carries the marker ink, the text is italic and dim.
        QCOMPARE(rows.at(3).spans.at(0).text.at(0), QChar(0x258e));
        QCOMPARE(rows.at(3).spans.at(0).fg, palette().text);
        QVERIFY(rows.at(3).spans.at(1).italic);
        QVERIFY(rows.at(3).spans.at(1).dim);
    }

    void aCodeFenceStaysVerbatimInItsOwnInk() {
        const QVector<FoldLine> rows = foldForMarkdown(QStringLiteral("```sh\n$ make\n```\n"), palette(), {});
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("```sh"), QStringLiteral("$ make"),
                                             QStringLiteral("```")}));
        QCOMPARE(rows.at(1).spans.first().fg, palette().code);
    }

    void emptyReasoningFoldsToNothing() {
        QVERIFY(foldForMarkdown(QString(), palette(), {}).isEmpty());
        QVERIFY(foldForMarkdown(QStringLiteral("   \n"), palette(), {}).isEmpty());
    }

    // `tail`: the end of the reasoning is what a reader watching a stream is looking for.
    void aLongThinkingFoldKeepsItsTail() {
        QString text;
        for (int at = 0; at < 30; ++at) text += QStringLiteral("thought %1\n").arg(at);
        FoldOptions options;
        options.maxLines = 10;
        options.openInPane = QStringLiteral("relay://open-call/p/t/thinking");
        const QStringList rows = textsOf(foldForMarkdown(text, palette(), options, 0, true));
        QCOMPARE(rows.size(), 12);   // the note, the last 10 rows, the links row
        QVERIFY(rows.first().startsWith(QStringLiteral("… 20 earlier lines")));
        QCOMPARE(rows.at(1), QStringLiteral("thought 20"));
        QCOMPARE(rows.at(10), QStringLiteral("thought 29"));
        QCOMPARE(rows.at(11), QStringLiteral("open in pane"));
    }

    // Without `tail` — the settled fold a reader opened by hand — the cap keeps the *start*, where
    // reading begins, and says how much is below (#K48R).
    void aSettledThinkingFoldKeepsItsHead() {
        QString text;
        for (int at = 0; at < 30; ++at) text += QStringLiteral("thought %1\n").arg(at);
        FoldOptions options;
        options.maxLines = 10;
        options.openInPane = QStringLiteral("relay://turn/p/t");
        const QStringList rows = textsOf(foldForMarkdown(text, palette(), options));
        QCOMPARE(rows.size(), 12);   // the first 10 rows, the note, the links row
        QCOMPARE(rows.first(), QStringLiteral("thought 0"));
        QCOMPARE(rows.at(9), QStringLiteral("thought 9"));
        QCOMPARE(rows.at(10), QStringLiteral("… 20 more lines · open in pane"));
        QCOMPARE(rows.at(11), QStringLiteral("open in pane"));
    }

    // The caps are the owner's two numbers (#K48R) and they count *rendered* rows: a several
    // thousand line block, and a single 5,000-character paragraph, are both cut to six rows while
    // the block streams and eighteen once it has settled — at 40 columns as at 100.
    void theThinkingCapsHoldOnAnyLengthOrShapeOfReasoning() {
        QString many;
        for (int at = 0; at < 4000; ++at) many += QStringLiteral("line %1 of the model's reasoning\n").arg(at);
        QString paragraph;
        while (paragraph.size() < 5000) paragraph += QStringLiteral("and then it considered the ponies again ");
        for (int cells : {40, 100}) {
            for (const QString &text : {many, paragraph}) {
                FoldOptions stream;
                stream.maxLines = kThinkingStreamRows;
                stream.openInPane = QStringLiteral("relay://turn/p/t");
                QStringList rows = textsOf(foldForMarkdown(text, palette(), stream, cells, true));
                // The six rows of reasoning, the row naming what was cut, and the link row.
                QCOMPARE(rows.size(), kThinkingStreamRows + 2);
                QVERIFY(rows.first().startsWith(QStringLiteral("… ")));
                QVERIFY(rows.first().endsWith(QStringLiteral(" earlier lines · open in pane")));
                QCOMPARE(rows.last(), QStringLiteral("open in pane"));
                for (const QString &row : rows) QVERIFY2(row.size() <= cells, qPrintable(row));

                FoldOptions done = stream;
                done.maxLines = kThinkingDoneRows;
                rows = textsOf(foldForMarkdown(text, palette(), done, cells, false));
                QCOMPARE(rows.size(), kThinkingDoneRows + 2);
                QVERIFY(rows.at(kThinkingDoneRows).startsWith(QStringLiteral("… ")));
                QVERIFY(rows.at(kThinkingDoneRows).endsWith(QStringLiteral(" more lines · open in pane")));
                QCOMPARE(rows.last(), QStringLiteral("open in pane"));
                for (const QString &row : rows) QVERIFY2(row.size() <= cells, qPrintable(row));
            }
        }
    }

    // Wrapping is the fold layer's own (relay::wrapFoldLines, engine/TerminalBackend.h): a row that
    // fits is left alone, and the spans — with their ink and their links — survive the split.
    void wrappingForTheCapKeepsTheSpansAndTheirInk() {
        FoldOptions options;
        options.maxLines = 100;
        const QVector<FoldLine> rows = foldForMarkdown(
            QStringLiteral("short\nsee [docs](https://example.com/a/very/long/path)\n"), palette(), options, 12);
        QCOMPARE(textOf(rows.first()), QStringLiteral("short"));
        QStringList texts = textsOf(rows);
        for (const QString &row : texts) QVERIFY2(row.size() <= 12, qPrintable(row));
        QCOMPARE(texts.join(QString()), QStringLiteral("shortsee docs (https://example.com/a/very/long/path)"));
        bool sawLinkInk = false;
        for (const FoldLine &row : rows)
            for (const relay::FoldSpan &span : row.spans)
                if (span.underline && span.fg == palette().link) sawLinkInk = true;
        QVERIFY(sawLinkInk);
    }
};

QTEST_MAIN(CallLinesTests)
#include "calllines_test.moc"
