// SPDX-License-Identifier: AGPL-3.0-or-later
// The card's `## Tests` strip and its Check (card #7BM4, protocol section 31.5).
//
// A card that names the tests which prove it gets a strip above its body: how many it lists,
// when it was last checked and what that check said, and a Check button. Pressing Check sends
// one `tests_check` to the tab's board worker; the answer draws the findings as clickable rows
// and at most three action buttons — the ones the worker offered, never a fourth invented here.
// A card with no `## Tests` section has no strip at all.
//
// The other half is the gate: a move out of `needs-verification` that the worker refuses with
// `tests_gate` shows in the board's notice bar with an "Override…" beside it, and that button
// re-sends the very same move with the reason on it.
//
// Driven the way tests/boardpane_test.cpp drives the pane: a real BoardView, fake worker events
// in, the messages it sends captured. Nothing is shown, so visibility is read as `isHidden()`.
#include "BoardPane.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QtTest>

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "in-progress", "needs-verification", "done"],
      "column_statuses": {"inbox": ["inbox"], "in-progress": ["in-progress"],
        "needs-verification": ["needs-verification"], "done": ["done"]},
      "all_statuses": ["inbox", "in-progress", "needs-verification", "done"],
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

// A `board_card` answer (19.2). `sections` is the body's `## ` headings, which is what the strip
// asks about — the same key `hasPlan()` reads.
QJsonObject cardArrived(const QString &id, const QString &status, const QString &tests,
                        const QStringList &sections)
{
    QJsonArray headings;
    for (const QString &heading : sections)
        headings << heading;
    QString body = QStringLiteral("# %1 card\n\n## Issue\nthe ask\n").arg(id);
    if (!tests.isEmpty())
        body += QStringLiteral("\n## Tests\n") + tests;
    return QJsonObject{{"event", "board_card"}, {"card_id", id},
                       {"title", id + QStringLiteral(" card")}, {"status", status},
                       {"tab", "features"}, {"hash", "h1"},
                       {"path", QStringLiteral("issues/features/") + id + ".md"},
                       {"body", body}, {"issue", QStringLiteral("the ask")},
                       {"issue_heading", "Issue"}, {"sections", headings},
                       {"thread", QJsonArray{}}, {"thread_total", 0}};
}

// One `tests_check` answer (31.2), with the three machine keys phase 4 added.
QJsonObject checkAnswered(const QString &card, const QJsonArray &findings,
                          const QStringList &actions, const QJsonObject &files = {},
                          const QStringList &ids = {}, const QStringList &failing = {})
{
    QJsonArray actionList, idList, failingList;
    for (const QString &action : actions)
        actionList << action;
    for (const QString &id : ids)
        idList << id;
    for (const QString &id : failing)
        failingList << id;
    return QJsonObject{{"event", "tests_check"}, {"card", card}, {"findings", findings},
                       {"actions", actionList}, {"ids", idList}, {"files", files},
                       {"failing", failingList}};
}

QJsonObject finding(const QString &test, const QString &verdict, const QString &severity,
                    const QString &message)
{
    return QJsonObject{{"test", test}, {"verdict", verdict}, {"severity", severity},
                       {"message", message}};
}

}  // namespace

class CardTestsTests : public QObject {
    Q_OBJECT

private:
    // A pane with one card open, the card's own `## Tests` body as given.
    void open(relay::BoardView &view, QList<QJsonObject> &sent, const QString &id,
              const QString &tests, const QString &status = QStringLiteral("needs-verification"))
    {
        view.onSend = [&sent](const QJsonObject &message) { sent << message; };
        view.handleEvent(opened({row(id, status)}));
        view.setCollapsedSections(QJsonArray{});
        view.selectCard(id);
        view.openSelected();
        QStringList sections{QStringLiteral("Issue")};
        if (!tests.isEmpty())
            sections << QStringLiteral("Tests");
        QJsonObject answer = cardArrived(id, status, tests, sections);
        answer.insert(QStringLiteral("id"), sent.last().value(QStringLiteral("id")).toString());
        view.handleEvent(answer);
    }

    static QWidget *strip(relay::BoardView &view)
    {
        return view.findChild<QWidget *>(QStringLiteral("boardTestsStrip"));
    }
    static QPushButton *check(relay::BoardView &view)
    {
        return view.findChild<QPushButton *>(QStringLiteral("boardTestsCheck"));
    }
    static QList<QLabel *> findings(relay::BoardView &view)
    {
        QWidget *box = view.findChild<QWidget *>(QStringLiteral("boardTestsFindings"));
        return box ? box->findChildren<QLabel *>(QStringLiteral("boardTestsFinding"))
                   : QList<QLabel *>{};
    }
    static QStringList actionLabels(relay::BoardView &view)
    {
        QStringList out;
        QWidget *box = view.findChild<QWidget *>(QStringLiteral("boardTestsActions"));
        if (box)
            for (QPushButton *button : box->findChildren<QPushButton *>())
                out << button->text();
        return out;
    }
    static QPushButton *action(relay::BoardView &view, const QString &prefix)
    {
        QWidget *box = view.findChild<QWidget *>(QStringLiteral("boardTestsActions"));
        if (box)
            for (QPushButton *button : box->findChildren<QPushButton *>())
                if (button->text().startsWith(prefix))
                    return button;
        return nullptr;
    }

private slots:
    void theStripIsOnlyThereForACardThatListsTests();
    void theLineCountsTheTestsAndNamesTheLastCheck();
    void checkSendsOneRequestForTheOpenCard();
    void theFindingsRenderAsRowsAndTheActionsAreTheWorkersOwn();
    void anEmptyCheckIsOneSentenceAndNothingElse();
    void theActionsSendWhatTheWorkerNamed();
    void anAnswerAboutAnotherCardIsNotDrawn();
    void aRefusedMoveOffersAnOverrideThatResendsIt();
};

// The strip is a control for a list, so it is there when the list is. A card with no `## Tests`
// section shows nothing at all — the missing section is the worker's advisory finding, not a
// button on every card in the board.
void CardTestsTests::theStripIsOnlyThereForACardThatListsTests()
{
    relay::BoardView with(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(with, sent, QStringLiteral("K7Q2"), QStringLiteral("- `ctest -R board`\n"));
    QVERIFY(with.detailOpen());
    QVERIFY(strip(with));
    QVERIFY(!strip(with)->isHidden());

    relay::BoardView without(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> other;
    open(without, other, QStringLiteral("M3XJ"), QString());
    QVERIFY(without.detailOpen());
    QVERIFY(strip(without));
    QVERIFY(strip(without)->isHidden());
}

// The header line: how many tests the section lists, and what the worker's last dated block
// said. The `### Check` block's own lines are not tests, so they are not counted.
void CardTestsTests::theLineCountsTheTestsAndNamesTheLastCheck()
{
    relay::BoardView never(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(never, sent, QStringLiteral("K7Q2"),
         QStringLiteral("- `ctest -R board`\n- `tests/test_board.py::CardTests::test_x`\n"));
    QLabel *line = never.findChild<QLabel *>(QStringLiteral("boardTestsLine"));
    QVERIFY(line);
    QVERIFY(line->text().contains(QStringLiteral("2 listed")));
    QVERIFY(line->text().contains(QStringLiteral("never checked")));

    relay::BoardView checked(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> more;
    open(checked, more, QStringLiteral("M3XJ"),
         QStringLiteral("- `ctest -R board`\n\n### Check 2026-09-01 09:00\n"
                        "- notice · ctest:board — an older answer\n\n"
                        "### Check 2026-09-20 21:04\n- no findings\n"));
    QLabel *second = checked.findChild<QLabel *>(QStringLiteral("boardTestsLine"));
    QVERIFY(second);
    // One test, not three: the two blocks' bullet lines are prose about tests, not tests.
    QVERIFY2(second->text().contains(QStringLiteral("1 listed")), qPrintable(second->text()));
    // The newest block wins, and its verdict is the line's tail.
    QVERIFY(second->text().contains(QStringLiteral("2026-09-20 21:04")));
    QVERIFY(second->text().contains(QStringLiteral("no findings")));
    QVERIFY(!second->text().contains(QStringLiteral("an older answer")));
}

void CardTestsTests::checkSendsOneRequestForTheOpenCard()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(view, sent, QStringLiteral("K7Q2"), QStringLiteral("- `ctest -R board`\n"));
    const int before = int(sent.size());
    QVERIFY(check(view));
    check(view)->click();
    QCOMPARE(int(sent.size()), before + 1);
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("tests_check"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));
    QVERIFY(!sent.last().value(QStringLiteral("id")).toString().isEmpty());
    // While it is out, the button says so and cannot be pressed twice.
    QVERIFY(!check(view)->isEnabled());
    view.handleEvent(checkAnswered(QStringLiteral("K7Q2"), QJsonArray{}, {}));
    QVERIFY(check(view)->isEnabled());
    QCOMPARE(check(view)->text(), QStringLiteral("Check"));
}

void CardTestsTests::theFindingsRenderAsRowsAndTheActionsAreTheWorkersOwn()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(view, sent, QStringLiteral("K7Q2"),
         QStringLiteral("- `ctest -R gone`\n- `ctest -R beta`\n"));
    const QJsonArray items{
        finding(QStringLiteral("ctest:gone"), QStringLiteral("gone"), QStringLiteral("failure"),
                QStringLiteral("ctest -R gone is not in the project any more")),
        finding(QStringLiteral("ctest:beta"), QStringLiteral("never-run"),
                QStringLiteral("failure"), QStringLiteral("ctest -R beta has never run here"))};
    view.handleEvent(checkAnswered(QStringLiteral("K7Q2"), items,
                                   {QStringLiteral("Run these"),
                                    QStringLiteral("Open the failing one")},
                                   QJsonObject{{"ctest:beta", "tests/beta_test.cpp"}},
                                   {QStringLiteral("ctest:gone"), QStringLiteral("ctest:beta")},
                                   {QStringLiteral("ctest:beta")}));
    // A heading line plus one row per finding.
    const QList<QLabel *> rows = findings(view);
    QCOMPARE(int(rows.size()), 3);
    QVERIFY(rows.at(0)->text().contains(QStringLiteral("2 findings")));
    QVERIFY(rows.at(1)->text().contains(QStringLiteral("ctest:gone")));
    QVERIFY(rows.at(1)->text().contains(QStringLiteral("failure")));
    QVERIFY(rows.at(1)->text().contains(QStringLiteral("not in the project any more")));
    // The test whose source the worker named is a link; the one it did not is plain text.
    QVERIFY(rows.at(2)->text().contains(QStringLiteral("<a href=\"tests/beta_test.cpp\"")));
    QVERIFY(!rows.at(1)->text().contains(QStringLiteral("<a href=")));
    // Exactly the actions the worker offered, in its order, and no fourth.
    QCOMPARE(actionLabels(view), QStringList({QStringLiteral("Run these"),
                                              QStringLiteral("Open the failing one")}));
}

void CardTestsTests::anEmptyCheckIsOneSentenceAndNothingElse()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(view, sent, QStringLiteral("K7Q2"), QStringLiteral("- `ctest -R board`\n"));
    view.handleEvent(checkAnswered(QStringLiteral("K7Q2"), QJsonArray{}, {}));
    const QList<QLabel *> rows = findings(view);
    QCOMPARE(int(rows.size()), 1);
    QVERIFY2(rows.at(0)->text().contains(QStringLiteral("nothing moved")),
             qPrintable(rows.at(0)->text()));
    QVERIFY(actionLabels(view).isEmpty());
    QWidget *box = view.findChild<QWidget *>(QStringLiteral("boardTestsActions"));
    QVERIFY(box && box->isHidden());
}

void CardTestsTests::theActionsSendWhatTheWorkerNamed()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    QStringList opened;
    view.onOpenFile = [&opened](const QString &path) { opened << path; };
    open(view, sent, QStringLiteral("K7Q2"), QStringLiteral("- `ctest -R beta`\n"));
    view.handleEvent(checkAnswered(
            QStringLiteral("K7Q2"),
            QJsonArray{finding(QStringLiteral("ctest:beta"), QStringLiteral("never-run"),
                               QStringLiteral("failure"), QStringLiteral("never run here"))},
            {QStringLiteral("Run these"),
             QStringLiteral("Add the tests this card's commits touched")},
            QJsonObject{{"ctest:beta", "tests/beta_test.cpp"}},
            {QStringLiteral("ctest:beta")}, {}));

    QPushButton *run = action(view, QStringLiteral("Run these"));
    QVERIFY(run);
    run->click();
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("tests_run"));
    QCOMPARE(sent.last().value(QStringLiteral("ids")).toArray().size(), 1);
    QCOMPARE(sent.last().value(QStringLiteral("ids")).toArray().first().toString(),
             QStringLiteral("ctest:beta"));

    QPushButton *add = action(view, QStringLiteral("Add the tests"));
    QVERIFY(add);
    add->click();
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("tests_suggest"));
    QCOMPARE(sent.last().value(QStringLiteral("card")).toString(), QStringLiteral("K7Q2"));
}

// A check about a card this pane is not showing belongs to whoever asked for it: the strip under
// the open card must never draw another card's findings.
void CardTestsTests::anAnswerAboutAnotherCardIsNotDrawn()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(view, sent, QStringLiteral("K7Q2"), QStringLiteral("- `ctest -R board`\n"));
    view.handleEvent(checkAnswered(
            QStringLiteral("M3XJ"),
            QJsonArray{finding(QStringLiteral("ctest:elsewhere"), QStringLiteral("gone"),
                               QStringLiteral("failure"), QStringLiteral("another card's test"))},
            {QStringLiteral("Run these")}));
    QVERIFY(findings(view).isEmpty());
    QVERIFY(actionLabels(view).isEmpty());
}

// The gate. The worker refuses the move with `tests_gate`; the notice bar says so and carries
// Override…, and that button re-sends the same move with a reason on it.
void CardTestsTests::aRefusedMoveOffersAnOverrideThatResendsIt()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    open(view, sent, QStringLiteral("K7Q2"), QStringLiteral("- `ctest -R gone`\n"));

    // The card page's status picker. The signals page (#AQ6X) has pickers of the same object
    // name and its own `boardDetail`, so the one under test is found by what it holds: the
    // board's own statuses, which no other picker carries.
    QComboBox *status = nullptr;
    for (QComboBox *combo : view.findChildren<QComboBox *>(QStringLiteral("boardPicker")))
        if (combo->findData(QStringLiteral("needs-verification")) >= 0)
            status = combo;
    QVERIFY(status);
    const int done = status->findData(QStringLiteral("done"));
    QVERIFY(done >= 0);
    QVERIFY(QMetaObject::invokeMethod(status, "activated", Qt::DirectConnection,
                                      Q_ARG(int, done)));
    QCOMPARE(sent.last().value(QStringLiteral("type")).toString(), QStringLiteral("board_move"));
    const QString moveId = sent.last().value(QStringLiteral("id")).toString();
    QVERIFY(!moveId.isEmpty());

    view.handleEvent(QJsonObject{
            {"event", "error"}, {"id", moveId}, {"code", "tests_gate"},
            {"card", "K7Q2"}, {"status", "done"},
            {"tests", QJsonArray{QStringLiteral("ctest:gone")}},
            {"text", QStringLiteral("#K7Q2 still has 1 test(s) that do not prove it "
                                    "(ctest:gone): run or fix them, or move it with an override "
                                    "that says why.")}});
    QVERIFY(view.notice().contains(QStringLiteral("ctest:gone")));
    // The button is on the notice, and the refused move is the one it would re-send.
    QToolButton *button = nullptr;
    for (QToolButton *candidate : view.findChildren<QToolButton *>())
        if (candidate->text().startsWith(QStringLiteral("Override")))
            button = candidate;
    QVERIFY(button);
    QVERIFY(!button->isHidden());
}

QTEST_MAIN(CardTestsTests)
#include "cardtests_test.moc"
