// SPDX-License-Identifier: AGPL-3.0-or-later
// The pane text journal (card #HEY7): the SGR <-> style-layer round trip against every form the
// engine's serializer writes, the writer's segments (append, seal on size / idle / close, a
// segment sealed under a live writer), and what a crash mid-append costs.
#include "TextJournal.h"
#include "core/AnsiSerializer.h"
#include "core/CellTypes.h"
#include "core/InlineImage.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace relay;
namespace tj = relay::textjournal;

namespace {

class DataHome {
public:
    DataHome() {
        m_previous = qgetenv("XDG_DATA_HOME");
        m_had = qEnvironmentVariableIsSet("XDG_DATA_HOME");
        if (m_dir.isValid()) qputenv("XDG_DATA_HOME", m_dir.path().toLocal8Bit());
    }
    ~DataHome() {
        if (m_had) qputenv("XDG_DATA_HOME", m_previous);
        else qunsetenv("XDG_DATA_HOME");
    }
    DataHome(const DataHome &) = delete;
    DataHome &operator=(const DataHome &) = delete;
    bool valid() const { return m_dir.isValid(); }

private:
    QTemporaryDir m_dir;
    QByteArray m_previous;
    bool m_had = false;
};

// A row of cells: `text`, every cell given `attrs`, `fg` and `bg`.
void put(relay::Line *line, const QString &text, uint16_t attrs = 0, uint32_t fg = 0, uint32_t bg = 0,
         uint32_t link = 0) {
    for (const QChar ch : text) {
        Cell c;
        c.ch = ch.unicode();
        c.attrs = attrs;
        c.fg = fg;
        c.bg = bg;
        c.link = link;
        line->cells.push_back(c);
    }
}

const QString kId = QStringLiteral("0f3c2a1b-aaaa-bbbb-cccc-123456789abc");

}  // namespace

class TextJournalTest : public QObject {
    Q_OBJECT

private slots:
    // Every attribute and colour form sgrParams() can write, alone and combined, comes back
    // byte for byte: the saved rows are the serializer's, so this is the round trip restore needs.
    void roundTripsEverySerializerForm() {
        const QVector<uint16_t> attrs{0, AttrBold, AttrFaint, AttrItalic, AttrUnderline, AttrDoubleUnderline,
                                      AttrCurlyUnderline, AttrBlink, AttrReverse, AttrConceal, AttrStrike,
                                      uint16_t(AttrBold | AttrItalic | AttrUnderline | AttrStrike)};
        const QVector<uint32_t> colours{CellColor::defaultColor(), CellColor::indexed(1), CellColor::indexed(196),
                                        CellColor::indexed(15), CellColor::rgb(255, 128, 0), CellColor::rgb(0, 0, 0)};
        int checked = 0;
        for (uint16_t a : attrs)
            for (uint32_t fg : colours)
                for (uint32_t bg : {CellColor::defaultColor(), CellColor::indexed(4), CellColor::rgb(10, 20, 30)}) {
                    relay::Line line;
                    put(&line, QStringLiteral("plain "));
                    put(&line, QStringLiteral("styled"), a, fg, bg);
                    put(&line, QStringLiteral(" tail"));
                    const QString ansi = lineToAnsi(line);
                    const tj::Line parsed = tj::fromAnsi(ansi);
                    QCOMPARE(parsed.text, QStringLiteral("plain styled tail"));
                    QCOMPARE(tj::toAnsi(parsed), ansi);
                    ++checked;
                }
        QCOMPARE(checked, attrs.size() * colours.size() * 3);
    }

    // A line that ends styled closes with ESC[0m; one that starts styled has no leading reset;
    // adjacent runs of different styles need no reset between them.
    void roundTripsRunShapes() {
        relay::Line a;
        put(&a, QStringLiteral("red"), 0, CellColor::indexed(1));
        put(&a, QStringLiteral("bold"), AttrBold);
        const QString ansi = lineToAnsi(a);
        const tj::Line parsed = tj::fromAnsi(ansi);
        QCOMPARE(parsed.spans.size(), 2);
        QCOMPARE(parsed.spans.at(0), (tj::Span{0, 3, QStringLiteral("38;5;1"), QString()}));
        QCOMPARE(parsed.spans.at(1), (tj::Span{3, 4, QStringLiteral("1"), QString()}));
        QCOMPARE(tj::toAnsi(parsed), ansi);

        relay::Line empty;
        QCOMPARE(tj::toAnsi(tj::fromAnsi(lineToAnsi(empty))), QString());
    }

    // Wide and astral characters: columns count code points, so a span after an emoji starts
    // where Python's str indexing says it does.
    void countsCodePoints() {
        const QString text = QStringLiteral("a") + QString::fromUcs4(U"\U0001F600") + QStringLiteral("b");
        const QString ansi = QStringLiteral("a") + QString::fromUcs4(U"\U0001F600") + QStringLiteral("\x1b[1mb\x1b[0m");
        const tj::Line parsed = tj::fromAnsi(ansi);
        QCOMPARE(parsed.text, text);
        QCOMPARE(parsed.spans.size(), 1);
        QCOMPARE(parsed.spans.at(0).col, 2);
        QCOMPARE(tj::toAnsi(parsed), ansi);
    }

    // The saved form keeps image links (lineToSavedAnsi), and they survive as link spans.
    void roundTripsSavedLinks() {
        relay::Line line;
        put(&line, QStringLiteral("xx"), 0, 0, 0, 1);
        const QString uri = QStringLiteral("relay-image:/tmp/a.png#w=2&h=1");
        const QString ansi = lineToAnsi(line, [&](uint32_t, int) { return uri; });
        const tj::Line parsed = tj::fromAnsi(ansi);
        QCOMPARE(parsed.spans.size(), 1);
        QCOMPARE(parsed.spans.at(0).link, uri);
        QCOMPARE(tj::toAnsi(parsed), ansi);
    }

    // Anything but SGR and OSC 8 is not text and does not survive: the journal is replayed.
    void dropsOtherEscapes() {
        const tj::Line parsed = tj::fromAnsi(QStringLiteral("a\x1b[2Jb\x1b]0;title\x07" "c\x1b" "7d\x07"));
        QCOMPARE(parsed.text, QStringLiteral("abcd"));
        QVERIFY(parsed.spans.isEmpty());
    }

    // Soft-wrapped rows are one logical line in the journal, with the spans moved along.
    void joinsContinuationRows() {
        DataHome home;
        QVERIFY(home.valid());
        {
            tj::Writer writer(kId);
            QVERIFY(writer.valid());
            writer.appendRow(QStringLiteral("first \x1b[1mhal\x1b[0m"), false, 0);
            writer.appendRow(QStringLiteral("\x1b[1mf\x1b[0m done"), true, 0);
            writer.appendRow(QStringLiteral("second"), false, 1);
            writer.seal();
        }
        const QVector<tj::Line> lines = tj::readLines(kId);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).text, QStringLiteral("first half done"));
        QCOMPARE(lines.at(0).spans.size(), 1);
        QCOMPARE(lines.at(0).spans.at(0), (tj::Span{6, 4, QStringLiteral("1"), QString()}));
        QCOMPARE(lines.at(1).marks, quint8(1));
        QCOMPARE(tj::lineCount(kId), 2);
    }

    // Sealed segments are zlib, a new writer seals what an earlier one left open, and every
    // segment reads alone: the second defines its styles again.
    void sealsAndContinuesAcrossWriters() {
        DataHome home;
        {
            tj::Writer writer(kId);
            writer.appendLine(tj::fromAnsi(QStringLiteral("\x1b[38;5;2mgreen\x1b[0m one")));
            QVERIFY(writer.flush());
        }   // left open: no seal() — a crash, or a pane that is still restorable
        const QString dir = tj::journalDirectory(kId);
        QVERIFY(QFile::exists(dir + QStringLiteral("/seg-000001.rtj")));
        {
            tj::Writer writer(kId);   // seals seg 1, writes seg 2
            QVERIFY(QFile::exists(dir + QStringLiteral("/seg-000001.rtj.z")));
            QVERIFY(!QFile::exists(dir + QStringLiteral("/seg-000001.rtj")));
            writer.appendLine(tj::fromAnsi(QStringLiteral("\x1b[38;5;2mgreen\x1b[0m two")));
            QVERIFY(writer.seal());
        }
        QVERIFY(QFile::exists(dir + QStringLiteral("/seg-000002.rtj.z")));
        const QVector<tj::Line> lines = tj::readLines(kId);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(tj::toAnsi(lines.at(1)), QStringLiteral("\x1b[38;5;2mgreen\x1b[0m two"));
        const QVector<QJsonObject> second = tj::readSegment(dir + QStringLiteral("/seg-000002.rtj.z"));
        QVERIFY(std::any_of(second.cbegin(), second.cend(), [](const QJsonObject &o) { return o.contains("s"); }));
        QCOMPARE(tj::lineCount(kId), 2);
    }

    // A crash mid-append leaves a torn last record: the reader skips it and keeps everything before.
    void tornLastRecordCostsOneLine() {
        DataHome home;
        {
            tj::Writer writer(kId);
            for (int i = 0; i < 5; ++i) writer.appendLine(tj::fromAnsi(QStringLiteral("line %1").arg(i)));
            QVERIFY(writer.flush());
        }
        const QString path = tj::journalDirectory(kId) + QStringLiteral("/seg-000001.rtj");
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append));
        file.write("{\"t\":\"torn li");
        file.close();
        const QVector<tj::Line> lines = tj::readLines(kId);
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines.last().text, QStringLiteral("line 4"));
    }

    // A segment rolls over at kSealBytes, and one idle for kSealIdleSeconds is sealed before the
    // next line goes in.
    void sealsOnSizeAndIdle() {
        DataHome home;
        const QString dir = [] { return tj::journalDirectory(kId); }();
        {
            tj::Writer writer(kId);
            const QString big(2000, QLatin1Char('y'));
            for (int i = 0; i < 700; ++i) {
                writer.appendLine(tj::fromAnsi(big));
                if (i % 50 == 0) QVERIFY(writer.flush());
            }
            QVERIFY(writer.flush());
            QVERIFY(QFile::exists(dir + QStringLiteral("/seg-000001.rtj.z")));

            QDateTime t = QDateTime::currentDateTimeUtc();
            writer.setClockForTest(t);
            writer.appendLine(tj::fromAnsi(QStringLiteral("before idle")));
            QVERIFY(writer.flush());
            writer.setClockForTest(t.addSecs(tj::Writer::kSealIdleSeconds + 1));
            const int sealedBefore = QDir(dir).entryList({QStringLiteral("*.rtj.z")}).size();
            writer.appendLine(tj::fromAnsi(QStringLiteral("after idle")));
            QVERIFY(writer.flush());
            QCOMPARE(QDir(dir).entryList({QStringLiteral("*.rtj.z")}).size(), sealedBefore + 1);
        }
        const QVector<tj::Line> lines = tj::readLines(kId);
        QCOMPARE(lines.size(), 702);
        QCOMPARE(lines.last().text, QStringLiteral("after idle"));
    }

    // The worker's idle pass may seal the open segment under a live writer; the writer notices
    // and goes on in a new segment that defines its styles again.
    void survivesSegmentSealedUnderIt() {
        DataHome home;
        const QString dir = tj::journalDirectory(kId);
        {
            tj::Writer writer(kId);
            writer.appendLine(tj::fromAnsi(QStringLiteral("\x1b[1mbold\x1b[0m a")));
            QVERIFY(writer.flush());
            QVERIFY(tj::sealFile(dir + QStringLiteral("/seg-000001.rtj")));
            writer.appendLine(tj::fromAnsi(QStringLiteral("\x1b[1mbold\x1b[0m b")));
            QVERIFY(writer.seal());
        }
        const QVector<tj::Line> lines = tj::readLines(kId);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(tj::toAnsi(lines.at(1)), QStringLiteral("\x1b[1mbold\x1b[0m b"));
    }

    // A clear is recorded, and a reader can start after the last one.
    void recordsClearsAndConversations() {
        DataHome home;
        {
            tj::Writer writer(kId);
            writer.appendLine(tj::fromAnsi(QStringLiteral("old")));
            writer.appendClear();
            writer.appendConversation(QStringLiteral("relay"), QStringLiteral("0123456789abcdef0123456789abcdef"),
                                      QStringLiteral("/tmp/sessions"));
            writer.appendConversation(QString(), QString(), QString());
            writer.appendLine(tj::fromAnsi(QStringLiteral("new")));
            QVERIFY(writer.seal());
        }
        QCOMPARE(tj::readLines(kId).size(), 2);
        QCOMPARE(tj::readLines(kId, true).size(), 1);
        const QVector<QJsonObject> records = tj::readRecords(kId);
        QVERIFY(std::any_of(records.cbegin(), records.cend(), [](const QJsonObject &o) {
            return o.value("c").toObject().value("src").toString() == QLatin1String("relay");
        }));
    }

    void refusesBadIds() {
        DataHome home;
        QVERIFY(tj::journalDirectory(QStringLiteral("../../etc")).isEmpty());
        tj::Writer writer(QStringLiteral("../x"));
        QVERIFY(!writer.valid());
        writer.appendLine(tj::fromAnsi(QStringLiteral("nothing")));
        QVERIFY(writer.flush());
    }
};

QTEST_GUILESS_MAIN(TextJournalTest)
#include "textjournal_test.moc"
