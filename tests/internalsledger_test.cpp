// SPDX-License-Identifier: AGPL-3.0-or-later
// The reprint rules of the Activity pane (card #QT8C), proven without a window: what the
// pane took is handed back in order, grouped per turn, once, and bounded at the fifty turns the
// worker still has detail for.
#include "InternalsLedger.h"

#include <QtTest>

using relay::internals::HiddenRow;
using relay::internals::HiddenTurn;
using relay::internals::Ledger;

namespace {
HiddenRow call(const QString &anchor, const QString &title, const QString &rest = QString(), bool failed = false) {
    HiddenRow row;
    row.kind = HiddenRow::Kind::Call;
    row.anchor = anchor; row.title = title; row.rest = rest; row.failed = failed;
    return row;
}
HiddenRow thought(const QString &anchor, const QString &title) {
    HiddenRow row;
    row.kind = HiddenRow::Kind::Thinking;
    row.anchor = anchor; row.title = title;
    return row;
}
}  // namespace

class InternalsLedgerTest : public QObject {
    Q_OBJECT
private slots:
    void rowsComeBackInOrderGroupedPerTurn() {
        Ledger ledger;
        ledger.setRequest("t1", "fix the build\nand the tests");
        ledger.add("t1", thought("relay://call/p/t1/thinking", "✦ thought for 3 s"));
        ledger.add("t1", call("relay://call/p/t1/c1", "ran pytest", " · 212 lines · exit 1"));
        ledger.setRequest("t2", "now the docs");
        ledger.add("t2", call("relay://call/p/t2/c1", "edited README.md"));
        QCOMPARE(ledger.turnCount(), 2);
        QCOMPARE(ledger.rowCount(), 3);

        const QVector<HiddenTurn> turns = ledger.take();
        QCOMPARE(turns.size(), 2);
        QCOMPARE(turns.at(0).turnId, QString("t1"));
        // The whole request is kept; the caller takes its first line for the rule.
        QCOMPARE(turns.at(0).request, QString("fix the build\nand the tests"));
        QCOMPARE(turns.at(0).rows.size(), 2);
        QCOMPARE(turns.at(0).rows.at(0).kind, HiddenRow::Kind::Thinking);
        QCOMPARE(turns.at(0).rows.at(1).anchor, QString("relay://call/p/t1/c1"));
        QCOMPARE(turns.at(1).turnId, QString("t2"));
        QCOMPARE(turns.at(1).request, QString("now the docs"));
    }

    void takeHandsOverOnce() {
        Ledger ledger;
        ledger.add("t1", call("a", "ran ls"));
        QVERIFY(!ledger.isEmpty());
        QCOMPARE(ledger.take().size(), 1);
        QVERIFY(ledger.isEmpty());
        // Closing the pane again with nothing new prints nothing twice.
        QCOMPARE(ledger.take().size(), 0);
    }

    void theFirstRequestWins() {
        Ledger ledger;
        ledger.setRequest("t1", "   ");            // blank is not a request
        ledger.setRequest("t1", "first ask");
        ledger.setRequest("t1", "a nudge Relay sent later");
        ledger.add("t1", call("a", "ran ls"));
        QCOMPARE(ledger.take().at(0).request, QString("first ask"));
    }

    void aTurnWithNoRowsIsNotReprinted() {
        Ledger ledger;
        ledger.setRequest("t0", "a turn that only had an answer");
        ledger.add("t1", call("a", "ran ls"));
        const QVector<HiddenTurn> turns = ledger.take();
        QCOMPARE(turns.size(), 1);
        QCOMPARE(turns.at(0).turnId, QString("t1"));
    }

    void aRunOfReadsRewritesItsOwnRowOnly() {
        Ledger ledger;
        ledger.add("t1", call("relay://call/p/t1/c1", "read a.py", " · 40 lines"));
        // "read 2 files": the row that stood for the run is replaced, anchor included.
        ledger.add("t1", call("relay://call/p/t1/c1+2", "read 2 files", " · 90 lines"), true);
        QCOMPARE(ledger.rowCount(), 1);
        QCOMPARE(ledger.turns().at(0).rows.at(0).anchor, QString("relay://call/p/t1/c1+2"));
        // A rewrite after a reasoning row appends: the row under the cursor is not a call row.
        ledger.add("t1", thought("relay://call/p/t1/thinking-2", "✦ thought for 1 s"));
        ledger.add("t1", call("relay://call/p/t1/c3", "read 3 files"), true);
        QCOMPARE(ledger.rowCount(), 3);
        // A rewrite for a turn that is no longer the last one appends to that turn, eating nothing.
        ledger.add("t2", call("relay://call/p/t2/c1", "ran ls"));
        ledger.add("t1", call("relay://call/p/t1/c4", "read 4 files"), true);
        const QVector<HiddenTurn> turns = ledger.take();
        QCOMPARE(turns.size(), 2);
        QCOMPARE(turns.at(0).rows.size(), 4);
        QCOMPARE(turns.at(1).rows.size(), 1);
    }

    void aTurnThatComesBackKeepsItsBlock() {
        Ledger ledger;
        ledger.add("t1", call("a", "ran ls"));
        ledger.add("t2", call("b", "ran pwd"));
        ledger.add("t1", call("c", "ran late"));   // an event of t1 after t2 started
        const QVector<HiddenTurn> turns = ledger.take();
        QCOMPARE(turns.size(), 2);
        QCOMPARE(turns.at(0).rows.size(), 2);
        QCOMPARE(turns.at(0).rows.at(1).anchor, QString("c"));
    }

    void olderTurnsThanFiftyAreDroppedAndCounted() {
        Ledger ledger;
        for (int turn = 1; turn <= Ledger::kMaxTurns + 7; ++turn)
            ledger.add(QStringLiteral("t%1").arg(turn), call(QStringLiteral("a%1").arg(turn), "ran ls"));
        QCOMPARE(ledger.turnCount(), Ledger::kMaxTurns);
        QCOMPARE(ledger.droppedTurns(), 7);
        int dropped = -1;
        const QVector<HiddenTurn> turns = ledger.take(&dropped);
        QCOMPARE(dropped, 7);
        QCOMPARE(turns.size(), Ledger::kMaxTurns);
        QCOMPARE(turns.first().turnId, QString("t8"));
        QCOMPARE(turns.last().turnId, QStringLiteral("t%1").arg(Ledger::kMaxTurns + 7));
        // take() resets the count with the rows.
        QCOMPARE(ledger.droppedTurns(), 0);
    }

    void clearForgetsWithoutHandingOver() {
        Ledger ledger;
        ledger.add("t1", call("a", "ran ls"));
        ledger.clear();
        QVERIFY(ledger.isEmpty());
        QCOMPARE(ledger.take().size(), 0);
    }
};

QTEST_APPLESS_MAIN(InternalsLedgerTest)
#include "internalsledger_test.moc"
