// SPDX-License-Identifier: AGPL-3.0-or-later
// Signals on the Switchboard (#AQ6X phase 2, the GUI half). Two halves are under test here:
//
//  * `board::SignalsState` — the fold over the worker's `signals_changed` payload (protocol §32):
//    the order the rows come in, groups before their members, the dismissed block, and every word
//    a row or the page says. It holds no widgets, so it is read straight.
//  * the pane — `BoardView` driven with fake `signals_changed` events, the way
//    tests/boardmodel_test.cpp drives the card rows: the fold row appears and folds, a signal row
//    opens its page, each action sends the message the protocol says, and a refusal lands on the
//    page's error line.
//
// Nothing here talks to a worker: the point is that the GUI half is finished and testable before
// the backend half lands.
#include "BoardPane.h"
#include "BoardSignals.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QtTest>

using relay::board::Signal;
using relay::board::SignalsState;

namespace {

QJsonObject config()
{
    const auto json = QByteArrayLiteral(R"({
      "columns": ["inbox", "discussing", "ready", "in-progress", "done"],
      "column_statuses": {"inbox": ["inbox"], "discussing": ["discussing"], "ready": ["ready"],
        "in-progress": ["in-progress"], "done": ["done"]},
      "all_statuses": ["inbox", "discussing", "ready", "in-progress", "done"],
      "tabs": [{"id": "features", "folder": "features"}, {"id": "bugs", "folder": "changes"}],
      "autonomy": "auto"
    })");
    return QJsonDocument::fromJson(json).object();
}

QJsonObject card(const QString &id, const QString &status)
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

// One signal as the worker sends it (protocol §32.1). Only what a test cares about is spelled
// out; the rest takes the shape a real record has.
QJsonObject signalJson(const QString &key, const QString &kind, const QString &state,
                       int count = 2, const QString &lastSeen = QStringLiteral("2026-09-20T12:00:00Z"))
{
    return QJsonObject{{"key", key}, {"source", key.section(QLatin1Char(':'), 0, 0)},
                       {"kind", kind}, {"state", state},
                       {"first_seen", "2026-09-20T09:00:00Z"}, {"last_seen", lastSeen},
                       {"count", count}, {"fingerprint", "AssertionError: % != %"},
                       {"regressed", false}, {"stale", false}, {"session", ""}, {"card", ""},
                       {"excerpt", QStringLiteral("FAILED %1\nAssertionError: 3 != 4").arg(key)}};
}

QJsonObject changed(const QList<QJsonObject> &open, const QList<QJsonObject> &dismissed = {},
                    int pending = 0, const QList<QJsonObject> &promoted = {})
{
    QJsonArray openArray, dismissedArray, promotedArray;
    for (const QJsonObject &item : open)
        openArray << item;
    for (const QJsonObject &item : dismissed)
        dismissedArray << item;
    for (const QJsonObject &item : promoted)
        promotedArray << item;
    return QJsonObject{{"event", "signals_changed"}, {"open", openArray},
                       {"dismissed", dismissedArray}, {"promoted", promotedArray},
                       {"pending_count", pending},
                       {"dismissed_count", int(dismissedArray.size())}};
}

// The list widget inside the pane, for the clicks and the tooltips.
QListWidget *listOf(relay::BoardView &view)
{
    const QList<QListWidget *> lists = view.findChildren<QListWidget *>();
    return lists.isEmpty() ? nullptr : lists.first();
}

// The rows as one readable line each: "> 3 signals folded", "· ctest:a", "  · ctest:b" for a
// member, "# inbox 2" for a section header.
QStringList sketch(const QList<relay::board::Row> &rows)
{
    QStringList out;
    for (const relay::board::Row &row : rows) {
        switch (row.kind) {
        case relay::board::Row::Section:
            out << QStringLiteral("# %1 %2%3").arg(row.columnId).arg(row.count)
                       .arg(row.collapsed ? QStringLiteral(" folded") : QString());
            break;
        case relay::board::Row::Card:
            out << row.cardId;
            break;
        case relay::board::Row::Fold:
            out << QStringLiteral("~ %1").arg(row.title);
            break;
        case relay::board::Row::SignalFold:
        case relay::board::Row::DismissedFold:
            out << QStringLiteral("> %1%2").arg(row.title,
                                                row.collapsed ? QStringLiteral(" folded") : QString());
            break;
        case relay::board::Row::Signal:
            out << QStringLiteral("%1· %2").arg(QString(row.indent * 2, QLatin1Char(' ')),
                                                row.signalKey);
            break;
        }
    }
    return out;
}

const QDateTime kNow = QDateTime::fromString(QStringLiteral("2026-09-20T15:00:00Z"), Qt::ISODate);

}  // namespace

class SignalsTests : public QObject {
    Q_OBJECT

private slots:
    // ---- the headless state
    void theStateOrdersGroupsBeforeTheirMembers();
    void theRowsFoldAndTheDismissedBlockIsASecondToggle();
    void theWordsOfARowAndOfAnExpiringDismissal();
    void theSignalSectionOfAPromotedCardsBody();

    // ---- the pane
    void theFoldRowAppearsFoldsAndIsHiddenWhenThereAreNoSignals();
    void aSignalRowOpensItsPageAndEachActionSendsItsMessage();
    void aRefusalLandsOnThePagesErrorLine();
    void whichSignalFoldsAreOpenRidesTheLayoutNode();
    // The `## Signal` strip on a promoted card's page is `theSignalSectionOfAPromotedCardsBody`
    // above until the strip itself lands: three other sessions are editing the card page's
    // constructor right now (#7BM4's `## Tests` strip among them), so the strip goes in on its own.
};

// The order is the research's R11: a group (one cause, one item) before the keys it stands for,
// regressed first, broken before flaky, then the one that has failed most.
void SignalsTests::theStateOrdersGroupsBeforeTheirMembers()
{
    SignalsState state;
    QVERIFY(!state.seen());
    QVERIFY(state.isEmpty());
    // An event of another kind is not ours, and does not make the pane think it has signals.
    QVERIFY(!state.take(QStringLiteral("board_changed"), QJsonObject{}));
    QVERIFY(!state.seen());

    QJsonObject group = signalJson(QStringLiteral("group:assertion"), QStringLiteral("group"),
                                   QStringLiteral("open"), 9);
    group.insert(QStringLiteral("members"),
                 QJsonArray{QStringLiteral("ctest:panelayout"), QStringLiteral("ctest:boardmodel")});
    QJsonObject flaky = signalJson(QStringLiteral("ctest:queuenav"), QStringLiteral("flaky"),
                                   QStringLiteral("open"), 7);
    QJsonObject broken = signalJson(QStringLiteral("ctest:themes"), QStringLiteral("broken"),
                                    QStringLiteral("open"), 2);
    QJsonObject regressed = signalJson(QStringLiteral("ctest:panestatus"),
                                       QStringLiteral("broken"), QStringLiteral("open"), 1);
    regressed.insert(QStringLiteral("regressed"), true);
    QVERIFY(state.take(QStringLiteral("signals_changed"),
                       changed({flaky, signalJson(QStringLiteral("ctest:panelayout"),
                                                  QStringLiteral("broken"), QStringLiteral("open"), 4),
                                broken, group, regressed,
                                signalJson(QStringLiteral("ctest:boardmodel"),
                                           QStringLiteral("broken"), QStringLiteral("open"), 5)})));
    QVERIFY(state.seen());
    QStringList keys;
    for (const Signal &signal : state.open())
        keys << signal.key;
    // Regressed first; then the group with its two members straight after it, in the group's own
    // order; then the rest, broken before flaky and by count inside that.
    QCOMPARE(keys, (QStringList{"ctest:panestatus", "group:assertion", "ctest:panelayout",
                                "ctest:boardmodel", "ctest:themes", "ctest:queuenav"}));
    QCOMPARE(state.openCount(), 6);
    QCOMPARE(state.dismissedCount(), 0);
    QVERIFY(state.signalFor(QStringLiteral("group:assertion")) != nullptr);
    QVERIFY(state.signalFor(QStringLiteral("group:assertion"))->isGroup());
    QVERIFY(!state.signalFor(QStringLiteral("ctest:themes"))->isGroup());
    QCOMPARE(state.signalFor(QStringLiteral("nope:nothing")), nullptr);

    // A `build:` signal is a group too — the tests under a broken build are not evaluated — and it
    // comes before everything, because it is why the rest are red.
    QJsonObject build = signalJson(QStringLiteral("build:relay"), QStringLiteral("build"),
                                   QStringLiteral("open"), 1);
    state.take(QStringLiteral("signals_changed"), changed({broken, build}));
    keys.clear();
    for (const Signal &signal : state.open())
        keys << signal.key;
    QCOMPARE(keys, (QStringList{"build:relay", "ctest:themes"}));

    // Clearing forgets everything, including that an event ever arrived.
    state.clear();
    QVERIFY(!state.seen());
    QVERIFY(state.isEmpty());
}

void SignalsTests::theRowsFoldAndTheDismissedBlockIsASecondToggle()
{
    SignalsState state;
    QJsonObject group = signalJson(QStringLiteral("run:ctest"), QStringLiteral("run"),
                                   QStringLiteral("open"), 12);
    group.insert(QStringLiteral("members"), QJsonArray{QStringLiteral("ctest:panelayout")});
    QJsonObject away = signalJson(QStringLiteral("ctest:voice"), QStringLiteral("flaky"),
                                  QStringLiteral("dismissed"), 3);
    away.insert(QStringLiteral("dismissed"), QJsonObject{{"reason", "environmental"},
                                                         {"until", "2026-09-27"}});
    state.take(QStringLiteral("signals_changed"),
               changed({group, signalJson(QStringLiteral("ctest:panelayout"),
                                          QStringLiteral("broken"), QStringLiteral("open"), 4)},
                       {away}, 3));
    QCOMPARE(state.pendingCount(), 3);

    // Folded: one row for the signals, one for the dismissed, and nothing else.
    QCOMPARE(sketch(state.rows(false, false)), (QStringList{"> 2 signals folded", "> 1 dismissed folded"}));
    // Open: the group, then its member indented under it. The dismissed toggle is still last.
    QCOMPARE(sketch(state.rows(true, false)),
             (QStringList{"> 2 signals", "· run:ctest", "  · ctest:panelayout",
                          "> 1 dismissed folded"}));
    // Both open: the dismissed rows come under their own toggle, indented the same way.
    QCOMPARE(sketch(state.rows(true, true)),
             (QStringList{"> 2 signals", "· run:ctest", "  · ctest:panelayout", "> 1 dismissed",
                          "  · ctest:voice"}));
    // The dismissed toggle alone, when nothing is open: no "0 signals" row is drawn.
    SignalsState only;
    only.take(QStringLiteral("signals_changed"), changed({}, {away}));
    QCOMPARE(sketch(only.rows(true, true)), (QStringList{"> 1 dismissed", "  · ctest:voice"}));
    // Nothing at all: no rows, so the block cannot say "none".
    SignalsState empty;
    empty.take(QStringLiteral("signals_changed"), changed({}));
    QVERIFY(empty.seen());
    QVERIFY(empty.rows(true, true).isEmpty());

    // A signal the worker sent in `open` but marked dismissed is taken at its word.
    SignalsState mixed;
    mixed.take(QStringLiteral("signals_changed"), changed({away}));
    QCOMPARE(mixed.openCount(), 0);
    QCOMPARE(mixed.dismissedCount(), 1);
}

void SignalsTests::theWordsOfARowAndOfAnExpiringDismissal()
{
    using namespace relay::board;
    QCOMPARE(signalsFoldTitle(1), QStringLiteral("1 signal"));
    QCOMPARE(signalsFoldTitle(5), QStringLiteral("5 signals"));
    QCOMPARE(dismissedFoldTitle(1), QStringLiteral("1 dismissed"));
    QCOMPARE(dismissedFoldTitle(3), QStringLiteral("3 dismissed"));
    QVERIFY(signalsFoldTip(true).startsWith(QStringLiteral("Failures a machine opened and will "
                                                           "close: failing tests, broken builds.")));
    QVERIFY(signalsFoldTip(true).endsWith(QStringLiteral("Enter or → to show them.")));
    QVERIFY(signalsFoldTip(false).endsWith(QStringLiteral("Enter or ← to put them away.")));
    QCOMPARE(signalKindWord(QStringLiteral("broken")), QStringLiteral("broken"));
    QCOMPARE(signalStateWord(QStringLiteral("pending")), QStringLiteral("Pending"));
    QCOMPARE(signalCountWord(4), QStringLiteral("×4"));
    QCOMPARE(signalCountWord(0), QString());
    // The four reasons, and who may write which (decision 7) — the words the form offers.
    QCOMPARE(dismissReasons(), (QStringList{"environmental", "flaky-known", "wont-fix", "expected"}));
    QCOMPARE(dismissReasonTitle(QStringLiteral("flaky-known")), QStringLiteral("Known flaky"));
    QCOMPARE(dismissReasonTitle(QStringLiteral("wont-fix")), QStringLiteral("Won't fix"));

    // The age is a thread entry's vocabulary, so one board never has two ways of saying "3 h ago".
    QCOMPARE(signalAge(QStringLiteral("2026-09-20T14:59:30Z"), kNow), QStringLiteral("just now"));
    QCOMPARE(signalAge(QStringLiteral("2026-09-20T14:40:00Z"), kNow), QStringLiteral("20 min ago"));
    QCOMPARE(signalAge(QStringLiteral("2026-09-20T12:00:00Z"), kNow), QStringLiteral("3 h ago"));
    QCOMPARE(signalAge(QString(), kNow), QString());

    Signal signal = Signal::fromJson(signalJson(QStringLiteral("ctest:panelayout"),
                                                QStringLiteral("broken"), QStringLiteral("open"), 4));
    QCOMPARE(signalRowLine(signal, kNow),
             QStringLiteral("broken · ctest:panelayout · ×4 · 3 h ago"));
    QVERIFY(signalMarks(signal, kNow).isEmpty());
    signal.regressed = true;
    signal.stale = true;
    QCOMPARE(signalMarks(signal, kNow), (QStringList{"regressed", "stale"}));
    QCOMPARE(signalRowLine(signal, kNow),
             QStringLiteral("broken · ctest:panelayout · ×4 · 3 h ago · regressed · stale"));

    // Every dismissal expires, and the row says when: the one thing in that block about to become
    // work again (R12).
    QCOMPARE(dismissalExpiry(QStringLiteral("2026-09-27"), kNow), QStringLiteral("expires in 7 days"));
    QCOMPARE(dismissalExpiry(QStringLiteral("2026-09-21"), kNow), QStringLiteral("expires tomorrow"));
    QCOMPARE(dismissalExpiry(QStringLiteral("2026-09-20"), kNow), QStringLiteral("expires today"));
    QCOMPARE(dismissalExpiry(QStringLiteral("2026-09-19"), kNow), QStringLiteral("expired"));
    QCOMPARE(dismissalExpiry(QString(), kNow), QString());
    Signal away = Signal::fromJson(signalJson(QStringLiteral("ctest:voice"),
                                              QStringLiteral("flaky"), QStringLiteral("dismissed")));
    away.dismissedReason = QStringLiteral("environmental");
    away.dismissedUntil = QStringLiteral("2026-09-27");
    QCOMPARE(signalMarks(away, kNow), (QStringList{"environmental", "expires in 7 days"}));
}

void SignalsTests::theSignalSectionOfAPromotedCardsBody()
{
    using relay::board::signalSectionOf;
    const QString body = QStringLiteral(
            "# A failing test\n\n## Issue\nit fails\n\n## Signal\n"
            "- key: `ctest:panelayout`\n- state: open\n- failures: 4\n\n## Plan\ndo the thing\n");
    QCOMPARE(signalSectionOf(body),
             QStringLiteral("- key: `ctest:panelayout`\n- state: open\n- failures: 4"));
    // The last section of a card: it ends at the end of the body.
    QCOMPARE(signalSectionOf(QStringLiteral("## Issue\nx\n\n## Signal\nkey: a\n")),
             QStringLiteral("key: a"));
    // Case does not matter, and a card with no such section has no strip.
    QCOMPARE(signalSectionOf(QStringLiteral("## SIGNAL\nkey: a\n")), QStringLiteral("key: a"));
    QCOMPARE(signalSectionOf(QStringLiteral("## Issue\nno signal here\n")), QString());
    QCOMPARE(signalSectionOf(QString()), QString());
    // A `## Signals` section is not this one, and a mention in prose is not a heading.
    QCOMPARE(signalSectionOf(QStringLiteral("## Signals\nkey: a\n")), QString());
    QCOMPARE(signalSectionOf(QStringLiteral("the ## Signal is red\n")), QString());
}

// ---------------------------------------------------------------- the pane

void SignalsTests::theFoldRowAppearsFoldsAndIsHiddenWhenThereAreNoSignals()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({card(QStringLiteral("AAA1"), QStringLiteral("inbox"))}));
    // A board whose worker never mentions signals draws no row at all: the same as a board with
    // none, which is what it is.
    QCOMPARE(relay::board::rowOfSignalFold(view.rows()), -1);

    QJsonObject group = signalJson(QStringLiteral("run:ctest"), QStringLiteral("run"),
                                   QStringLiteral("open"), 12);
    group.insert(QStringLiteral("members"), QJsonArray{QStringLiteral("ctest:panelayout")});
    QJsonObject away = signalJson(QStringLiteral("ctest:voice"), QStringLiteral("flaky"),
                                  QStringLiteral("dismissed"), 3);
    away.insert(QStringLiteral("dismissed"), QJsonObject{{"reason", "flaky-known"},
                                                         {"until", "2026-09-27"}});
    view.handleEvent(changed({group, signalJson(QStringLiteral("ctest:panelayout"),
                                                QStringLiteral("broken"), QStringLiteral("open"), 4)},
                             {away}));
    QListWidget *list = listOf(view);
    QVERIFY(list);

    // Folded by default, above the sections: the board's own rows, not a section's, so a folded
    // section (which is how every section of a new pane starts) cannot hide them.
    const int fold = relay::board::rowOfSignalFold(view.rows());
    QCOMPARE(fold, 0);
    QCOMPARE(view.rows().at(fold).title, QStringLiteral("2 signals"));
    QVERIFY(view.rows().at(fold).collapsed);
    QCOMPARE(relay::board::rowOfSignal(view.rows(), QStringLiteral("run:ctest")), -1);
    QCOMPARE(list->count(), view.rows().size());
    QVERIFY(list->item(fold)->flags().testFlag(Qt::ItemIsSelectable));
    QVERIFY(!list->item(fold)->flags().testFlag(Qt::ItemIsDragEnabled));
    QVERIFY(list->item(fold)->toolTip().startsWith(
            QStringLiteral("Failures a machine opened and will close")));
    QVERIFY(list->item(fold)->toolTip().contains(QStringLiteral("Enter or → to show them.")));

    // A click shows them, and stands the selection on the row — as #93WR's row does.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(fold)).center());
    QVERIFY(relay::board::rowOfSignal(view.rows(), QStringLiteral("run:ctest")) >= 0);
    QCOMPARE(view.selectedSignalFold(), QStringLiteral("signals"));
    QVERIFY(view.selectedCard().isEmpty());        // a signal row is not a card
    QVERIFY(view.selectedFold().isEmpty());
    // The group's member is indented under it, and the dismissed one is still away.
    const int member = relay::board::rowOfSignal(view.rows(), QStringLiteral("ctest:panelayout"));
    QVERIFY(member > relay::board::rowOfSignal(view.rows(), QStringLiteral("run:ctest")));
    QCOMPARE(view.rows().at(member).indent, 1);
    QCOMPARE(relay::board::rowOfSignal(view.rows(), QStringLiteral("ctest:voice")), -1);

    // Enter puts them away again; → shows them and steps onto the first row; ← comes back.
    QTest::keyClick(list, Qt::Key_Return);
    QCOMPARE(relay::board::rowOfSignal(view.rows(), QStringLiteral("run:ctest")), -1);
    QTest::keyClick(list, Qt::Key_Right);
    QCOMPARE(view.selectedSignal(), QStringLiteral("run:ctest"));
    QTest::keyClick(list, Qt::Key_Left);
    QCOMPARE(view.selectedSignalFold(), QStringLiteral("signals"));
    QCOMPARE(relay::board::rowOfSignal(view.rows(), QStringLiteral("run:ctest")), -1);

    // The dismissed toggle is a second row at the end of the block, with its own words.
    const int dismissed = relay::board::rowOfDismissedFold(view.rows());
    QVERIFY(dismissed > relay::board::rowOfSignalFold(view.rows()));
    QCOMPARE(view.rows().at(dismissed).title, QStringLiteral("1 dismissed"));
    QVERIFY(list->item(dismissed)->toolTip().contains(QStringLiteral("every one does")));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(dismissed)).center());
    QVERIFY(relay::board::rowOfSignal(view.rows(), QStringLiteral("ctest:voice")) >= 0);
    QCOMPARE(view.selectedSignalFold(), QStringLiteral("dismissed"));

    // An event with nothing open takes the row away; the board keeps its cards.
    view.handleEvent(changed({}));
    QCOMPARE(relay::board::rowOfSignalFold(view.rows()), -1);
    QCOMPARE(relay::board::rowOfDismissedFold(view.rows()), -1);
    QVERIFY(relay::board::rowOfSection(view.rows(), QStringLiteral("inbox")) >= 0);

    // A filter is about cards: the signal rows get out of its way, as a folded section does.
    view.handleEvent(changed({signalJson(QStringLiteral("ctest:themes"), QStringLiteral("broken"),
                                         QStringLiteral("open"))}));
    QVERIFY(relay::board::rowOfSignalFold(view.rows()) >= 0);
    QLineEdit *filter = view.findChild<QLineEdit *>(QStringLiteral("boardFilter"));
    QVERIFY(filter);
    filter->setText(QStringLiteral("AAA1"));
    QCOMPARE(relay::board::rowOfSignalFold(view.rows()), -1);
    filter->clear();
    QVERIFY(relay::board::rowOfSignalFold(view.rows()) >= 0);
}

void SignalsTests::aSignalRowOpensItsPageAndEachActionSendsItsMessage()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({card(QStringLiteral("AAA1"), QStringLiteral("inbox"))}));
    QJsonObject group = signalJson(QStringLiteral("group:assertion"), QStringLiteral("group"),
                                   QStringLiteral("open"), 9);
    group.insert(QStringLiteral("members"), QJsonArray{QStringLiteral("ctest:panelayout")});
    view.handleEvent(changed({group, signalJson(QStringLiteral("ctest:panelayout"),
                                                QStringLiteral("broken"), QStringLiteral("open"), 4)}));
    view.setOpenSignals(QJsonArray{QStringLiteral("signals")});

    relay::board::SignalDetail *page = view.signalDetail();
    QVERIFY(page);
    QVERIFY(page->isHidden());

    // Enter on a signal row opens its page — in the card detail's place, not over the list.
    view.selectSignal(QStringLiteral("ctest:panelayout"));
    QListWidget *list = listOf(view);
    QVERIFY(list);
    QTest::keyClick(list, Qt::Key_Return);
    QVERIFY(!page->isHidden());
    QCOMPARE(page->key(), QStringLiteral("ctest:panelayout"));
    QVERIFY(!view.detailOpen());          // no card is open: a signal is not a card
    // What the page says about it: the excerpt is the failure's own words, the fields its state.
    QCOMPARE(page->signal().count, 4);
    QCOMPARE(page->signal().kind, QStringLiteral("broken"));

    // Claim sends the key and this pane's token; the four messages are protocol §32.2.
    sent.clear();
    page->claim();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("signals_claim"));
    QCOMPARE(sent.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("ctest:panelayout"));
    QVERIFY(!sent.at(0).value(QStringLiteral("pane_token")).toString().isEmpty());

    sent.clear();
    page->release();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("signals_release"));
    QCOMPARE(sent.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("ctest:panelayout"));
    QVERIFY(!sent.at(0).value(QStringLiteral("reason")).toString().isEmpty());

    sent.clear();
    page->promote();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("signals_promote"));
    QCOMPARE(sent.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("ctest:panelayout"));

    // Dismiss is a form in the page, never a dialog: reason, comment, and an expiry seven days out.
    sent.clear();
    QVERIFY(!page->dismissOpen());
    page->openDismiss();
    QVERIFY(page->dismissOpen());
    QCOMPARE(page->dismissReason(), QStringLiteral("environmental"));
    QCOMPARE(page->dismissUntil(),
             QDate::currentDate().addDays(7).toString(Qt::ISODate));
    // A dismissal with no comment is refused here rather than on the worker.
    page->submitDismiss();
    QVERIFY(sent.isEmpty());
    QVERIFY(!page->error().isEmpty());
    page->setDismissReason(QStringLiteral("wont-fix"));   // the owner may pick any of the four
    page->setDismissComment(QStringLiteral("the box has no sound card"));
    page->setDismissUntil(QStringLiteral("2026-10-20"));
    page->submitDismiss();
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("signals_dismiss"));
    QCOMPARE(sent.at(0).value(QStringLiteral("key")).toString(), QStringLiteral("ctest:panelayout"));
    QCOMPARE(sent.at(0).value(QStringLiteral("reason")).toString(), QStringLiteral("wont-fix"));
    QCOMPARE(sent.at(0).value(QStringLiteral("comment")).toString(),
             QStringLiteral("the box has no sound card"));
    QCOMPARE(sent.at(0).value(QStringLiteral("until")).toString(), QStringLiteral("2026-10-20"));
    QVERIFY(page->error().isEmpty());
    // The form is away once it has been sent: a form left open invites a second dismissal.
    QVERIFY(!page->dismissOpen());

    // The worker's answer says it was written; the page stays on the signal and says so.
    view.handleEvent(QJsonObject{{"event", "signals_written"}, {"kind", "dismiss"},
                                 {"key", "ctest:panelayout"}});
    QVERIFY(!page->isHidden());

    // A `signals_changed` while the page is open redraws it from the new state.
    QJsonObject claimed = signalJson(QStringLiteral("ctest:panelayout"), QStringLiteral("broken"),
                                     QStringLiteral("open"), 5);
    claimed.insert(QStringLiteral("session"), QStringLiteral("abcdef1234"));
    claimed.insert(QStringLiteral("card"), QStringLiteral("K7Q2"));
    view.handleEvent(changed({claimed}));
    QCOMPARE(page->signal().count, 5);
    QCOMPARE(page->signal().session, QStringLiteral("abcdef1234"));
    QCOMPARE(page->signal().card, QStringLiteral("K7Q2"));

    // Escape goes back to the list, and the row is still there to stand on.
    QTest::keyClick(page, Qt::Key_Escape);
    QVERIFY(page->isHidden());
    QVERIFY(relay::board::rowOfSignal(view.rows(), QStringLiteral("ctest:panelayout")) >= 0);

    // A signal that leaves the state takes its page with it: nothing stays open on a key the
    // board no longer has.
    view.selectSignal(QStringLiteral("ctest:panelayout"));
    QTest::keyClick(list, Qt::Key_Return);
    QVERIFY(!page->isHidden());
    view.handleEvent(changed({signalJson(QStringLiteral("ctest:themes"), QStringLiteral("broken"),
                                         QStringLiteral("open"))}));
    QVERIFY(page->isHidden());
}

void SignalsTests::aRefusalLandsOnThePagesErrorLine()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    QList<QJsonObject> sent;
    view.onSend = [&sent](const QJsonObject &message) { sent << message; };
    view.handleEvent(opened({card(QStringLiteral("AAA1"), QStringLiteral("inbox"))}));
    view.handleEvent(changed({signalJson(QStringLiteral("ctest:panelayout"),
                                         QStringLiteral("broken"), QStringLiteral("open"), 4)}));
    view.setOpenSignals(QJsonArray{QStringLiteral("signals")});
    view.openSignal(QStringLiteral("ctest:panelayout"));
    relay::board::SignalDetail *page = view.signalDetail();
    QVERIFY(!page->isHidden());

    sent.clear();
    page->claim();
    QCOMPARE(sent.size(), 1);
    const QString requestId = sent.at(0).value(QStringLiteral("id")).toString();
    QVERIFY(!requestId.isEmpty());

    // Another session has it (#R9G7's refusal, reused): the page says who, on its own error line,
    // where the action was pressed — not in a status bar the pane does not show.
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", requestId},
                                 {"code", "board_claimed_elsewhere"},
                                 {"session", "9f8e7d6c"},
                                 {"text", "That signal is claimed by another session."}});
    QVERIFY(!page->error().isEmpty());
    QVERIFY(page->error().contains(QStringLiteral("9f8e7d6c")));

    // The next action clears it: a refusal never outlives the thing it refused.
    page->promote();
    QVERIFY(page->error().isEmpty());

    // An error this pane did not ask for leaves the page alone.
    view.handleEvent(QJsonObject{{"event", "error"}, {"id", "someone-else-1"},
                                 {"code", "board_claimed_elsewhere"}, {"text", "not ours"}});
    QVERIFY(page->error().isEmpty());
}

void SignalsTests::whichSignalFoldsAreOpenRidesTheLayoutNode()
{
    relay::BoardView view(QStringLiteral("/tmp/workspace"));
    view.handleEvent(opened({card(QStringLiteral("AAA1"), QStringLiteral("inbox"))}));
    QJsonObject away = signalJson(QStringLiteral("ctest:voice"), QStringLiteral("flaky"),
                                  QStringLiteral("dismissed"), 3);
    away.insert(QStringLiteral("dismissed"), QJsonObject{{"reason", "environmental"},
                                                         {"until", "2026-09-27"}});
    view.handleEvent(changed({signalJson(QStringLiteral("ctest:panelayout"),
                                         QStringLiteral("broken"), QStringLiteral("open"), 4)},
                             {away}));

    // A new pane starts folded, so the node is empty until a row is opened.
    QVERIFY(view.openSignals().isEmpty());
    view.toggleSignalFold(QStringLiteral("signals"));
    QStringList open;
    for (const QJsonValue &value : view.openSignals())
        open << value.toString();
    QCOMPARE(open, (QStringList{"signals"}));
    view.toggleSignalFold(QStringLiteral("dismissed"));
    open.clear();
    for (const QJsonValue &value : view.openSignals())
        open << value.toString();
    open.sort();
    QCOMPARE(open, (QStringList{"dismissed", "signals"}));

    // And it comes back from the node: the pane restores exactly the rows it was saved with.
    relay::BoardView back(QStringLiteral("/tmp/workspace"));
    back.setOpenSignals(QJsonArray{QStringLiteral("signals")});
    back.handleEvent(opened({card(QStringLiteral("AAA1"), QStringLiteral("inbox"))}));
    back.handleEvent(changed({signalJson(QStringLiteral("ctest:panelayout"),
                                         QStringLiteral("broken"), QStringLiteral("open"), 4)},
                             {away}));
    QVERIFY(!back.rows().at(relay::board::rowOfSignalFold(back.rows())).collapsed);
    QVERIFY(relay::board::rowOfSignal(back.rows(), QStringLiteral("ctest:panelayout")) >= 0);
    QVERIFY(back.rows().at(relay::board::rowOfDismissedFold(back.rows())).collapsed);
    // An unknown key in a saved node is ignored rather than opening something at random.
    back.setOpenSignals(QJsonArray{QStringLiteral("nonsense")});
    QVERIFY(back.rows().at(relay::board::rowOfSignalFold(back.rows())).collapsed);
}

QTEST_MAIN(SignalsTests)
#include "signals_test.moc"
