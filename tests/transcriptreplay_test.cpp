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
    // A turn as the pane would have printed it: the prompt, the reply, one ▸ row per call.
    void aTurnLooksLikeALiveTurn() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("add a test")),
                               entry(1, QStringLiteral("tool_call"), QStringLiteral("read src/Pane.h")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("Done."))};
        const QVector<Row> rows = render(items, 400);
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("add a test"), QStringLiteral("▸ read src/Pane.h"),
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
                 (QStringList{QStringLiteral("run it"), QStringLiteral("▸ run pytest"), QStringLiteral("green")}));
    }

    // Turns are separated by the blank line a live conversation leaves, and never opened or
    // closed by one — the rules around the block are the caller's.
    void turnsAreSeparatedNotPadded() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("one")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("first")),
                               entry(2, QStringLiteral("prompt"), QStringLiteral("two")),
                               entry(2, QStringLiteral("reply"), QStringLiteral("second"))};
        QCOMPARE(textsOf(render(items, 400)),
                 (QStringList{QStringLiteral("one"), QStringLiteral("first"), QString(),
                              QStringLiteral("two"), QStringLiteral("second")}));
    }

    // A multi-line prompt keeps its lines; the live line wears no glyph, and neither does this.
    void aMultiLinePromptKeepsItsLines() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("first line\nsecond line")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("a\nb"))};
        const QVector<Row> rows = render(items, 400);
        QCOMPARE(textsOf(rows), (QStringList{QStringLiteral("first line"), QStringLiteral("second line"),
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

    // The fill a truncated saved text gets (#KDB4): `beforeTurn` drops that turn and every one
    // after it, so what is left is exactly the turns the file's window no longer holds.
    void beforeTurnDropsThatTurnAndLater() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("one")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("first")),
                               entry(2, QStringLiteral("prompt"), QStringLiteral("two")),
                               entry(2, QStringLiteral("reply"), QStringLiteral("second")),
                               entry(3, QStringLiteral("prompt"), QStringLiteral("three")),
                               entry(3, QStringLiteral("reply"), QStringLiteral("third"))};
        QCOMPARE(textsOf(render(items, 400, 2)),
                 (QStringList{QStringLiteral("one"), QStringLiteral("first")}));
        QCOMPARE(render(items, 400, 0).size(), 0);   // nothing is before the first turn
        QCOMPARE(render(items, 400, 1).size(), 0);
    }

    // The first prompt row the saved text still holds names the turn its window starts at: the row
    // is cut at the pane's width, so the prompt's first line has to start with the row, not the
    // other way round, and the row may carry the ink and the OSC 8 link of the line it was saved
    // with. Windows saved while prompts still wore the "✦ " glyph match the same way with the
    // glyph stripped.
    void coveredFromMatchesTheFirstPromptRow() {
        const QJsonArray items{entry(0, QStringLiteral("prompt"), QStringLiteral("verify image generation")),
                               entry(0, QStringLiteral("reply"), QStringLiteral("starting")),
                               entry(1, QStringLiteral("prompt"), QStringLiteral("yes, help me update the gateway")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("a longer reply than the window")),
                               entry(2, QStringLiteral("prompt"), QStringLiteral("Continue")),
                               entry(2, QStringLiteral("reply"), QStringLiteral("done"))};
        // The window starts mid-reply of turn 1, and its first whole prompt row is turn 2's.
        const QStringList saved{QStringLiteral("the implementer. Next is the desktop again"),
                                QStringLiteral("st a loc"),
                                QStringLiteral("al gateway"),
                                QStringLiteral("\x1b[38;5;15m\x1b]8;;relay://pane/1\x1b\\Continue\x1b]8;;\x1b\\\x1b[0m"),
                                QStringLiteral("done")};
        QCOMPARE(coveredFrom(saved, items), 2);
        // The same window as an older pane saved it, prompt row and all, still matches.
        const QStringList marked{QStringLiteral("\x1b[38;5;15m✦ \x1b]8;;relay://pane/1\x1b\\Continue\x1b]8;;\x1b\\\x1b[0m"),
                                 QStringLiteral("done")};
        QCOMPARE(coveredFrom(marked, items), 2);
        // A row cut at the pane's width matches the full prompt line it came from.
        QCOMPARE(coveredFrom(QStringList{QStringLiteral("yes, help me update the ga")}, items), 1);
        // A short line that merely starts like the prompt is the agent's own prose, not a turn.
        QCOMPARE(coveredFrom(QStringList{QStringLiteral("yes, help me")}, items), -1);
        // No prompt or reply anchor: the caller must draw the whole transcript, as a restored
        // pane with only a recap would otherwise show none of its earlier turns.
        QCOMPARE(coveredFrom(QStringList{QStringLiteral("only a reply's tail")}, items), -1);
        QCOMPARE(coveredFrom(saved, QJsonArray{}), -1);
    }

    void recapOnlySavedTextNeedsTheWholeTranscript() {
        const QJsonArray items{entry(0, QStringLiteral("prompt"), QStringLiteral("fix the board error")),
                               entry(0, QStringLiteral("reply"), QStringLiteral("I found the invalid links field")),
                               entry(1, QStringLiteral("prompt"), QStringLiteral("continue")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("The fix is committed"))};
        const QStringList saved{QStringLiteral("Session loaded: Fixing card links · 2 turns"),
                                QStringLiteral("Next · restart Relay")};
        const int covered = coveredFrom(saved, items);
        QCOMPARE(covered, -1);
        const auto rows = render(items, 400, covered);
        QVERIFY(textsOf(rows).contains(QStringLiteral("fix the board error")));
        QVERIFY(textsOf(rows).contains(QStringLiteral("continue")));
    }

    // A window deep enough to have lost its turn's ✦ row — measured: the window opened at a
    // turn's recap — is dated by the replies it still holds instead, and the newest turn dated
    // is the answer: over-covering a turn the window already holds is a cosmetic repeat, where
    // under-covering drops one for good. The containment is whitespace-free on both sides, so a
    // reply wrapped mid-word at the pane's width is still found — that is the wrap the real
    // window was measured through. A reply too short to be distinctive dates nothing.
    void repliesDateTheWindowWhenNoPromptRowSurvives() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("one")),
                               entry(1, QStringLiteral("reply"),
                                     QStringLiteral("The gateway config goes in /opt/relay/gateway.json")),
                               entry(2, QStringLiteral("prompt"), QStringLiteral("Continue")),
                               entry(2, QStringLiteral("reply"),
                                     QStringLiteral("Deployed to the live gateway just now")),
                               entry(3, QStringLiteral("prompt"), QStringLiteral("go on")),
                               entry(3, QStringLiteral("reply"),
                                     QStringLiteral("All three cards are filed with their evidence"))};
        const QStringList saved{QStringLiteral("Recap · 13:01 → 17:50 · 4h 48m"),
                                QStringLiteral("Next · Run the local gateway with the"),
                                QStringLiteral("fake upstream to verify it"),
                                QStringLiteral("Deployed to the live gatew"),
                                QStringLiteral("ay just now"),
                                QStringLiteral("▸ ran echo · 1 line")};
        QCOMPARE(coveredFrom(saved, items), 2);
        // A reply too short to be distinctive dates nothing.
        const QJsonArray curt{entry(5, QStringLiteral("reply"), QStringLiteral("Done."))};
        QCOMPARE(coveredFrom(saved, curt), -1);
    }

    // "Continue" opened two of the turns in the conversation this was measured on. The *last*
    // matching turn is the answer: the first would claim turn A for turn B's identical row and
    // drop every turn between them from the fill for good, where the last can only duplicate one.
    void aRepeatedPromptAnswersItsLastTurn() {
        const QJsonArray items{entry(1, QStringLiteral("prompt"), QStringLiteral("Continue")),
                               entry(1, QStringLiteral("reply"), QStringLiteral("first pass")),
                               entry(2, QStringLiteral("prompt"), QStringLiteral("Continue")),
                               entry(2, QStringLiteral("reply"), QStringLiteral("second pass"))};
        QCOMPARE(coveredFrom(QStringList{QStringLiteral("Continue")}, items), 2);
        // …and the same row as an older pane saved it, with the glyph it wore then.
        QCOMPARE(coveredFrom(QStringList{QStringLiteral("✦ Continue")}, items), 2);
    }
};

QTEST_MAIN(TranscriptReplayTest)
#include "transcriptreplay_test.moc"
