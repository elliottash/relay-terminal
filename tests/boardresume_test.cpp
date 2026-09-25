// SPDX-License-Identifier: AGPL-3.0-or-later
// The Resume card action (#FYEY step 8). `land.py orphans --json` lists, per card, the land
// sessions that died — their pane closed or went stale — still holding uncommitted hunks. When
// it lists the card the page is showing, the action row carries Resume card: what Run does, with
// the orphan listing — session, path, hunks, snapshot age — and the landing steps as the pane's
// first prompt. A card the listing does not name gets no action, and a listing run that fails
// (or no `land.py` at all) hides the action rather than breaking the row.
//
// Its own executable rather than more of boardpane_test.cpp or boardexecute_test.cpp, which
// several sessions hold at once (the same reasoning boardexecute_test.cpp records).
#include "BoardPane.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "planning", "executing", "needs-verification", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"],
        "planning": ["planning"], "executing": ["executing"],
        "needs-verification": ["needs-verification"], "done": ["done", "dropped"]},
      "all_statuses": ["inbox", "discussing", "planning", "executing",
        "needs-verification", "done", "dropped"],
      "tabs": [{"id": "features", "folder": "features"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

QJsonObject row(const QString &id, const QString &status)
{
    return QJsonObject{{"id", id}, {"title", id + QStringLiteral(" card")}, {"type", "work"},
                       {"status", status}, {"tab", "features"}, {"rank", "i"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"}};
}

QJsonObject opened(const QList<QJsonObject> &cards)
{
    QJsonArray items;
    for (const QJsonObject &item : cards)
        items << item;
    return QJsonObject{{"event", "board"}, {"config", config()}, {"cards", items},
                       {"problems", QJsonArray{}}};
}

// A `board_card` answer (protocol 19.2). The card carries no session claim: the resume press has
// to be the hand-off, not a reveal of a pane that already holds the card.
QJsonObject cardArrived(const QString &id, const QString &status)
{
    return QJsonObject{{"event", "board_card"}, {"card_id", id},
                       {"title", id + QStringLiteral(" card")}, {"status", status},
                       {"tab", "features"}, {"hash", "h1"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"},
                       {"body", QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id)},
                       {"issue", QStringLiteral("the ask")}, {"issue_heading", "Issue"},
                       {"thread", QJsonArray{}}, {"thread_total", 0},
                       {"front", QJsonObject{}}};
}

// A data root `dataRoot()` accepts — `RELAY_DATA_DIR` names it and it holds the
// `backend/worker.py` the probe looks for — with a `scripts/land.py` that answers
// `orphans --json` listing card `#FYAA` only (or fails, when `fail` is set). Every run appends to
// `runs.log` beside it, so a test can tell that a listing run has finished.
static void makeDataRoot(QTemporaryDir &dir, bool fail = false)
{
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.filePath(QStringLiteral("backend"))).mkpath(QStringLiteral(".")));
    QFile worker(dir.filePath(QStringLiteral("backend/worker.py")));
    QVERIFY(worker.open(QIODevice::WriteOnly));
    worker.write("# the sentinel dataRoot() probes for\n");
    const bool withScript = QDir(dir.filePath(QStringLiteral("scripts"))).mkpath(QStringLiteral("."));
    if (withScript) {
        QFile script(dir.filePath(QStringLiteral("scripts/land.py")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        const QByteArray body = fail
            ? QByteArrayLiteral("import os, sys\n"
                                "with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "
                                "'runs.log'), 'a') as log:\n"
                                "    log.write('orphans\\n')\n"
                                "sys.exit(3)\n")
            : QByteArrayLiteral("#!/usr/bin/env python3\n"
                                "import json, os, sys\n"
                                "with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "
                                "'runs.log'), 'a') as log:\n"
                                "    log.write('orphans\\n')\n"
                                "if sys.argv[1:2] == ['orphans'] and '--json' in sys.argv:\n"
                                "    print(json.dumps({\n"
                                "        'cards': [\n"
                                "            {'card': '#FYAA',\n"
                                "             'sessions': [{'session': 'dead-hand', "
                                "'token': 'deadbeef00000000', 'pane': '7',\n"
                                "                           'owner': 'subagent b',\n"
                                "                           'claims': [{'path': 'src/Foo.cpp', "
                                "'hunks': 3, 'snapshot_age_minutes': 12}]}]},\n"
                                "            {'card': '', 'sessions': []}\n"
                                "        ]\n"
                                "    }))\n");
        script.write(body);
    }
    qputenv("RELAY_DATA_DIR", dir.path().toUtf8());
}

}  // namespace

class BoardResumeTests : public QObject {
    Q_OBJECT

private slots:
    void aListedCardGetsTheActionAndThePrefilledPrompt();
    void anUnlistedCardGetsNoAction();
    void aFailingOrMissingScriptHidesTheAction();
};

// The open card's action with this key, out of the same list the card page's agent console draws
// its buttons from (#DEH6): `BoardView::cardActions()` → `CardDetail::cardActions()` — testing
// the list tests the buttons, minus the paint.
static relay::agent::Action cardAction(const relay::BoardView &view, const QString &key)
{
    for (const relay::agent::Action &action : view.cardActions())
        if (action.key == key)
            return action;
    return {};
}

static bool hasCardAction(const relay::BoardView &view, const QString &key)
{
    return cardAction(view, key).key == key;
}

// Open one card's page on `view`, the way boardexecute_test.cpp does, and answer the
// `board_card_get` it sends.
static void openCardPage(relay::BoardView &view, QList<QJsonObject> &sent, const QString &id)
{
    view.handleEvent(opened({row(id, QStringLiteral("inbox"))}));
    view.selectCard(id);
    view.openSelected();
    QJsonObject answer = cardArrived(id, QStringLiteral("inbox"));
    answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")));
    view.paneExists = [](const QString &) { return false; };
    view.handleEvent(answer);
}

void BoardResumeTests::aListedCardGetsTheActionAndThePrefilledPrompt()
{
    QTemporaryDir data;
    makeDataRoot(data);
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };

    openCardPage(view, sent, QStringLiteral("FYAA"));

    // `land.py orphans --json` runs asynchronously; once it has answered, the row carries the
    // action.
    QTRY_VERIFY(hasCardAction(view, QStringLiteral("boardResume")));
    const relay::agent::Action action = cardAction(view, QStringLiteral("boardResume"));
    QCOMPARE(action.label, QStringLiteral("Resume card"));
    QVERIFY(action.enabled);

    // Pressing it is what Run does: the card goes to a terminal pane, whose first prompt is the
    // orphan listing and the landing steps — read `## Done means` before landing anything.
    QString paneCard, paneTask;
    bool paneBackground = false;
    view.onExecuteCard = [&](const QString &id, const QString &task, bool background) {
        paneCard = id;
        paneTask = task;
        paneBackground = background;
        return QStringLiteral("panetoken1");
    };
    sent.clear();
    action.run();
    QCOMPARE(paneCard, QStringLiteral("FYAA"));
    QVERIFY(paneBackground);
    // The listing names the dead session, the path, the hunk count and the snapshot age.
    QVERIFY(paneTask.contains(QStringLiteral("dead-hand")));
    QVERIFY(paneTask.contains(QStringLiteral("src/Foo.cpp")));
    QVERIFY(paneTask.contains(QStringLiteral("3 hunk")));
    QVERIFY(paneTask.contains(QStringLiteral("12 min")));
    // The landing steps are the plan's sentence: salvage reads `## Done means` first.
    QVERIFY(paneTask.contains(QStringLiteral("read `## Done means`")));
    QVERIFY(paneTask.contains(QStringLiteral("land.py who")));
    QVERIFY(paneTask.contains(QStringLiteral("land.py commit <session>")));
    QVERIFY(paneTask.contains(QStringLiteral("land.py abandon <session>")));
    // The claim the worker gets is Run's pane claim with Resume wording, naming the pane.
    QCOMPARE(sent.size(), 1);
    const QJsonObject claim = sent.last();
    QCOMPARE(claim.value(QStringLiteral("type")).toString(), QStringLiteral("board_claim"));
    QCOMPARE(claim.value(QStringLiteral("card")).toString(), QStringLiteral("FYAA"));
    QCOMPARE(claim.value(QStringLiteral("pane_token")).toString(), QStringLiteral("panetoken1"));
}

// The listing names `#FYAA` only: opening another card's page must not carry the action, and
// opening the listed card again — the read again on demand — must carry it once more.
void BoardResumeTests::anUnlistedCardGetsNoAction()
{
    QTemporaryDir data;
    makeDataRoot(data);
    const QString log = data.filePath(QStringLiteral("scripts/runs.log"));
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };

    openCardPage(view, sent, QStringLiteral("NOPE"));
    QTRY_VERIFY(QFileInfo::exists(log));   // the listing ran; `#NOPE` is not in it
    QVERIFY(!hasCardAction(view, QStringLiteral("boardResume")));

    openCardPage(view, sent, QStringLiteral("FYAA"));
    QTRY_VERIFY(hasCardAction(view, QStringLiteral("boardResume")));
}

// A listing run that fails — and a data root with no `land.py` at all — means no action: an
// empty answer is a hidden action, never a broken row.
void BoardResumeTests::aFailingOrMissingScriptHidesTheAction()
{
    QTemporaryDir failing;
    makeDataRoot(failing, true);
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };

    openCardPage(view, sent, QStringLiteral("FYAA"));
    QTRY_VERIFY(QFileInfo::exists(failing.filePath(QStringLiteral("scripts/runs.log"))));
    QVERIFY(!hasCardAction(view, QStringLiteral("boardResume")));

    // No `scripts/land.py`: nothing to ask, so nothing ever appears.
    QTemporaryDir bare;
    makeDataRoot(bare, true);
    QFile::remove(bare.filePath(QStringLiteral("scripts/land.py")));
    openCardPage(view, sent, QStringLiteral("FYAA"));
    QVERIFY(!hasCardAction(view, QStringLiteral("boardResume")));
}

QTEST_MAIN(BoardResumeTests)
#include "boardresume_test.moc"
