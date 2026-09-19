// SPDX-License-Identifier: AGPL-3.0-or-later
// The blank-line rule between transcript blocks (#5AWD): src/TranscriptGaps.h.
#include <QtTest>

#include "TranscriptGaps.h"

using relay::gaps::Block;
using relay::gaps::gapBefore;

class TranscriptGapsTest : public QObject {
    Q_OBJECT
private slots:
    void nothingBeforeTheFirstBlock() {
        QVERIFY(!gapBefore(Block::None, Block::User));
        QVERIFY(!gapBefore(Block::None, Block::Agent));
        QVERIFY(!gapBefore(Block::None, Block::Call));
        QVERIFY(!gapBefore(Block::None, Block::Header));
        QVERIFY(!gapBefore(Block::None, Block::Recap));
    }
    void aRunOfToolCallsIsSingleSpaced() {
        QVERIFY(!gapBefore(Block::Call, Block::Call));
    }
    void proseDeltasAreOneBlock() {
        QVERIFY(!gapBefore(Block::Agent, Block::Agent));
    }
    void proseAndToolCallsAreSeparated() {
        QVERIFY(gapBefore(Block::Agent, Block::Call));
        QVERIFY(gapBefore(Block::Call, Block::Agent));
    }
    void userLinesAreSeparatedFromEverything() {
        QVERIFY(gapBefore(Block::Agent, Block::User));
        QVERIFY(gapBefore(Block::Call, Block::User));
        QVERIFY(gapBefore(Block::User, Block::Agent));
        QVERIFY(gapBefore(Block::User, Block::Call));
        QVERIFY(gapBefore(Block::User, Block::Header));
    }
    void theHeaderIntroducesWhatFollowsIt() {
        // "▸ model" sits directly on top of the first prose line or the first tool row…
        QVERIFY(!gapBefore(Block::Header, Block::Agent));
        QVERIFY(!gapBefore(Block::Header, Block::Call));
        QVERIFY(!gapBefore(Block::Header, Block::User));
        // …and is itself set off from the ✦ line or the previous turn above it.
        QVERIFY(gapBefore(Block::User, Block::Header));
        QVERIFY(gapBefore(Block::Agent, Block::Header));
        QVERIFY(gapBefore(Block::Call, Block::Header));
    }
    void twoUserLinesInARowAreOneBlock() {
        // A prompt and a steer that lands right under it: the ✦ lines stay together.
        QVERIFY(!gapBefore(Block::User, Block::User));
    }
    void theTurnLinkSitsWithTheToolRowsItSums() {
        // The "✦ N tool calls" link is the run's own footer: a gap under the prose above it,
        // none over a run of rows it crowns.
        QVERIFY(gapBefore(Block::Agent, Block::Call));
        QVERIFY(!gapBefore(Block::Call, Block::Call));
    }
    void theRecapIsItsOwnBlock() {
        // "Recap · …" is set off from the prose, the tool rows or the ✦ link above it; its own
        // lines (the span header, the summary, Next ·, Open ·) stay together; and whatever
        // prints after the recap is set off from it again.
        QVERIFY(gapBefore(Block::Agent, Block::Recap));
        QVERIFY(gapBefore(Block::Call, Block::Recap));
        QVERIFY(gapBefore(Block::User, Block::Recap));
        QVERIFY(!gapBefore(Block::Recap, Block::Recap));
        QVERIFY(gapBefore(Block::Recap, Block::User));
        QVERIFY(gapBefore(Block::Recap, Block::Header));
        QVERIFY(gapBefore(Block::Recap, Block::Agent));
        QVERIFY(gapBefore(Block::Recap, Block::Call));
    }
};

QTEST_APPLESS_MAIN(TranscriptGapsTest)
#include "transcriptgaps_test.moc"
