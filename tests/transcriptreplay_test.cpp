// SPDX-License-Identifier: AGPL-3.0-or-later
// The transcript fallback (card #0TJ9): a conversation with no saved terminal text is redrawn
// from the entries `conversation_get` returns, in the shape a live turn prints.
#include "TranscriptReplay.h"

#include <QtTest>

using namespace relay::transcriptreplay;

namespace {

QJsonObject entry(int turn, const QString &kind, const QString &text) {
    return {{"turn", turn}, {"kind", kind}, {"text", text}};
}

QStringList textsOf(const QVector<Row> &rows) {
    QStringList out;
    for (const Row &row : rows) out << row.text;
    return out;
}

}  // namespace

class TranscriptReplayTest : public QObject {
    Q_OBJECT

private slots:
    // A turn as the pane would have printed it: the ✦ prompt, the reply, one ▸ row per call.
    void aTurnLooksLikeALiveTurn() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("add a test")),
                               entry(1, QStringLiteral("tool_call"), QStringLiteral("read src/Pane.h")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("Done."))};
        const QVector<Row> rows = render(items, 400);
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("✦ add a test"), QStringLiteral("▸ read src/Pane.h"),
                                             QStringLiteral("Done.")}));
        QCOMPARE(rows.at(0).kind, Line::Prompt);
        QCOMPARE(rows.at(1).kind, Line::Call);
        QCOMPARE(rows.at(2).kind, Line::Reply);
    }

    // Tool output is the bulk of a transcript and the least of it; a resumed pane leaves it out.
    // So are the kinds that are not messages at all.
    void outputAndNonMessagesAreLeftOut() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("run it")),
                               entry(1, QStringLiteral("tool_call"), QStringLiteral("run pytest")),
                               entry(1, QStringLiteral("tool_output"), QStringLiteral("212 lines of pytest")),
                               entry(1, QStringLiteral("command_output"), QStringLiteral("more output")),
                               entry(1, QStringLiteral("summary"), QStringLiteral("not a message")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("green"))};
        QCOMPARE(textsOf(render(items, 400)),
                 (QStringList{QStringLiteral("✦ run it"), QStringLiteral("▸ run pytest"), QStringLiteral("green")}));
    }

    // Turns are separated by the blank line a live conversation leaves, and never opened or
    // closed by one — the rules around the block are the caller's.
    void turnsAreSeparatedNotPadded() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("one")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("first")),
                               entry(2, QStringLiteral("prompt"), QStringLiteral("two")),
                               entry(2, QStringLiteral("reply"), QStringLiteral("second"))};
        QCOMPARE(textsOf(render(items, 400)),
                 (QStringList{QStringLiteral("✦ one"), QStringLiteral("first"), QString(),
                              QStringLiteral("✦ two"), QStringLiteral("second")}));
    }

    // A multi-line prompt keeps its lines; only the first wears the ✦, as the live line does.
    void aMultiLinePromptWearsOneMarker() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("first line\nsecond line")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("a\nb"))};
        const QVector<Row> rows = render(items, 400);
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("✦ first line"), QStringLiteral("second line"),
                                             QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(rows.at(1).kind, Line::Prompt);
    }

    // A call row is one line however long the arguments were: a wrapped ▸ row would look like a
    // fold that is not there.
    void aCallRowIsOneLine() {
        const QString huge = QStringLiteral("run ") + QString(400, QLatin1Char('x'));
        const QJsonArray items{entry(1, QStringLiteral("tool_call"), huge + QStringLiteral("\nand more"))};
        const QVector<Row> rows = render(items, 400);
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.constFirst().text.size(), kCallWidth + 2);   // the "▸ " and the cut text
        QVERIFY(rows.constFirst().text.endsWith(QChar(0x2026)));
        QVERIFY(!rows.constFirst().text.contains(QLatin1Char('\n')));
    }

    // Past the budget the newest rows are kept: a resumed pane should end where the conversation
    // ended, exactly as the saved terminal text does.
    void theNewestRowsSurviveTheBudget() {
        QJsonArray items;
        for (int turn = 1; turn <= 50; ++turn) {
            items.append(entry(turn, QStringLiteral("prompt"), QStringLiteral("ask %1").arg(turn)));
            items.append(entry(turn, QStringLiteral("reply"), QStringLiteral("answer %1").arg(turn)));
        }
        const QVector<Row> rows = render(items, 5);
        QCOMPARE(rows.size(), 5);
        QCOMPARE(rows.constLast().text, QStringLiteral("answer 50"));
        QVERIFY(!rows.constFirst().text.isEmpty());   // never opens on the separator
    }

    // Guests come through the same index rows, so the same renderer draws claude and codex.
    void anEmptyConversationDrawsNothing() {
        QVERIFY(render(QJsonArray{}, 400).isEmpty());
        QVERIFY(render(QJsonArray{entry(1, QStringLiteral("reply"), QStringLiteral("   "))}, 400).isEmpty());
        QVERIFY(render(QJsonArray{entry(1, QStringLiteral("tool_output"), QStringLiteral("x"))}, 400).isEmpty());
    }
};

QTEST_MAIN(TranscriptReplayTest)
#include "transcriptreplay_test.moc"
