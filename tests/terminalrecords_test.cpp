// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TerminalRecords.h"
#include <QTest>
#include <QRegularExpression>

using relay::terminalcontext::Records;
using relay::terminalcontext::Stream;

static QByteArray commandMarker(const QString &command, const QString &cwd = "/tmp", const QByteArray &token = "secret") {
    return "\x1b]777;notify;relay-command;" + token + ";" + command.toUtf8().toBase64() + ";" + cwd.toUtf8().toBase64() + "\x1b\\";
}

class TerminalRecordsTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void lifecycleAndIdentity() {
        Records records, other;
        QVERIFY(records.records().isEmpty());
        QVERIFY(records.latestUser().isEmpty());
        records.append("ignored");
        records.finish(3);
        const QString first = records.begin("ls", "/tmp", "ssh.example");
        auto running = records.latestUser();
        QVERIFY(!QUuid(first).isNull());
        QVERIFY(records.active());
        QCOMPARE(running["state"].toString(), QString("running"));
        QVERIFY(running["exit_status"].isNull());
        QVERIFY(running["ended_at"].isNull());
        QCOMPARE(running["cwd"].toString(), QString("/tmp"));
        QCOMPARE(running["host"].toString(), QString("ssh.example"));
        records.append("file\n");
        QVERIFY(records.latestUser()["revision"].toInt() > running["revision"].toInt());
        QCOMPARE(running["output"].toString(), QString()); // frozen snapshot
        records.finish(7);
        QVERIFY(!records.active());
        auto completed = records.latestUser();
        QCOMPARE(completed["exit_status"].toInt(), 7);
        QCOMPARE(completed["state"].toString(), QString("completed"));
        QVERIFY(completed["ended_at"].toDouble() >= completed["started_at"].toDouble());
        records.begin("agent", "/", "", "agent");
        QCOMPARE(records.latestUser()["command_id"].toString(), first);
        records.begin("next", "/", "");
        auto interrupted = records.records()[1].toObject();
        QCOMPARE(interrupted["state"].toString(), QString("interrupted"));
        QVERIFY(interrupted["exit_status"].isNull());
        QCOMPARE(interrupted["availability"].toString(), QString("interrupted"));
        records.resetGeneration();
        QVERIFY(!records.active());
        QVERIFY(records.latestUser().isEmpty());
        QCOMPARE(records.records().size(), 3);
        QCOMPARE(records.records().last().toObject()["command_id"].toString(), first);
        records.begin("new shell agent", "/", "", "agent");
        QVERIFY(records.latestUser().isEmpty());
        records.finish(0);
        records.begin("new shell", "/", "");
        auto current = records.latestUser();
        QCOMPARE(current["pane_id"], completed["pane_id"]);
        QVERIFY(current["generation"] != completed["generation"]);
        QVERIFY(current["sequence"].toInt() > completed["sequence"].toInt());
        other.begin("ls", "/tmp", "ssh.example");
        QVERIFY(other.latestUser()["pane_id"] != completed["pane_id"]);
        QVERIFY(other.latestUser()["command_id"] != completed["command_id"]);
    }

    void emptyAndUnavailable() {
        Records records;
        records.begin("true", "", ""); records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString());
        QCOMPARE(records.latestUser()["availability"].toString(), QString("captured"));
        records.begin("vim", "", "");
        records.append("\x1b[?1049hfull screen UI");
        records.finish(-1, "unsupported");
        QCOMPARE(records.latestUser()["output"].toString(), QString());
        QCOMPARE(records.latestUser()["availability"].toString(), QString("unsupported"));
        QVERIFY(records.latestUser()["bytes_seen"].toInt() > 0);
        records.begin("long UI", "", "");
        records.append(QByteArray(100000, 'x'));
        records.finish(-1, "unsupported");
        QCOMPARE(records.latestUser()["output"].toString(), QString());
        QCOMPARE(records.latestUser()["availability"].toString(), QString("unsupported"));
    }

    void streamingSanitizer() {
        const QByteArray input = QByteArray("echo hello\r\n\x1b[31mred\x1b[0m ") +
            QString::fromUtf8("雪🙂").toUtf8() +
            "\x1b]0;secret title\x1b\\\x1bPprivate\x1b\\\x1b(B\a\b\r\n" +
            "\x1b]133;A\x1b\\prompt and later output";
        for (int split = 0; split <= input.size(); ++split) {
            Records records;
            records.begin("echo hello", "", "");
            records.append(input.left(split)); records.append(input.mid(split));
            records.finish(0);
            QCOMPARE(records.latestUser()["output"].toString(), QString::fromUtf8("red 雪🙂\n"));
            QCOMPARE(records.latestUser()["bytes_seen"].toInt(), input.size());
        }
        Records bytewise;
        bytewise.begin("echo hello", "", "");
        for (char byte : input) bytewise.append(QByteArray(1, byte));
        bytewise.finish(0);
        QCOMPARE(bytewise.latestUser()["output"].toString(), QString::fromUtf8("red 雪🙂\n"));
    }

    void echoAndBoundaries() {
        Records records;
        records.begin("echo hello", "", "");
        records.append("echo hello world\n"); records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString("echo hello world\n"));
        records.begin("echo hello", "", "");
        records.append("echo"); records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString("echo"));
        for (const QByteArray &marker : {QByteArray("7772;end-output"), QByteArray("133;D;0")}) {
            records.begin("x", "", "");
            records.append("result\x1b]" + marker); records.append("\aPROMPT"); records.finish(0);
            QCOMPARE(records.latestUser()["output"].toString(), QString("result"));
        }
        records.begin("x", "", "");
        records.append("\x1b]0;" + QByteArray(100000, 'x'));
        records.append("\x1b"); records.append("\\ok"); records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString("ok"));
        records.begin("x", "", ""); records.append(QByteArray::fromHex("f09f")); records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString(QChar(0xfffd)));
    }

    void legacyControlsAndUnicodeTitles() {
        Records records;
        records.begin("x", "", "");
        const QByteArray input = QByteArray::fromHex("9d303b") + QString::fromUtf8("Ü hidden").toUtf8() +
            QByteArray::fromHex("9c9b33316d") + "red" + QByteArray::fromHex("9b306d") +
            "\x1b]0;" + QString::fromUtf8("Ü title").toUtf8() + "\x1b\\ok";
        for (char c : input) records.append(QByteArray(1, c));
        records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString("redok"));
        records.begin("x", "", "");
        records.append("\x1b]bad\x1b\x1b\\good"); records.finish(0);
        QCOMPARE(records.latestUser()["output"].toString(), QString("good"));
    }

    void streamSplitMarkersAndMultipleCommands() {
        const QString command = QString::fromUtf8("printf 雪");
        const QByteArray input = "idle prompt" + commandMarker(command, QString::fromUtf8("/tmp/雪")) +
            command.toUtf8() + "\n" + "\x1b]133;D;7\a\x1b]133;A\aprompt" +
            commandMarker("second") + "last\x1b]133;D;0\x1b\\";
        for (int split = 0; split <= input.size(); ++split) {
            Records records; Stream stream;
            const bool first = stream.append(records, input.left(split), "secret", "remote");
            const bool second = stream.append(records, input.mid(split), "secret", "remote");
            QVERIFY(first || second);
            QCOMPARE(records.records().size(), 2);
            auto older = records.records().last().toObject();
            QCOMPARE(older["command"].toString(), command);
            QCOMPARE(older["output"].toString(), command + "\n"); // marker means no echo
            QCOMPARE(older["exit_status"].toInt(), 7);
            QCOMPARE(older["cwd"].toString(), QString::fromUtf8("/tmp/雪"));
            QCOMPARE(older["host"].toString(), QString("remote"));
            QCOMPARE(records.latestUser()["output"].toString(), QString("last"));
            QVERIFY(!records.active());
        }
    }

    void streamAuthenticationAndComposerDedup() {
        Records records; Stream stream;
        const QString id = records.begin("same", "/composer", "remote", "agent");
        const auto before = records.records().first().toObject();
        QVERIFY(!stream.append(records, commandMarker("fake", "/", "wrong"), "secret", "remote"));
        QVERIFY(!stream.append(records, commandMarker("fake"), "", "remote"));
        QVERIFY(!stream.append(records, "\x1b]777;notify;relay-command;secret;@@;Lw==\a", "secret", "remote"));
        QCOMPARE(records.records().size(), 1);
        stream.append(records, "old prompt\x1b]133;A\aechoed command", "secret", "remote");
        QVERIFY(stream.append(records, commandMarker("same") + "same\n", "secret", "remote"));
        const auto current = records.records().first().toObject();
        QCOMPARE(current["command_id"].toString(), id);
        QCOMPARE(current["origin"].toString(), QString("agent"));
        QCOMPARE(current["started_at"], before["started_at"]);
        QCOMPARE(current["cwd"].toString(), QString("/composer"));
        QCOMPARE(current["output"].toString(), QString("same\n"));
        QVERIFY(stream.append(records, "\x1b]133;D;0\a", "secret", "remote"));
        QVERIFY(stream.append(records, commandMarker("same"), "secret", "remote"));
        QCOMPARE(records.records().size(), 2); // same completed command is a new run
        QVERIFY(records.latestUser()["command_id"].toString() != id);
    }

    void streamPromptOverflowAndClear() {
        Records records; Stream stream;
        stream.append(records, commandMarker("one"), "secret", "");
        const QByteArray input = QString::fromUtf8("雪🙂").toUtf8() + "\x1b]133;A\x1b\\prompt";
        for (char byte : input) stream.append(records, QByteArray(1, byte), "secret", "");
        QCOMPARE(records.latestUser()["output"].toString(), QString::fromUtf8("雪🙂"));
        stream.append(records, "\x1b]133;D;0\a", "secret", "");
        stream.append(records, commandMarker("two") + "\x1b]0;" + QByteArray(100000, 'x'), "secret", "");
        stream.append(records, "\x1b\\visible\x1b]133;D;0\a", "secret", "");
        QCOMPARE(records.latestUser()["output"].toString(), QString("visible"));
        stream.append(records, "\x1b]777;notify;relay-command;", "secret", "");
        stream.clear(); records.resetGeneration();
        QVERIFY(stream.append(records, commandMarker("fresh") + "fresh\n", "secret", ""));
        QCOMPARE(records.latestUser()["output"].toString(), QString("fresh\n"));
    }

    void emptyRemoteMarkerPreservesPendingComposer() {
        Records records; Stream stream;
        const QString id = records.begin("printf known", "/remote", "ssh.example", "agent");
        QVERIFY(records.awaitingOutput());
        QVERIFY(!stream.append(records, commandMarker("", "/", "wrong"), "secret", "ssh.example"));
        QVERIFY(records.awaitingOutput());
        stream.append(records, "printf known\r\n" + commandMarker("") + "known\n", "secret", "ssh.example");
        QVERIFY(!records.awaitingOutput());
        QCOMPARE(records.records().size(), 1);
        const auto current = records.records().first().toObject();
        QCOMPARE(current["command_id"].toString(), id);
        QCOMPARE(current["command"].toString(), QString("printf known"));
        QCOMPARE(current["origin"].toString(), QString("agent"));
        QCOMPARE(current["output"].toString(), QString("known\n"));
        QCOMPARE(current["availability"].toString(), QString("captured"));
        // A later empty native marker must not reuse this already-started record.
        stream.append(records, commandMarker("") + "unattributed", "secret", "ssh.example");
        QCOMPARE(records.records().size(), 2);
        QCOMPARE(records.latestUser()["availability"].toString(), QString("unsupported"));
        QCOMPARE(records.latestUser()["output"].toString(), QString());
        QVERIFY(records.latestUser()["command_id"].toString() != id);
        Records native; Stream nativeStream;
        nativeStream.append(native, commandMarker(""), "secret", "ssh.example");
        QCOMPARE(native.latestUser()["availability"].toString(), QString("unsupported"));
        native.begin("native", "/", "ssh.example", "user", false);
        QVERIFY(!native.awaitingOutput());
        nativeStream.append(native, commandMarker(""), "secret", "ssh.example");
        QCOMPARE(native.latestUser()["availability"].toString(), QString("unsupported"));
    }

    void alternateScreenInOneBatch() {
        for (const QByteArray &params : {QByteArray("47"), QByteArray("1047"), QByteArray("1049"),
                                        QByteArray("25;1049;2004"), QByteArray("47;25")}) {
            for (const QString &host : {QString(), QStringLiteral("ssh.example")}) {
                const QByteArray body = "before UI\x1b[?" + params + "hSCREEN CONTENT\x1b[?" +
                    params + "lafter UI\x1b]133;D;0\a";
                const QByteArray bytes = commandMarker("vim") + body;
                for (int split = 0; split <= bytes.size(); ++split) {
                    Records records; Stream stream;
                    stream.append(records, bytes.left(split), "secret", host);
                    stream.append(records, bytes.mid(split), "secret", host);
                    const auto result = records.latestUser();
                    QCOMPARE(result["availability"].toString(), QString("unsupported"));
                    QCOMPARE(result["output"].toString(), QString());
                    QCOMPARE(result["state"].toString(), QString("completed"));
                    QCOMPARE(result["exit_status"].toInt(), 0);
                    QCOMPARE(result["host"].toString(), host);
                }
            }
        }
        Records records;
        records.begin("test", "", "");
        records.append("before\x1b[?25hvisible\x1b[?1049lstill visible");
        QCOMPARE(records.latestUser()["availability"].toString(), QString("captured"));
        QCOMPARE(records.latestUser()["output"].toString(), QString("beforevisiblestill visible"));
        records.append(QByteArray::fromHex("9b") + "?1049hhidden");
        QCOMPARE(records.latestUser()["availability"].toString(), QString("unsupported"));
        QCOMPARE(records.latestUser()["output"].toString(), QString());
    }

    void serializedBudgetAndMetadata() {
        Records records;
        const QString huge = QString::fromUtf8("🙂").repeated(6000);
        records.begin(huge, huge, huge);
        auto item = records.latestUser();
        QVERIFY(item["command"].toString().toUtf8().size() <= 16384);
        QVERIFY(item["cwd"].toString().toUtf8().size() <= 4096);
        QVERIFY(item["host"].toString().toUtf8().size() <= 4096);
        records.append("not attributable"); records.finish(0);
        QCOMPARE(records.latestUser()["availability"].toString(), QString("unsupported"));
        QCOMPARE(records.latestUser()["output"].toString(), QString());
        for (int i = 0; i < 40; ++i) {
            records.begin("x", "", "");
            records.append(QByteArray(65536, '\t')); records.finish(0);
            QVERIFY(QJsonDocument(records.records()).toJson(QJsonDocument::Compact).size() <= 2 * 1024 * 1024);
        }
        QVERIFY(records.records().size() < 32); // JSON escapes count too.
        records.begin("x", "", ""); records.append(QByteArray::fromHex("ff")); records.finish(0);
        item = records.latestUser();
        QVERIFY(item["bytes_seen"].toDouble() >= item["output"].toString().toUtf8().size());
        Records small;
        for (int i = 0; i < 40; ++i) { small.begin(QString::number(i), "", ""); small.finish(0); }
        QCOMPARE(small.records().size(), 32);
        QCOMPARE(small.records().last().toObject()["command"].toString(), QString("8"));
    }

    void limitsAndOmissionAccounting() {
        Records records;
        const QByteArray unicode = QString::fromUtf8("雪🙂").repeated(18000).toUtf8();
        const QByteArray input = "FIRST\n" + unicode + "\nLAST";
        for (int i = 0; i < 40; ++i) {
            records.begin(QString::number(i), "", "");
            records.append(input);
            auto live = records.latestUser();
            QVERIFY(live["output"].toString().toUtf8().size() <= 65536);
            records.finish(0);
        }
        QVERIFY(records.records().size() <= 32);
        QVERIFY(records.records().size() >= 30);
        QVERIFY(records.records().last().toObject()["command"].toString().toInt() >= 8);
        qint64 retained = 0;
        for (const auto value : records.records()) {
            auto record = value.toObject();
            const QString output = record["output"].toString();
            retained += output.toUtf8().size();
            QCOMPARE(record["availability"].toString(), QString("truncated"));
            QVERIFY(output.startsWith("FIRST\n")); QVERIFY(output.endsWith("\nLAST"));
            QVERIFY(!output.contains(QChar(0xfffd)));
            const QRegularExpression marker("\\n\\[\\.\\.\\. (\\d+) bytes omitted \\.\\.\\.\\]\\n");
            const auto match = marker.match(output);
            QVERIFY(match.hasMatch());
            const qint64 kept = output.toUtf8().size() - match.captured().toUtf8().size();
            QCOMPARE(kept + match.captured(1).toLongLong(), qint64(input.size()));
        }
        QVERIFY(retained <= 2 * 1024 * 1024);
        records.begin("boundary", "", "");
        records.append(QByteArray(65536, 'a')); records.finish(0);
        QCOMPARE(records.latestUser()["availability"].toString(), QString("captured"));
        QCOMPARE(records.latestUser()["output"].toString().size(), 65536);
    }
};

QTEST_GUILESS_MAIN(TerminalRecordsTests)
#include "terminalrecords_test.moc"
