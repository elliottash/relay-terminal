// SPDX-License-Identifier: GPL-3.0-or-later
// The Claude IDE bridge's C++ rules (issue GT7X, protocol 26.5): the environment a pane's shell
// gets, the two answers' vocabulary — and the rule that decides which reply files Relay may write.
//
// That last one is here because a live end-to-end run caught it (docs/qa_evidence/
// 2026-09-19-claude-codex-guest-integration): the reply file does not exist when the check runs —
// it is what the check admits writing — and canonicalFilePath() is empty for a file that is not
// there, so every answer was refused and every openDiff hung. The rule is a pure function, so the
// test builds temp directories and asks it; the sidecar's own surface is tested in Python.
#include "GuestBridge.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace relay::guestbridge;

class GuestBridgeTest : public QObject {
    Q_OBJECT

private slots:
    void theAnswersAreUpstreamsOwnStrings() {
        QCOMPARE(fileSaved(), QStringLiteral("FILE_SAVED"));
        QCOMPARE(diffRejected(), QStringLiteral("DIFF_REJECTED"));
    }

    void bridgeEnvIsClAUDEOnlyAndOnlyForARealPort() {
        const QJsonObject env = bridgeEnv(QStringLiteral("claude"), 41234);
        QCOMPARE(env.value(QStringLiteral("CLAUDE_CODE_SSE_PORT")).toString(), QStringLiteral("41234"));
        QCOMPARE(env.value(QStringLiteral("ENABLE_IDE_INTEGRATION")).toString(), QStringLiteral("true"));
        QCOMPARE(env.size(), 2);
        // Codex has no IDE bridge (26.2) and "no bridge" is port 0: both inject nothing.
        QVERIFY(bridgeEnv(QStringLiteral("codex"), 41234).isEmpty());
        QVERIFY(bridgeEnv(QStringLiteral("claude"), 0).isEmpty());
        QVERIFY(bridgeEnv(QStringLiteral("claude"), -1).isEmpty());
        QVERIFY(bridgeEnv(QStringLiteral("claude"), 65536).isEmpty());
    }

    void everyKeyBridgeEnvCanSetIsInTheKeyList() {
        // The pane clears from this list and sets from `bridgeEnv`, because `qputenv` writes the
        // *GUI process's* environment: iterating over what `bridgeEnv` returned removed nothing
        // when it returned nothing, so a sidecar that died left its port in every later shell and
        // the claudes started in them hung dialling a closed socket. A key added to `bridgeEnv`
        // and not to `bridgeEnvKeys` would bring that back.
        const QStringList keys = bridgeEnvKeys();
        const QJsonObject env = bridgeEnv(QStringLiteral("claude"), 41234);
        QCOMPARE(keys.size(), env.size());
        for (auto it = env.constBegin(); it != env.constEnd(); ++it)
            QVERIFY2(keys.contains(it.key()), qPrintable(it.key()));
        // "The bridge is off" must be expressible as "clear every one of them".
        QVERIFY(bridgeEnv(QStringLiteral("claude"), 0).isEmpty());
    }

    void aReplyThatDoesNotExistYetIsAllowed() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString replies = root.filePath(QStringLiteral("replies"));
        QVERIFY(QDir().mkpath(replies));
        // The case the live run caught: the sidecar names a fresh uuid, nothing is there yet.
        QVERIFY(replyAllowed(replies, replies + QStringLiteral("/1c0f0b6a-reply.json")));
    }

    void nothingOutsideTheRepliesDirectoryIsWritten() {
        QTemporaryDir root, elsewhere;
        QVERIFY(root.isValid() && elsewhere.isValid());
        const QString replies = root.filePath(QStringLiteral("replies"));
        QVERIFY(QDir().mkpath(replies));
        QVERIFY(!replyAllowed(replies, elsewhere.filePath(QStringLiteral("steal.json"))));
        QVERIFY(!replyAllowed(replies, QStringLiteral("/tmp/relay-qa-bridge-escape.json")));
        // A `..` climbs back out lexically; the clean in the rule resolves it before the prefix
        // check, so this is a refusal and not a write above the directory.
        QVERIFY(!replyAllowed(replies, replies + QStringLiteral("/../escape.json")));
        QVERIFY(!replyAllowed(replies, QString()));
    }

    void aMissingRepliesDirectoryRefusesEverything() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString nowhere = root.filePath(QStringLiteral("never-made"));
        QVERIFY(!QDir(nowhere).exists());
        QVERIFY(!replyAllowed(nowhere, nowhere + QStringLiteral("/reply.json")));
    }
};

QTEST_MAIN(GuestBridgeTest)
#include "guestbridge_test.moc"
