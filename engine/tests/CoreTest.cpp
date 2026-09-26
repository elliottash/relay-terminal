// SPDX-License-Identifier: AGPL-3.0-or-later
// Screen-model tests: feed byte sequences into every available VtCore and
// assert cells, text, modes and events.
#include "core/AnsiSerializer.h"
#include "core/InlineImage.h"
#include "core/InlineMedia.h"
#include "core/LibVtermCore.h"
#include "core/SequenceScanner.h"
#include "core/VtCore.h"

#include <QtTest>

using namespace relay;

namespace {

struct Harness {
    std::unique_ptr<VtCore> vt;
    QByteArray replies;
    QStringList titles;
    QList<QPair<QString, QString>> cwds;
    QList<bool> alt;
    QList<std::tuple<PromptMark, int, int>> marks;
    int bells = 0;
    QList<QPair<QString, QByteArray>> clips;
    QList<QPair<QString, QString>> notes;

    explicit Harness(const QString &core, int rows = 5, int cols = 20)
    {
        vt = createVtCore(core, rows, cols);
        vt->resize(rows, cols, 8, 16);
        vt->events.reply = [this](const char *d, size_t n) { replies.append(d, int(n)); };
        vt->events.titleChanged = [this](const QString &t) { titles << t; };
        vt->events.cwdChanged = [this](const QString &p, const QString &h) { cwds << qMakePair(p, h); };
        vt->events.altScreenChanged = [this](bool a) { alt << a; };
        vt->events.promptMark = [this](PromptMark k, int row, int code) { marks << std::make_tuple(k, row, code); };
        vt->events.bell = [this] { ++bells; };
        vt->events.clipboardWrite = [this](const QString &t, const QByteArray &d) { clips << qMakePair(t, d); };
        vt->events.notification = [this](const QString &t, const QString &b) { notes << qMakePair(t, b); };
    }
    void feed(const QByteArray &b) { vt->feed(b.constData(), size_t(b.size())); }
    QString row(int r) const { return vt->screenText().split(QLatin1Char('\n')).value(r); }
    ViewportFrame frame()
    {
        ViewportFrame f;
        vt->updateFrame(&f, true);
        return f;
    }
};

} // namespace

class CoreTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase_data()
    {
        QTest::addColumn<QString>("core");
        for (const QString &c : availableVtCores())
            QTest::newRow(qPrintable(c)) << c;
    }

    void plainTextAndCursor()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("hello\r\nworld");
        QCOMPARE(h.row(0), QStringLiteral("hello"));
        QCOMPARE(h.row(1), QStringLiteral("world"));
        const CursorState c = h.vt->activeCursor();
        QCOMPARE(c.row, 1);
        QCOMPARE(c.col, 5);
    }

    void sgrAttributesAndColours()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.vt->setColors(0xd8d8d8, 0x1c1e24, nullptr);
        h.feed("\x1b[1;4;31mA\x1b[0m\x1b[38;2;1;2;3;48;2;4;5;6mB\x1b[0mC\x1b[7mD");
        ViewportFrame f = h.frame();
        const Line &l = f.lines[0];
        QVERIFY(l.cells[0].attrs & AttrBold);
        QVERIFY(l.cells[0].attrs & AttrUnderline);
        QCOMPARE(CellColor::kind(l.cells[0].fg), core == QLatin1String("libvterm") ? CellColor::Indexed : CellColor::Rgb);
        QCOMPARE(l.cells[1].fg, CellColor::rgb(1, 2, 3));
        QCOMPARE(l.cells[1].bg, CellColor::rgb(4, 5, 6));
        QCOMPARE(CellColor::kind(l.cells[2].fg), CellColor::Default);
        QCOMPARE(CellColor::kind(l.cells[2].bg), CellColor::Default);
        QVERIFY(l.cells[3].attrs & AttrReverse);
    }

    void ansiSerializerPreservesAttributesAndColours()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.vt->setColors(0xd8d8d8, 0x1c1e24, nullptr);
        h.feed("\x1b[1;4;31mA\x1b[0m\x1b[38;2;1;2;3;48;2;4;5;6mB\x1b[0mC\x1b[7mD");
        const QString ansi = lineToAnsi(h.frame().lines[0]);
        // A was printed bold, underlined and red. A core may keep the colour indexed (31) or
        // resolve it to RGB, so only the attributes and the presence of an SGR are asserted here.
        QVERIFY(ansi.contains(QLatin1String("\x1b[1;4;")));
        QVERIFY(ansi.contains(QLatin1String("mA")));
        // B is truecolour and must survive exactly, both foreground and background.
        QVERIFY(ansi.contains(QLatin1String("\x1b[38;2;1;2;3;48;2;4;5;6mB")));
        QVERIFY(ansi.contains(QLatin1String("\x1b[7mD")));
        QVERIFY(ansi.endsWith(QLatin1String("\x1b[0m")));
    }

    void ansiSerializerRetainsLinksOnlyForLiveReplay()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("before \x1b]8;;file:///tmp/rewound.txt\x1b\\\x1b[3mview output\x1b[0m\x1b]8;;\x1b\\ after");
        const Line line = h.frame().lines[0];
        const QString saved = lineToAnsi(line);
        QVERIFY(!saved.contains(QStringLiteral("\x1b]8;")));
        const QString live = lineToAnsi(line, [&](uint32_t id, int col) {
            return h.vt->hyperlinkUri(id, 0, col);
        });
        QVERIFY(live.contains(QStringLiteral("\x1b]8;;file:///tmp/rewound.txt\x1b\\")));
        Harness replay(core);
        replay.feed(live.toUtf8());
        QCOMPARE(replay.vt->hyperlinkAt(0, 7), QStringLiteral("file:///tmp/rewound.txt"));
        QVERIFY(replay.vt->hyperlinkAt(0, 19).isEmpty());
        QVERIFY(replay.frame().lines[0].cells[7].attrs & AttrItalic);
        QCOMPARE(replay.frame().lines[0].text(), line.text());
    }

    // The saved form (#1MGS) keeps an image row's link and drops every other one; the restore
    // filter passes it through byte for byte and still strips every other escape.
    void ansiSerializerSavesOnlyImageLinks()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        const QString uri = inlineimage::imageUri({QStringLiteral("/tmp/a b.png"), 0, 2, 4});
        h.feed("\x1b]8;;file:///tmp/x.txt\x1b\\\x1b[1mab\x1b[0m\x1b]8;;\x1b\\"
               + (QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\") + QChar(0x2800)).toUtf8()
               + "\x1b]8;;\x1b\\ cd");
        const Line line = h.frame().lines[0];
        const QString saved = lineToSavedAnsi(line, [&](uint32_t id, int col) { return h.vt->hyperlinkUri(id, 0, col); });
        QCOMPARE(saved, QStringLiteral("\x1b[1mab\x1b]8;;") + uri + QStringLiteral("\x1b\\\x1b[0m") + QChar(0x2800)
                            + QStringLiteral("\x1b]8;;\x1b\\ cd"));
        QCOMPARE(restorableAnsi(saved), saved);
        Harness replay(core);
        replay.feed(restorableAnsi(saved).toUtf8());
        QCOMPARE(replay.vt->hyperlinkAt(0, 2), uri);
        QVERIFY(replay.vt->hyperlinkAt(0, 0).isEmpty());
        QCOMPARE(replay.frame().lines[0].text(), line.text());

        // Everything else that is not SGR loses its escape, and so cannot drive the terminal.
        const QString hostile = QStringLiteral("\x1b]8;;file:///etc/passwd\x1b\\x\x1b]8;;\x1b\\"
                                               "\x1b]52;c;aGk=\x07\x1b[5Ay\x1b]8;;relay-image:0/1/1/rel.png\x1b\\z");
        const QString clean = restorableAnsi(hostile);
        QVERIFY2(!clean.contains(QLatin1Char('\x1b')) && !clean.contains(QLatin1Char('\x07')), qPrintable(clean));
        // An image link left open (a truncated file) is closed at the end of its line.
        QCOMPARE(restorableAnsi(QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\") + QChar(0x2800)),
                 QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\") + QChar(0x2800) + QStringLiteral("\x1b]8;;\x1b\\"));
    }

    void proseRunsKeepTheirLinksThroughRestore()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        // A word-wrapped block's row, as the pane prints it (#R2WQ): the run's URI around the
        // text, here with the fragment a label link rides on (#MDKN).
        const QString uri = QStringLiteral("relay://prose/p9/17#l=card%3AMTCS");
        h.feed((QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\wrapped words\x1b]8;;\x1b\\ tail")).toUtf8());
        const Line line = h.frame().lines[0];
        const QString saved = lineToSavedAnsi(line, [&](uint32_t id, int col) { return h.vt->hyperlinkUri(id, 0, col); });
        QVERIFY(saved.contains(uri));
        QCOMPARE(restorableAnsi(saved), saved);
        Harness replay(core);
        replay.feed(restorableAnsi(saved).toUtf8());
        QCOMPARE(replay.vt->hyperlinkAt(0, 2), uri);
        // A URI that only *looks* like prose's is inert but keeps its shape too: the replayed run
        // paints nothing until the pane registers a block under it.
        const QString stranger = QStringLiteral("relay://prose/zzz/999");
        const QString strangerLine = QStringLiteral("\x1b]8;;") + stranger + QStringLiteral("\x1b\\x\x1b]8;;\x1b\\");
        QCOMPARE(restorableAnsi(strangerLine), strangerLine);
    }

    void mediaRowsKeepOnlyValidatedLinksThroughRestore()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        const QString uri = inlinemedia::mediaUri({QStringLiteral("/tmp/relay media.json"), 0, 1, 60});
        QVERIFY(inlinemedia::parseMediaUri(uri));
        QVERIFY(!inlinemedia::parseMediaUri(QStringLiteral("relay-media:0/1/60/relative.json")));
        QVERIFY(!inlinemedia::parseMediaUri(QStringLiteral("relay-media:1/1/60/%2Ftmp%2Fa.json")));
        QVERIFY(!inlinemedia::parseMediaUri(QStringLiteral("relay-media:0/21/60/%2Ftmp%2Fa.json")));
        const QByteArray media = (QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\") +
                                  QChar(0x2800) + QStringLiteral("\x1b]8;;\x1b\\")).toUtf8();
        h.feed(QByteArrayLiteral("\x1b]8;;file:///tmp/no.txt\x1b\\x\x1b]8;;\x1b\\") + media);
        const QString saved = lineToSavedAnsi(h.frame().lines[0],
            [&](uint32_t id, int col) { return h.vt->hyperlinkUri(id, 0, col); });
        QVERIFY(saved.contains(uri));
        QVERIFY(!saved.contains(QStringLiteral("file:///tmp/no.txt")));
        QCOMPARE(restorableAnsi(saved), saved);
        Harness replay(core);
        replay.feed(restorableAnsi(saved).toUtf8());
        QCOMPARE(replay.vt->hyperlinkAt(0, 1), uri);
    }

    void ansiSerializerHandlesWideCharactersAndClusters()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed(QStringLiteral("a漢b éx").toUtf8());
        const QString ansi = lineToAnsi(h.frame().lines[0]);
        QCOMPARE(ansi, QStringLiteral("a漢b éx"));
    }

    void wideAndCombiningCharacters()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed(QStringLiteral("a漢b éx").toUtf8());
        ViewportFrame f = h.frame();
        const Line &l = f.lines[0];
        QCOMPARE(l.cells[1].width, uint8_t(2));
        QCOMPARE(l.cells[2].ch, kWideTail);
        QCOMPARE(l.cells[3].ch, char32_t('b'));
        QCOMPARE(l.cellText(l.cells[5]), QStringLiteral("é"));
        QCOMPARE(l.cells[6].ch, char32_t('x'));
        QCOMPARE(h.row(0), QStringLiteral("a漢b éx"));
    }

    void emojiClusters()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        // thumbs up + skin tone, then a flag (regional indicators J P), then x
        h.feed(QStringLiteral("\U0001F44D\U0001F3FD\U0001F1EF\U0001F1F5x").toUtf8());
        ViewportFrame f = h.frame();
        const Line &l = f.lines[0];
        QCOMPARE(l.cells[0].width, uint8_t(2));
        QCOMPARE(l.cellText(l.cells[0]), QStringLiteral("\U0001F44D\U0001F3FD"));
        QCOMPARE(l.cells[1].ch, kWideTail);
        QCOMPARE(l.cellText(l.cells[2]), QStringLiteral("\U0001F1EF\U0001F1F5"));
        QCOMPARE(l.cells[4].ch, char32_t('x'));
    }

    void titleBellNotification()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]2;my title\x07\x07\x1b]777;notify;Build;done\x1b\\");
        QCOMPARE(h.titles.value(0), QStringLiteral("my title"));
        QCOMPARE(h.vt->title(), QStringLiteral("my title"));
        QCOMPARE(h.bells, 1);
        QCOMPARE(h.notes.size(), 1);
        QCOMPARE(h.notes[0].first, QStringLiteral("Build"));
        QCOMPARE(h.notes[0].second, QStringLiteral("done"));
    }

    void osc7WorkingDirectory()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]7;file://myhost/tmp/some%20dir\x07");
        QCOMPARE(h.cwds.size(), 1);
        QCOMPARE(h.cwds[0].first, QStringLiteral("/tmp/some dir"));
        QCOMPARE(h.cwds[0].second, QStringLiteral("myhost"));
    }

    void osc8Hyperlinks()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("ab\x1b]8;;https://example.com/x\x1b\\link\x1b]8;;\x1b\\cd");
        QCOMPARE(h.row(0), QStringLiteral("ablinkcd"));
        QCOMPARE(h.vt->hyperlinkAt(0, 2), QStringLiteral("https://example.com/x"));
        QCOMPARE(h.vt->hyperlinkAt(0, 5), QStringLiteral("https://example.com/x"));
        QCOMPARE(h.vt->hyperlinkAt(0, 1), QString());
        QCOMPARE(h.vt->hyperlinkAt(0, 6), QString());
        ViewportFrame f = h.frame();
        QVERIFY(f.lines[0].cells[3].link != 0);
        QCOMPARE(f.lines[0].cells[7].link, uint32_t(0));
    }

    // The view's fold layer finds the row a fold hangs under by asking the core
    // for every OSC 8 run with the fold prefix, in absolute scrollback rows.
    void hyperlinkRunsInAbsoluteRows()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        auto anchor = [](const char *id, const char *text) {
            return QByteArray("\x1b]8;;relay://call/p/1/") + id + "\x1b\\" + text + "\x1b]8;;\x1b\\\r\n";
        };
        h.feed("first line\r\n");
        h.feed(anchor("a", "> ran python"));
        for (int i = 0; i < 6; ++i)
            h.feed(QByteArray("filler ") + QByteArray::number(i) + "\r\n");
        h.feed(anchor("b", "> wrote x.py"));
        h.feed("tail\r\n");

        const std::vector<VtCore::HyperlinkRun> runs = h.vt->hyperlinkRuns(QStringLiteral("relay://call/"));
        QCOMPARE(int(runs.size()), 2);
        QCOMPARE(runs[0].uri, QStringLiteral("relay://call/p/1/a"));
        QCOMPARE(runs[1].uri, QStringLiteral("relay://call/p/1/b"));
        QCOMPARE(runs[0].startRow, 1);
        QCOMPARE(runs[0].endRow, 1);
        QCOMPARE(runs[1].startRow, 8);
        QCOMPARE(runs[1].endRow, 8);
        QCOMPARE(runs[0].startCol, 0);
        QVERIFY(runs[0].endCol >= 11);
        // The rows are the ones scrollViewportToRow() uses.
        QCOMPARE(h.vt->historyRows() + h.vt->rows(), 11);
        // A prefix nothing carries, and the empty prefix, find nothing.
        QVERIFY(h.vt->hyperlinkRuns(QStringLiteral("relay://other/")).empty());
        QVERIFY(h.vt->hyperlinkRuns(QString()).empty());
        QCOMPARE(int(h.vt->hyperlinkRuns(QStringLiteral("relay://call/p/1/b")).size()), 1);
    }

    // An image's rows stay in its column wherever it starts, #1MGS. Given the column,
    // placementBytes() returns with CR LF and a cursor-forward, which holds in the last column
    // (where the cell leaves a wrap pending and a backspace would step one column left) and under
    // newline mode (LNM, where LF alone returns to column 0).
    void imageRowsKeepTheirColumn()
    {
        QFETCH_GLOBAL(QString, core);
        for (const int col : {0, 7, 19}) {
            for (const bool lnm : {false, true}) {
                Harness h(core, 8, 20);
                if (lnm)
                    h.feed("\x1b[20h");
                h.feed("\x1b[2;" + QByteArray::number(col + 1) + "H");
                h.feed(inlineimage::placementBytes(QStringLiteral("/tmp/pic.png"), QSize(1, 3), true, col));
                const std::vector<VtCore::HyperlinkRun> runs =
                    h.vt->hyperlinkRuns(QString::fromLatin1(inlineimage::kImagePrefix));
                QCOMPARE(int(runs.size()), 3);
                for (int i = 0; i < 3; ++i) {
                    QCOMPARE(runs[size_t(i)].startRow, 1 + i);
                    QCOMPARE(runs[size_t(i)].startCol, col);
                }
            }
        }
    }

    // An anchor line longer than the grid soft-wraps; the fold hangs under the
    // last row of the run, so that is the row the core reports.
    void aWrappedHyperlinkRunEndsOnItsLastRow()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        h.feed("top\r\n");
        h.feed("\x1b]8;;relay://call/p/1/a\x1b\\"
               "0123456789012345678901234567890123"
               "\x1b]8;;\x1b\\\r\n");
        h.feed("after\r\n");
        const std::vector<VtCore::HyperlinkRun> runs = h.vt->hyperlinkRuns(QStringLiteral("relay://call/"));
        QCOMPARE(int(runs.size()), 1);
        QCOMPARE(runs[0].startRow, 1);
        QCOMPARE(runs[0].endRow, 2);
    }

    // The host's row role (OSC 7772): a line the user typed, kept in the line's marks beside the
    // OSC 133 bits so the view can paint it from the live theme. Unknown roles mark nothing.
    void theRowRoleOscMarksItsLine()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]7772;agent\x1b\\* fix it\r\nreply\r\n\x1b]7772;shell\x07! make\r\n\x1b]7772;nonsense\x07x\r\n");
        const ViewportFrame f = h.frame();
        QCOMPARE(int(f.lines[0].marks), int(MarkUserAgent));
        QCOMPARE(int(f.lines[1].marks), 0);
        QCOMPARE(int(f.lines[2].marks), int(MarkUserShell));
        QCOMPARE(int(f.lines[3].marks), 0);
        QCOMPARE(h.marks.size(), 0);   // a role is not a prompt mark: no promptMark event
        // The role makes the trip to history and back: the paged history keeps
        // it, and so does a viewport scrolled up onto the row (the libvterm
        // fork widened relay_marks for this; GhosttyCore tracks the row by ref).
        Harness s(core, 4, 20);
        s.feed("\x1b]7772;agent\x1b\\* fix it\r\n");
        for (int i = 0; i < 6; ++i)
            s.feed("filler\r\n");
        const int total = s.vt->historyRows();
        QVERIFY(total >= 2);
        std::vector<Line> hist;
        s.vt->historyLines(0, total, &hist);
        QCOMPARE(int(hist.size()), total);
        QCOMPARE(int(hist[0].marks), int(MarkUserAgent));
        QCOMPARE(int(hist[1].marks), 0);
        s.vt->scrollViewportToTop();
        const ViewportFrame up = s.frame();
        QCOMPARE(int(up.lines[0].marks), int(MarkUserAgent));
    }

    void wrappedUserRolesSurviveReflow()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 6, 10);
        h.feed("\x1b]133;A\a\x1b]7772;shell\aabcdefghijklmnop\r\n");
        h.vt->resize(7, 8, 8, 16);
        auto f = h.frame();
        QVERIFY(f.lines[0].marks & MarkUserShell);
        QVERIFY(f.lines[1].marks & MarkUserShell);
        QVERIFY(f.lines[0].marks & MarkPromptStart);
        QVERIFY(!(f.lines[1].marks & MarkPromptStart));
        QVERIFY(!(f.lines[2].marks & MarkUserShell));
        for (int i = 0; i < 10; ++i) h.feed("filler\r\n");
        h.vt->resize(7, 5, 8, 16);
        std::vector<Line> hist;
        h.vt->historyLines(0, h.vt->historyRows(), &hist);
        QVERIFY(hist.size() >= 4);
        for (int i = 0; i < 4; ++i) QVERIFY(hist[i].marks & MarkUserShell);
        for (int i = 1; i < 4; ++i) QVERIFY(!(hist[i].marks & MarkPromptStart));
    }

    // A row erased in full loses its role with its text. `/new` clears the screen through the
    // shell (Ctrl-L, then `\e[H\e[2J`), and the row the band was on is where the next
    // conversation's prompt lands: without this, the new conversation opened wearing the old
    // one's cyan.
    void clearingARowInFullDropsItsRole()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]7772;shell\x1b\\! make\r\nbuilt\r\n\x1b]7772;agent\x1b\\* fix it\r\n");
        QCOMPARE(int(h.frame().lines[0].marks), int(MarkUserShell));
        QCOMPARE(int(h.frame().lines[2].marks), int(MarkUserAgent));
        h.feed("\x1b[H\x1b[2J");   // what `clear` and readline's redraw send
        h.feed("$ ");
        const ViewportFrame after = h.frame();
        for (int row = 0; row < 4; ++row)
            QCOMPARE(int(after.lines[row].marks), 0);

        // Erase to the end of the screen from a row's middle: that row still holds what the user
        // typed, so it keeps the band. The rows below it are gone and do not.
        Harness p(core);
        p.feed("\x1b]7772;shell\x1b\\! make\r\n\x1b]7772;shell\x1b\\! test\r\n");
        p.feed("\x1b[1;4H\x1b[J");
        const ViewportFrame part = p.frame();
        QCOMPARE(int(part.lines[0].marks), int(MarkUserShell));
        QCOMPARE(int(part.lines[1].marks), 0);
    }

    // The scanner's half of the same rule, which is how a core that keeps roles
    // outside the line (GhosttyCore, tracked grid refs) hears about an erase.
    // Only the selectors that can clear a row end to end are reported: `CSI K`
    // stops at the cursor and readline sends it by the hundred, `CSI 3 J` drops
    // scrollback, and `CSI ? 2 J` (DECSED) may leave protected cells standing.
    void theScannerReportsAnErasedRow()
    {
        const auto erases = [](const QByteArray &in) {
            SequenceScanner scanner;
            QStringList out;
            SequenceScanner::Hit hit;
            size_t off = 0;
            while (off < size_t(in.size()) && scanner.next(in.constData(), size_t(in.size()), off, &hit)) {
                off = hit.end;
                if (hit.kind == SequenceScanner::Hit::Erase)
                    out << QStringLiteral("%1%2").arg(hit.eraseParam).arg(QLatin1Char(hit.mark));
            }
            return out.join(QLatin1Char(' '));
        };
        QCOMPARE(erases("\x1b[H\x1b[2J"), QStringLiteral("2J"));       // `clear`, and readline's redraw
        QCOMPARE(erases("\x1b[J"), QStringLiteral("0J"));              // to the end of the screen
        QCOMPARE(erases("\x1b[1J\x1b[2K"), QStringLiteral("1J 2K"));
        QCOMPARE(erases("\x1b[K\x1b[0K\x1b[1K"), QString());           // partial: the text stays
        QCOMPARE(erases("\x1b[3J"), QString());                        // scrollback, not a row
        QCOMPARE(erases("\x1b[?2J"), QString());                       // DECSED
        // The role and the erase arrive in order, each hit ending where the
        // sequence does, so the core can act on the state each one left.
        SequenceScanner ordered;
        const QByteArray in = "\x1b]7772;shell\x1b\\! make\r\n\x1b[2J";
        SequenceScanner::Hit hit;
        size_t off = 0;
        QVERIFY(ordered.next(in.constData(), size_t(in.size()), off, &hit));
        QCOMPARE(int(hit.kind), int(SequenceScanner::Hit::RowRole));
        QCOMPARE(int(hit.role), int(MarkUserShell));
        off = hit.end;
        QVERIFY(ordered.next(in.constData(), size_t(in.size()), off, &hit));
        QCOMPARE(int(hit.kind), int(SequenceScanner::Hit::Erase));
        QCOMPARE(hit.eraseParam, 2);
        QCOMPARE(int(hit.end), in.size());
    }

    void osc133PromptMarks()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]133;A\x07$ \x1b]133;B\x07ls\r\n\x1b]133;C\x07out\r\n\x1b]133;D;3\x07\x1b]133;A\x07$ ");
        QCOMPARE(h.marks.size(), 5);
        QCOMPARE(std::get<0>(h.marks[0]), MarkPromptStart);
        QCOMPARE(std::get<1>(h.marks[0]), 0);
        QCOMPARE(std::get<0>(h.marks[1]), MarkCommandStart);
        QCOMPARE(std::get<0>(h.marks[2]), MarkOutputStart);
        QCOMPARE(std::get<1>(h.marks[2]), 1);
        QCOMPARE(std::get<0>(h.marks[3]), MarkCommandFinished);
        QCOMPARE(std::get<2>(h.marks[3]), 3);
        QCOMPARE(std::get<1>(h.marks[4]), 2);
        // Split across feed() calls.
        Harness s(core);
        s.feed("\x1b]13");
        s.feed("3;D;");
        s.feed("42\x1b");
        s.feed("\\");
        QCOMPARE(s.marks.size(), 1);
        QCOMPARE(std::get<2>(s.marks[0]), 42);
        ViewportFrame f = h.frame();
        QVERIFY(f.lines[0].marks & MarkPromptStart);
    }

    void promptJump()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 20);
        h.feed("\x1b]133;A\x07$ one\r\n");
        for (int i = 0; i < 10; ++i)
            h.feed("output\r\n");
        h.feed("\x1b]133;A\x07$ two\r\n");
        for (int i = 0; i < 10; ++i)
            h.feed("more\r\n");
        QVERIFY(h.vt->scrollToPrompt(-1));
        ViewportFrame f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("$ two"));
        QVERIFY(h.vt->scrollToPrompt(-1));
        f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("$ one"));
    }

    void altScreen()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("shell");
        h.feed("\x1b[?1049h\x1b[Hfull screen");
        QVERIFY(h.vt->altScreen());
        QCOMPARE(h.alt, QList<bool>{true});
        QCOMPARE(h.row(0), QStringLiteral("full screen"));
        h.feed("\x1b[?1049l");
        QVERIFY(!h.vt->altScreen());
        QCOMPARE(h.alt, (QList<bool>{true, false}));
        QCOMPARE(h.row(0), QStringLiteral("shell"));
        // Enter and leave within one chunk still reports both transitions.
        Harness quick(core);
        quick.feed("\x1b[?1049hfull\x1b[?1049l");
        QCOMPARE(quick.alt, (QList<bool>{true, false}));
    }

    void scrollbackAndViewport()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 3, 10);
        for (int i = 1; i <= 20; ++i)
            h.feed(QByteArray::number(i) + (i < 20 ? "\r\n" : ""));
        QCOMPARE(h.vt->historyRows(), 17);
        QCOMPARE(h.vt->historyText(3), (QStringList{QStringLiteral("15"), QStringLiteral("16"), QStringLiteral("17")}));
        QCOMPARE(h.row(0), QStringLiteral("18"));
        QVERIFY(h.vt->viewportAtBottom());
        h.vt->scrollViewport(-5);
        QVERIFY(!h.vt->viewportAtBottom());
        ViewportFrame f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("13"));
        QCOMPARE(f.viewportTop, 12);
        QCOMPARE(f.historyRows, 17);
        // New output keeps the scrolled viewport on the same content.
        h.feed("\r\n21\r\n22");
        f = h.frame();
        QCOMPARE(f.lines[0].text(), QStringLiteral("13"));
        h.vt->scrollViewportToBottom();
        f = h.frame();
        QCOMPARE(f.lines[2].text(), QStringLiteral("22"));
        h.vt->clearScrollback();
        QCOMPARE(h.vt->historyRows(), 0);
    }

    // The pane's text journal (card #HEY7): a row that leaves the ring for good — overwritten at
    // the limit, cut by a smaller limit, cleared — is handed over once, in order, with its style
    // and its soft-wrap flag; nothing is collected while collection is off.
    void evictedRowsAreHandedOverOnceInOrder()
    {
        QFETCH_GLOBAL(QString, core);
        if (core != QLatin1String("libvterm"))
            QSKIP("only libvterm reports evicted rows; the journal then gets the tail at prune");
        Harness h(core, 3, 10);
        h.vt->setScrollbackLines(5);
        std::vector<Line> lines;
        std::vector<int> clears;
        for (int i = 1; i <= 12; ++i)
            h.feed(QByteArray::number(i) + "\r\n");
        h.vt->takeEvicted(&lines, &clears);
        QVERIFY(lines.empty());   // not collecting: nothing kept

        h.vt->setCollectEvicted(true);
        const quint64 before = h.vt->changeCount();
        // "1".."12" and a newline on a 3-row screen: 11, 12 and the cursor row are on screen, so
        // 1..10 were pushed and the ring (5) holds 6..10. Three more rows push out 6, 7 and 8.
        h.feed("\x1b[1mbold\x1b[0m\r\n");
        h.feed("13\r\n14\r\n");
        QVERIFY(h.vt->changeCount() != before);
        h.vt->takeEvicted(&lines, &clears);
        QStringList texts;
        for (const Line &l : lines)
            texts << l.text();
        QCOMPARE(texts, (QStringList{QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8")}));
        QVERIFY(clears.empty());
        h.vt->takeEvicted(&lines, &clears);
        QVERIFY(lines.empty());   // handed over once

        // A wrapped line leaves as rows flagged as continuations; its style goes with it.
        h.feed("abcdefghijKLM\r\n");
        for (int i = 0; i < 8; ++i)
            h.feed("x\r\n");
        h.vt->takeEvicted(&lines, &clears);
        QStringList saved;
        QList<bool> cont;
        for (const Line &l : lines) {
            saved << lineToSavedAnsi(l, {});
            cont << l.continuation;
        }
        const int wrapped = int(saved.indexOf(QStringLiteral("abcdefghij")));
        QVERIFY(wrapped >= 0);
        QCOMPARE(saved.value(wrapped + 1), QStringLiteral("KLM"));
        QVERIFY(cont.value(wrapped + 1));
        QVERIFY(saved.contains(QStringLiteral("\x1b[1mbold\x1b[0m")));

        // A clear hands over the whole ring and says where it happened.
        const int held = h.vt->historyRows();
        QVERIFY(held > 0);
        h.vt->clearScrollback();
        h.vt->takeEvicted(&lines, &clears);
        QCOMPARE(int(lines.size()), held);
        QCOMPARE(clears, (std::vector<int>{held}));
        // CSI 3 J is the same clear, from the program.
        h.feed("y\r\ny\r\ny\r\ny\r\n");
        h.feed("\x1b[3J");
        h.vt->takeEvicted(&lines, &clears);
        QCOMPARE(clears.size(), size_t(1));
        QCOMPARE(clears.front(), int(lines.size()));

        // A smaller limit cuts the oldest rows; evictAll takes the rest without a clear mark.
        for (int i = 0; i < 9; ++i)
            h.feed("z\r\n");
        h.vt->takeEvicted(&lines, &clears);
        h.vt->setScrollbackLines(2);
        h.vt->takeEvicted(&lines, &clears);
        QCOMPARE(int(lines.size()), 3);
        QCOMPARE(h.vt->historyRows(), 2);
        h.vt->evictAll();
        h.vt->takeEvicted(&lines, &clears);
        QCOMPARE(int(lines.size()), 2);
        QVERIFY(clears.empty());
        QCOMPARE(h.vt->historyRows(), 0);
    }

    // Scrollback reaches a phone styled (docs/REMOTE-PROTOCOL.md section 6.5):
    // historyLines() hands back the same Line the viewport frame carries, so
    // one serializer does the live screen and history alike.
    void indexedHistorySurvivesPaletteChangesAndResize()
    {
        QFETCH_GLOBAL(QString, core);
        if (core != QLatin1String("libvterm")) QSKIP("libvterm history storage regression");
        Harness h(core, 3, 20);
        h.feed("\x1b[31;44mindexed\x1b[0m\r\nsecond\r\nthird\r\n");
        std::vector<Line> history;
        h.vt->historyLines(0, 1, &history);
        QCOMPARE(history.size(), size_t(1));
        QCOMPARE(history[0].cells[0].fg, CellColor::indexed(1));
        QCOMPARE(history[0].cells[0].bg, CellColor::indexed(4));
        Harness restored(core, 3, 20);
        restored.feed(lineToAnsi(history[0]).toUtf8());
        QCOMPARE(restored.frame().lines[0].cells[0].fg, CellColor::indexed(1));
        QCOMPARE(restored.frame().lines[0].cells[0].bg, CellColor::indexed(4));
        h.vt->setColors(0x101010, 0xf0f0f0, nullptr);
        h.vt->resize(5, 20, 8, 16); // pop history back into the screen
        const auto frame = h.frame();
        bool found = false;
        for (const auto &line : frame.lines) if (line.text() == QLatin1String("indexed")) {
            QCOMPARE(line.cells[0].fg, CellColor::indexed(1));
            QCOMPARE(line.cells[0].bg, CellColor::indexed(4));
            found = true;
        }
        QVERIFY(found);
    }

    void styledHistoryLines()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 20);
        h.vt->setColors(0xd8d8d8, 0x1c1e24, nullptr);
        h.feed("\x1b[1;4;38;2;10;20;30mbold\x1b[0m plain\r\n");
        h.feed("\x1b[48;2;4;5;6mBG\x1b[0m ok\r\n");
        h.feed(QStringLiteral("wide 漢 end\r\n").toUtf8());
        h.feed("0123456789abcdefghijklmno\r\n"); // 25 columns over a 20-column grid: soft-wraps
        for (int i = 0; i < 6; ++i)
            h.feed("filler\r\n");

        const int total = h.vt->historyRows();
        QVERIFY2(total >= 8, qPrintable(QString::number(total)));
        std::vector<Line> lines;
        QCOMPARE(h.vt->historyLines(0, total, &lines), 0);
        QCOMPARE(int(lines.size()), total);

        QStringList texts;
        for (const Line &line : lines)
            texts << line.text();
        const auto rowOf = [&texts](const QString &prefix) {
            for (int i = 0; i < texts.size(); ++i)
                if (texts[i].startsWith(prefix))
                    return i;
            return -1;
        };
        // The same rows, the same text, as the plain-text reader sees them.
        QCOMPARE(h.vt->historyText(total), texts);

        const int styled = rowOf(QStringLiteral("bold"));
        QVERIFY2(styled >= 0, qPrintable(texts.join(QLatin1Char('|'))));
        const Line &bold = lines[size_t(styled)];
        QCOMPARE(bold.text(), QStringLiteral("bold plain"));
        QVERIFY(bold.cells[0].attrs & AttrBold);
        QVERIFY(bold.cells[0].attrs & AttrUnderline);
        QCOMPARE(bold.cells[0].fg, CellColor::rgb(10, 20, 30));
        QCOMPARE(CellColor::kind(bold.cells[5].fg), CellColor::Default); // after the reset
        QVERIFY(!(bold.cells[5].attrs & AttrBold));

        const int background = rowOf(QStringLiteral("BG"));
        QVERIFY(background >= 0);
        QCOMPARE(lines[size_t(background)].cells[0].bg, CellColor::rgb(4, 5, 6));
        QCOMPARE(CellColor::kind(lines[size_t(background)].cells[4].bg), CellColor::Default);

        // A double-width glyph keeps its width and its tail cell, so the
        // serializer can drop the tail and still line the row up.
        const int wide = rowOf(QStringLiteral("wide"));
        QVERIFY(wide >= 0);
        const Line &w = lines[size_t(wide)];
        QCOMPARE(w.cells[5].width, uint8_t(2));
        QCOMPARE(w.cellText(w.cells[5]), QStringLiteral("漢"));
        QCOMPARE(w.cells[6].ch, kWideTail);

        // A soft-wrapped line is two rows, the second flagged as a continuation.
        const int wrapped = rowOf(QStringLiteral("0123456789abcdefghij"));
        QVERIFY(wrapped >= 0);
        QVERIFY(wrapped + 1 < int(lines.size()));
        QCOMPARE(lines[size_t(wrapped + 1)].text(), QStringLiteral("klmno"));
        QVERIFY(!lines[size_t(wrapped)].continuation);
        QVERIFY(lines[size_t(wrapped + 1)].continuation);

        // Trailing blanks are trimmed, so a row is usually shorter than the grid.
        QVERIFY(int(lines[size_t(wrapped + 1)].cells.size()) < h.vt->columns());
        // Viewport decorations belong to a frame, never to a history page.
        for (const Line &line : lines) {
            QCOMPARE(line.selectionStart, int16_t(-1));
            QCOMPARE(line.selectionEnd, int16_t(-1));
            QVERIFY(line.highlights.empty());
        }
    }

    // A phone paging history must not drag the desktop user's own screen: the
    // call clamps, and moves nothing.
    void historyLinesClampAndTouchNothing()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 3, 10);
        for (int i = 1; i <= 20; ++i)
            h.feed(QByteArray::number(i) + "\r\n");
        const int total = h.vt->historyRows();
        QVERIFY(total >= 10);

        std::vector<Line> lines;
        // A page from the middle.
        QCOMPARE(h.vt->historyLines(2, 3, &lines), 2);
        QCOMPARE(int(lines.size()), 3);
        QCOMPARE(lines[0].text(), QStringLiteral("3"));
        QCOMPARE(lines[2].text(), QStringLiteral("5"));
        // Before the oldest line: clamped to 0, and the count with it.
        QCOMPARE(h.vt->historyLines(-5, 4, &lines), 0);
        QCOMPARE(int(lines.size()), 4);
        QCOMPARE(lines[0].text(), QStringLiteral("1"));
        // Past the newest scrollback line: what exists, then nothing.
        QCOMPARE(h.vt->historyLines(total - 1, 50, &lines), total - 1);
        QCOMPARE(int(lines.size()), 1);
        QCOMPARE(h.vt->historyLines(total, 10, &lines), total);
        QVERIFY(lines.empty());
        QCOMPARE(h.vt->historyLines(total + 100, 10, &lines), total);
        QVERIFY(lines.empty());
        QCOMPARE(h.vt->historyLines(0, 0, &lines), 0);
        QVERIFY(lines.empty());

        // Scrolled back, the viewport stays exactly where the user left it and
        // the scrollback is neither grown nor trimmed by reading it.
        h.vt->scrollViewport(-4);
        const int top = h.vt->viewportTop();
        ViewportFrame before = h.frame();
        QVERIFY(!h.vt->updateFrame(&before, false)); // settled: nothing left to report
        QCOMPARE(h.vt->historyLines(0, total, &lines), 0);
        QCOMPARE(h.vt->viewportTop(), top);
        QCOMPARE(h.vt->historyRows(), total);
        QVERIFY(!h.vt->viewportAtBottom());
        QVERIFY2(!h.vt->updateFrame(&before, false), "historyLines() must not dirty the frame");
        const ViewportFrame after = h.frame();
        QCOMPARE(after.viewportTop, top);
        QCOMPARE(after.lines[0].text(), before.lines[0].text());
        QVERIFY(!h.vt->hasSelection());
    }

    void resizePreservesHistoryTop()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 6, 10);
        h.feed("0123456789abcdefghijKLMNOPQRST\r\n");
        for (int i = 0; i < 30; ++i)
            h.feed(QByteArray("line ") + QByteArray::number(i) + "\r\n");
        h.vt->scrollViewportToRow(1);
        QCOMPARE(h.frame().lines[0].text(), QStringLiteral("abcdefghij"));
        h.vt->resize(9, 10, 8, 16);
        QCOMPARE(h.frame().lines[0].text(), QStringLiteral("abcdefghij"));
        h.vt->resize(4, 5, 8, 16);
        QCOMPARE(h.frame().lines[0].text(), QStringLiteral("abcde"));
        h.vt->resize(8, 20, 8, 16);
        QCOMPARE(h.frame().lines[0].text(), QStringLiteral("0123456789abcdefghij"));
        QVERIFY(!h.vt->viewportAtBottom());
        h.vt->scrollViewportToBottom();
        h.vt->resize(5, 8, 8, 16);
        QVERIFY(h.vt->viewportAtBottom());
    }

    void resizeKeepsBlankWrappedTop()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.feed("          abcdefghij\r\n");
        for (int i = 0; i < 10; ++i)
            h.feed("tail\r\n");
        h.vt->scrollViewportToTop();
        h.vt->resize(4, 5, 8, 16);
        QCOMPARE(h.vt->viewportTop(), 0);
        QVERIFY(h.frame().lines[0].text().trimmed().isEmpty());
        h.vt->scrollViewportToRow(2);
        QCOMPARE(h.frame().lines[0].text(), QStringLiteral("abcde"));
    }

    void resizeClampsTrimmedHistoryTop()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.vt->setScrollbackLines(12);
        for (int i = 0; i < 30; ++i)
            h.feed("0123456789\r\n");
        h.vt->scrollViewportToTop();
        h.vt->resize(4, 2, 8, 16);
        QCOMPARE(h.vt->viewportTop(), 0);
        QVERIFY(!h.vt->viewportAtBottom());
        QVERIFY(!h.frame().lines.empty());
    }

    void reflowOnResize()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.feed("0123456789abcdefghij\r\n");      // wraps into two rows
        for (int i = 0; i < 6; ++i)
            h.feed("line\r\n");                  // push the wrapped line into scrollback
        h.feed("end");
        h.vt->resize(4, 20, 8, 16);
        QStringList all = h.vt->historyText(100);
        all += h.vt->screenText().split(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("0123456789abcdefghij")), qPrintable(all.join(QLatin1Char('|'))));
        h.vt->resize(4, 5, 8, 16);
        all = h.vt->historyText(100);
        all += h.vt->screenText().split(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("01234")) && all.contains(QStringLiteral("fghij")), qPrintable(all.join(QLatin1Char('|'))));
    }

    // A rows-only resize (the reasoning panel opening under the terminal) must leave the cursor
    // where it was when the row above it is exactly full. libvterm's reflow moved it to that
    // row's last column, so the next text erased that column and overwrote the line below.
    void rowsOnlyResizeKeepsCursorBelowAFullRow()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 8, 10);
        h.feed("prompt\r\n0123456789\r\nabcd");
        h.vt->resize(5, 10, 8, 16);
        h.vt->resize(8, 10, 8, 16);
        h.feed("ef\r\nnext");
        QCOMPARE(h.row(1), QStringLiteral("0123456789"));
        QCOMPARE(h.row(2), QStringLiteral("abcdef"));
        QCOMPARE(h.row(3), QStringLiteral("next"));
    }

    void selectionText()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 20);
        h.feed("hello world foo\r\nsecond line");
        h.vt->selectionBegin(0, 6, SelectionUnit::Cell, false);
        h.vt->selectionExtend(1, 5);
        QVERIFY(h.vt->hasSelection());
        QCOMPARE(h.vt->selectedText(), QStringLiteral("world foo\nsecond"));
        ViewportFrame f = h.frame();
        QCOMPARE(int(f.lines[0].selectionStart), 6);
        QCOMPARE(int(f.lines[1].selectionEnd), 5);
        h.vt->selectionBegin(0, 7, SelectionUnit::Word, false);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("world"));
        // Double-click drag extends by whole words in both directions.
        h.vt->selectionExtend(0, 13);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("world foo"));
        h.vt->selectionExtend(0, 1);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("hello world"));
        h.vt->selectionBegin(1, 2, SelectionUnit::Line, false);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("second line"));
        h.vt->selectionClear();
        QVERIFY(!h.vt->hasSelection());
    }

    // #8SBD: copying across a soft wrap dropped the space the line wrapped at, so "the quick "
    // and "brown" arrived in the clipboard as "quickbrown". The join is character-exact: the row
    // the next row wraps out of is copied without the trailing-space trim, the last row of the
    // selection still gets it, and a hard break is still a newline.
    void selectionKeepsThePrintedSpaceAtASoftWrap()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.feed("the quick brown");   // wraps after the space in the last column
        h.feed("\r\nnext");          // and then a real line break
        ViewportFrame f = h.frame();
        QVERIFY(!f.lines[0].continuation);
        QVERIFY2(f.lines[1].continuation, "premise: row 1 is the soft wrap of row 0");
        QVERIFY(!f.lines[2].continuation);

        h.vt->selectionBegin(0, 0, SelectionUnit::Cell, false);
        h.vt->selectionExtend(1, 4);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("the quick brown"));
        // Past the end of the wrapped row: the last row of a selection is still trimmed, so the
        // copy does not carry the grid's padding.
        h.vt->selectionExtend(1, 9);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("the quick brown"));
        // A hard break stays a newline, and the row before it is trimmed as before.
        h.vt->selectionExtend(2, 3);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("the quick brown\nnext"));
        // Starting inside the first row keeps the space just the same.
        h.vt->selectionBegin(0, 4, SelectionUnit::Cell, false);
        h.vt->selectionExtend(1, 4);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("quick brown"));
    }

    // #8SBD, the other half of the rule: when the wrap falls mid-token nothing is inserted, so a
    // URL or path too long for the row still copies whole.
    void selectionAcrossAMidTokenWrapJoinsWithNothing()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.feed("https://example.com/a"); // "https://ex" / "ample.com/" / "a"
        ViewportFrame f = h.frame();
        QVERIFY2(f.lines[1].continuation && f.lines[2].continuation,
                 "premise: the URL wraps twice, mid-token both times");

        h.vt->selectionBegin(0, 0, SelectionUnit::Cell, false);
        h.vt->selectionExtend(2, 0);
        QCOMPARE(h.vt->selectedText(), QStringLiteral("https://example.com/a"));
    }

    void searchScrollback()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 3, 20);
        for (int i = 0; i < 30; ++i)
            h.feed(i % 10 == 0 ? "needle here\r\n" : "hay\r\n");
        QCOMPARE(h.vt->searchSet(QStringLiteral("needle")), 3);
        const int idx = h.vt->searchStep(true);
        QVERIFY(idx >= 0);
        ViewportFrame f = h.frame();
        bool highlighted = false;
        for (const Line &l : f.lines)
            highlighted |= !l.highlights.empty();
        QVERIFY(highlighted);
        QCOMPARE(h.vt->searchSet(QString()), 0);
    }

    void deviceAttributesReply()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b[c");
        QVERIFY2(h.replies.startsWith("\x1b[?"), h.replies.toHex().constData());
        h.replies.clear();
        h.feed("\x1b[6n");
        QCOMPARE(h.replies, QByteArray("\x1b[1;1R"));
    }

    void keyEncoding()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        auto key = [&](Key k, uint8_t mods = ModNone) {
            h.replies.clear();
            KeyInput in;
            in.key = k;
            in.modifiers = mods;
            h.vt->sendKey(in);
            return h.replies;
        };
        auto text = [&](char32_t cp, const QString &t, uint8_t mods = ModNone) {
            h.replies.clear();
            KeyInput in;
            in.codepoint = cp;
            in.text = t;
            in.modifiers = mods;
            h.vt->sendKey(in);
            return h.replies;
        };
        QCOMPARE(key(Key::Up), QByteArray("\x1b[A"));
        QCOMPARE(key(Key::Enter), QByteArray("\r"));
        QCOMPARE(key(Key::Backspace), QByteArray("\x7f"));
        QCOMPARE(key(Key::F5), QByteArray("\x1b[15~"));
        QCOMPARE(key(Key::Right, ModCtrl), QByteArray("\x1b[1;5C"));
        QCOMPARE(text('c', QStringLiteral("\x03"), ModCtrl), QByteArray("\x03"));
        QCOMPARE(text('x', QStringLiteral("x"), ModAlt), QByteArray("\x1bx"));
        QCOMPARE(text('a', QStringLiteral("A"), ModShift), QByteArray("A"));
        QCOMPARE(text(0x00e9, QStringLiteral("é")), QStringLiteral("é").toUtf8());
        h.feed("\x1b[?1h"); // application cursor keys
        QCOMPARE(key(Key::Up), QByteArray("\x1bOA"));
        // Key releases produce nothing unless a program asked for them (kitty protocol).
        h.replies.clear();
        KeyInput release;
        release.key = Key::Up;
        release.release = true;
        h.vt->sendKey(release);
        QVERIFY(h.replies.isEmpty());
    }

    void pasteAndFocus()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.vt->paste(QStringLiteral("a\nb"));
        QCOMPARE(h.replies, QByteArray("a\rb"));
        h.replies.clear();
        h.feed("\x1b[?2004h");
        QVERIFY(h.vt->bracketedPaste());
        h.vt->paste(QStringLiteral("x"));
        QCOMPARE(h.replies, QByteArray("\x1b[200~x\x1b[201~"));
        // A paste cannot end the bracket early or smuggle control codes.
        h.replies.clear();
        h.vt->paste(QStringLiteral("a\x1b[20\x1b[201~1~b\x03"));
        QCOMPARE(h.replies.count("\x1b[201~"), 1);
        QVERIFY(h.replies.endsWith("\x1b[201~"));
        QVERIFY(!h.replies.contains('\x03'));
        h.replies.clear();
        h.vt->focusChanged(true);
        QVERIFY(h.replies.isEmpty());
        h.feed("\x1b[?1004h");
        h.vt->focusChanged(false);
        QCOMPARE(h.replies, QByteArray("\x1b[O"));
    }

    void mouseReporting()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        QCOMPARE(h.vt->mouseTracking(), MouseTracking::None);
        h.feed("\x1b[?1000h\x1b[?1006h");
        QCOMPARE(h.vt->mouseTracking(), MouseTracking::Click);
        MouseInput m;
        m.action = MouseInput::Action::Press;
        m.button = MouseButton::Left;
        m.row = 2;
        m.col = 3;
        h.vt->sendMouse(m);
        QCOMPARE(h.replies, QByteArray("\x1b[<0;4;3M"));
    }

    void osc52ClipboardIsOptIn()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b]52;c;aGVsbG8=\x07");
        QVERIFY(h.clips.isEmpty());
        h.vt->setClipboardWriteAllowed(true);
        h.feed("\x1b]52;c;aGVsbG8=\x07");
        QCOMPARE(h.clips.size(), 1);
        QCOMPARE(h.clips[0].second, QByteArray("hello"));
    }

    void osc52CarriesRelayMarksOverMosh()
    {
        // #XQ8F: over a mosh link the remote script wraps every mark in an OSC 52 write, the one
        // OSC mosh forwards. A write whose decoded text starts with "relay:" is not a clipboard
        // write: the rest is fed back through the parser as a bare OSC, whatever
        // setClipboardWriteAllowed says, so the callbacks the real sequences fire run — and
        // events.clipboardWrite never sees it.
        QFETCH_GLOBAL(QString, core);
        const auto wrapped = [](const QByteArray &payload) {
            return QByteArrayLiteral("\x1b]52;c;") + payload.toBase64() + QByteArrayLiteral("\x07");
        };
        {
            Harness h(core);   // clipboard writes denied, the default
            h.feed(wrapped("relay:133;A"));
            QCOMPARE(h.marks.size(), 1);
            QCOMPARE(std::get<0>(h.marks[0]), MarkPromptStart);
            QVERIFY(h.clips.isEmpty());
        }
        Harness h(core);
        h.vt->setClipboardWriteAllowed(true);
        h.feed(wrapped("relay:133;A"));
        QCOMPARE(std::get<0>(h.marks.value(0)), MarkPromptStart);
        QVERIFY(h.clips.isEmpty());   // allowed or not, a mark is never a clipboard write
        // An ordinary OSC 52 write still behaves as before.
        h.feed("\x1b]52;c;" + QByteArray("hello").toBase64() + "\x07");
        QCOMPARE(h.clips.size(), 1);
        QCOMPARE(h.clips[0].second, QByteArray("hello"));
        // The rest of the family rides the same channel.
        h.feed(wrapped("relay:7;file://box/tmp"));
        QCOMPARE(h.cwds.size(), 1);
        QCOMPARE(h.cwds[0].first, QStringLiteral("/tmp"));
        QCOMPARE(h.cwds[0].second, QStringLiteral("box"));
        h.feed(wrapped("relay:777;notify;Build;done"));
        QCOMPARE(h.notes.size(), 1);
        QCOMPARE(h.notes[0].first, QStringLiteral("Build"));
        QCOMPARE(h.notes[0].second, QStringLiteral("done"));
        h.feed(wrapped("relay:133;D;0"));
        QCOMPARE(h.marks.size(), 2);
        QCOMPARE(std::get<0>(h.marks[1]), MarkCommandFinished);
        QCOMPARE(std::get<2>(h.marks[1]), 0);
        h.feed(wrapped("relay:7772;shell"));
        QVERIFY(h.frame().lines[0].marks & MarkUserShell);
        // A mark can be split across feeds, exactly as a real OSC can: both cores buffer.
        Harness split(core);
        split.feed(wrapped("relay:133;A").left(12));
        split.feed(wrapped("relay:133;A").mid(12));
        QCOMPARE(std::get<0>(split.marks.value(0)), MarkPromptStart);
        QVERIFY(split.clips.isEmpty());
    }

    void cursorShape()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core);
        h.feed("\x1b[6 q");
        ViewportFrame f = h.frame();
        QCOMPARE(int(f.cursor.shape), int(CursorShape::Bar));
        h.feed("\x1b[4 q");
        f = h.frame();
        QCOMPARE(int(f.cursor.shape), int(CursorShape::Underline));
        h.feed("\x1b[?25l");
        f = h.frame();
        QVERIFY(!f.cursor.visible);
    }


    // #6W0Z: a scrolled-out line is converted up to its last real cell, not to
    // the grid width, and it does not keep a grid-width allocation afterwards.
    // What counts as "real" is unchanged: a blank cell wearing a background or
    // reverse video is content and stays.
    void aStoredLineStopsAtItsLastRealCell()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 120);
        auto *lib = dynamic_cast<LibVtermCore *>(h.vt.get());
        if (!lib)
            QSKIP("work counters are libvterm's");
        // Push ten 9-character lines out of a 120-column grid.
        const quint64 before = lib->storedCells();
        for (int i = 0; i < 10; ++i)
            h.feed("nine char\r\n");
        for (int i = 0; i < 4; ++i)
            h.feed("\r\n");
        const quint64 converted = lib->storedCells() - before;
        // 10 lines x 9 cells, plus the blank lines that pushed them: far below
        // the 1 200 cells the grid width would have cost.
        QVERIFY2(converted <= 120, qPrintable(QStringLiteral("converted %1 cells").arg(converted)));

        std::vector<Line> lines;
        h.vt->historyLines(0, h.vt->historyRows(), &lines);
        int nine = -1;
        for (int i = 0; i < int(lines.size()); ++i) {
            if (lines[size_t(i)].text() == QStringLiteral("nine char"))
                nine = i;
        }
        QVERIFY(nine >= 0);
        QCOMPARE(int(lines[size_t(nine)].cells.size()), 9);
    }

    void aTrailingColouredSpaceIsNotABlank()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 40);
        h.vt->setColors(0xd8d8d8, 0x1c1e24, nullptr);
        // Erase to end of line under a background: cells with no character in
        // them that are nonetheless ink, right out to the grid width.
        h.feed("red\x1b[48;2;200;0;0m\x1b[K\r\n");
        // The same under a reverse pen: libvterm resolves the swap as it
        // erases, so what reaches the cell is a ground either way — ink, not a
        // blank, and the trim keeps it.
        h.feed("\x1b[0mrev\x1b[7m\x1b[48;2;0;0;200m\x1b[K\r\n");
        // Written spaces are characters, not blanks, and were never trimmed.
        h.feed("spaces    \r\n");
        // Nothing but the word: the rest of the row is blank and goes.
        h.feed("plain\r\n");
        for (int i = 0; i < 4; ++i)
            h.feed("\r\n");
        std::vector<Line> lines;
        h.vt->historyLines(0, h.vt->historyRows(), &lines);
        const auto lineStarting = [&lines](const QString &prefix) {
            for (const Line &l : lines) {
                if (l.text().startsWith(prefix))
                    return l;
            }
            return Line();
        };
        const Line red = lineStarting(QStringLiteral("red"));
        QCOMPARE(int(red.cells.size()), 40);                      // the erased tail is kept
        QCOMPARE(red.cells[39].bg, CellColor::rgb(200, 0, 0));
        QCOMPARE(red.cells[39].ch, char32_t(0));
        const Line rev = lineStarting(QStringLiteral("rev"));
        QCOMPARE(int(rev.cells.size()), 40);
        QVERIFY(CellColor::kind(rev.cells[39].bg) != CellColor::Default);
        QCOMPARE(int(lineStarting(QStringLiteral("spaces")).cells.size()), 10);
        QCOMPARE(int(lineStarting(QStringLiteral("plain")).cells.size()), 5);
    }

    // The trimmed line is what reflow reads back, so a resize after the
    // scrollback has been trimmed still rewraps to the same text.
    void aTrimmedScrollbackStillReflows()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 10);
        h.vt->setScrollbackLines(8);
        h.feed("0123456789abcdefghij\r\n");   // wraps into two rows of ten
        h.feed("red\x1b[48;2;200;0;0m  \x1b[0m\r\n");
        for (int i = 0; i < 4; ++i)
            h.feed("line\r\n");
        h.feed("end");
        h.vt->resize(4, 20, 8, 16);
        QStringList all = h.vt->historyText(100);
        all += h.vt->screenText().split(QLatin1Char('\n'));
        QVERIFY2(all.contains(QStringLiteral("0123456789abcdefghij")), qPrintable(all.join(QLatin1Char('|'))));
        QVERIFY(all.contains(QStringLiteral("red")));
        // The coloured blanks survived the round trip through libvterm.
        std::vector<Line> lines;
        h.vt->historyLines(0, h.vt->historyRows(), &lines);
        for (const Line &l : lines) {
            if (!l.text().startsWith(QStringLiteral("red")))
                continue;
            QCOMPARE(int(l.cells.size()), 5);
            QCOMPARE(l.cells[4].bg, CellColor::rgb(200, 0, 0));
        }
    }

    // #6W0Z: the URI behind a link id the caller already holds, without
    // converting the row it sits on.
    void hyperlinkUriByIdMatchesTheRow()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 20);
        h.feed("a\x1b]8;;https://example.com/x\x1b\\link\x1b]8;;\x1b\\b\r\n");
        ViewportFrame f = h.frame();
        const uint32_t id = f.lines[0].cells[2].link;
        QVERIFY(id != 0);
        QCOMPARE(h.vt->hyperlinkUri(id, 0, 2), QStringLiteral("https://example.com/x"));
        QCOMPARE(h.vt->hyperlinkUri(id, 0, 2), h.vt->hyperlinkAt(0, 2));
        QCOMPARE(h.vt->hyperlinkUri(0, 0, 0), QString());          // an unlinked cell
        QCOMPARE(f.lines[0].cells[0].link, uint32_t(0));
    }

    // #PPR4: a second walk costs the screen, not the whole scrollback, and
    // answers exactly what a full walk would.
    void hyperlinkRunsWalkOnlyWhatIsNew()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 40);
        auto *lib = dynamic_cast<LibVtermCore *>(h.vt.get());
        if (!lib)
            QSKIP("work counters are libvterm's");
        const QString prefix = QStringLiteral("relay://call/");
        const auto anchor = [&h](int n) {
            h.feed(QByteArray("\x1b]8;;relay://call/p/1/") + QByteArray::number(n) + "\x1b\\* ran it\x1b]8;;\x1b\\\r\n");
        };
        for (int i = 0; i < 40; ++i) {
            anchor(i);
            h.feed("plain output\r\n");
        }
        const std::vector<VtCore::HyperlinkRun> first = h.vt->hyperlinkRuns(prefix);
        QCOMPARE(int(first.size()), 40);
        // The scrollback is 80 rows deep; the first walk visited all of it.
        const quint64 afterFirst = lib->linkRowsWalked();
        QVERIFY2(afterFirst >= 80, qPrintable(QString::number(afterFirst)));

        // Nothing has moved: the second walk visits the screen only, and says
        // the same thing.
        const std::vector<VtCore::HyperlinkRun> again = h.vt->hyperlinkRuns(prefix);
        const quint64 secondWalk = lib->linkRowsWalked() - afterFirst;
        QCOMPARE(int(secondWalk), h.vt->rows());
        QCOMPARE(int(again.size()), int(first.size()));
        for (int i = 0; i < int(first.size()); ++i) {
            QCOMPARE(again[size_t(i)].uri, first[size_t(i)].uri);
            QCOMPARE(again[size_t(i)].startRow, first[size_t(i)].startRow);
            QCOMPARE(again[size_t(i)].endRow, first[size_t(i)].endRow);
            QCOMPARE(again[size_t(i)].startCol, first[size_t(i)].startCol);
        }

        // Ten more rows of output, one of them a new anchor: the walk covers
        // the new rows and the screen, and the rows every fold sits on have
        // moved down by exactly what was printed.
        const quint64 beforeThird = lib->linkRowsWalked();
        for (int i = 0; i < 5; ++i)
            h.feed("more output\r\n");
        anchor(99);
        h.feed("plain output\r\n");
        const std::vector<VtCore::HyperlinkRun> third = h.vt->hyperlinkRuns(prefix);
        QCOMPARE(int(third.size()), 41);
        QVERIFY2(lib->linkRowsWalked() - beforeThird < 20,
                 qPrintable(QString::number(lib->linkRowsWalked() - beforeThird)));
        // Nothing was trimmed, so the anchors are where they were: absolute
        // rows count from the oldest line the scrollback still holds.
        for (int i = 0; i < 40; ++i) {
            QCOMPARE(third[size_t(i)].uri, first[size_t(i)].uri);
            QCOMPARE(third[size_t(i)].startRow, first[size_t(i)].startRow);
        }
    }

    // The same answer as a full walk after the ring has been rewrapped, after
    // it has been trimmed, and after it has been cleared — the three things the
    // cache cannot see for itself.
    void hyperlinkRunsSurviveResizeTrimAndClear()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 4, 30);
        const QString prefix = QStringLiteral("relay://call/");
        for (int i = 0; i < 12; ++i) {
            h.feed(QByteArray("\x1b]8;;relay://call/p/1/") + QByteArray::number(i)
                   + "\x1b\\* a tool call line that is long enough to wrap when the grid narrows\x1b]8;;\x1b\\\r\n");
            h.feed("plain\r\n");
        }
        const auto uris = [](const std::vector<VtCore::HyperlinkRun> &runs) {
            QStringList out;
            for (const VtCore::HyperlinkRun &r : runs)
                out << QStringLiteral("%1@%2-%3").arg(r.uri).arg(r.startRow).arg(r.endRow);
            return out;
        };
        const QStringList wide = uris(h.vt->hyperlinkRuns(prefix));
        QCOMPARE(wide.size(), 12);

        h.vt->resize(4, 14, 8, 16);
        const QStringList narrow = uris(h.vt->hyperlinkRuns(prefix));
        QCOMPARE(narrow.size(), 12);
        // A fresh core fed the same bytes at the same width is the reference.
        {
            Harness ref(core, 4, 14);
            for (int i = 0; i < 12; ++i) {
                ref.feed(QByteArray("\x1b]8;;relay://call/p/1/") + QByteArray::number(i)
                         + "\x1b\\* a tool call line that is long enough to wrap when the grid narrows\x1b]8;;\x1b\\\r\n");
                ref.feed("plain\r\n");
            }
            QCOMPARE(uris(ref.vt->hyperlinkRuns(prefix)).size(), narrow.size());
        }

        // Trim: the oldest anchors leave the scrollback and the rest renumber.
        h.vt->setScrollbackLines(6);
        if (core == QStringLiteral("ghostty")) {
            // Ghostty's documented limit is an approximate byte budget, including
            // 256KiB page slack (docs/ENGINE.md), not libvterm's exact line count.
            // Twelve anchors fit in that slack. Force real eviction before checking
            // its links, and retain a fresh anchor so an empty/broken scan cannot pass.
            h.feed(QByteArray("plain padding\r\n").repeated(8192));
            h.feed("\x1b]8;;relay://call/p/1/fresh\x1b\\fresh anchor\x1b]8;;\x1b\\\r\n");
        }
        const std::vector<VtCore::HyperlinkRun> trimmed = h.vt->hyperlinkRuns(prefix);
        QVERIFY(int(trimmed.size()) < 12);
        if (core == QStringLiteral("ghostty")) {
            QCOMPARE(int(trimmed.size()), 1);
            QCOMPARE(trimmed.front().uri, QStringLiteral("relay://call/p/1/fresh"));
        }
        for (const VtCore::HyperlinkRun &r : trimmed) {
            QVERIFY(r.startRow >= 0);
            QVERIFY(r.endRow >= r.startRow);
        }
        h.vt->clearScrollback();
        for (const VtCore::HyperlinkRun &r : h.vt->hyperlinkRuns(prefix))
            QVERIFY(r.startRow >= 0);
        h.vt->reset();
        QVERIFY(h.vt->hyperlinkRuns(prefix).empty());
    }

    void dirtyTracking()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        ViewportFrame f;
        QVERIFY(h.vt->updateFrame(&f, false));
        QVERIFY(!h.vt->updateFrame(&f, false));
        h.feed("\x1b[3;1Hx");
        QVERIFY(h.vt->updateFrame(&f, false));
        QVERIFY(f.dirty[2]);
        QCOMPARE(f.lines[2].text(), QStringLiteral("x"));
    }

    // #3H5T: output scrolling the screen is a shift plus the rows it could not carry over, not a
    // whole new screen. A phone watching a streamed reply was sent 166 snapshots of 7,747 B for
    // exactly this; the frame now says what moved so the wire can say it too.
    void aScrollSaysWhatMoved()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        ViewportFrame f;
        h.feed("a\r\nb\r\nc\r\nd\r\ne");
        QVERIFY(h.vt->updateFrame(&f, true));
        h.feed("\r\nf");
        QVERIFY(h.vt->updateFrame(&f, false));
        if (f.scrolledBy == 0)
            QSKIP("this core does not describe a scroll yet");
        QCOMPARE(f.scrolledBy, 1);
        QCOMPARE(f.scrollTop, 0);
        QCOMPARE(f.scrollBottom, 5);
        // Every row still arrives — the desktop's own view repaints a scroll whole — but `dirty`
        // names only the rows a client that shifts its own copy has to be told about.
        QCOMPARE(f.lines[3].text(), QStringLiteral("e"));
        QCOMPARE(f.lines[4].text(), QStringLiteral("f"));
        int dirtyRows = 0;
        for (uint8_t bit : f.dirty)
            dirtyRows += bit ? 1 : 0;
        QVERIFY2(dirtyRows <= 2, qPrintable(QStringLiteral("a one-row scroll dirtied %1 rows")
                                                .arg(dirtyRows)));
        QVERIFY(f.dirty[4]);
    }

    // The reason the description has to be exact: a client that applies it must end up with the
    // screen a full frame would have given it, row for row, after a run of ordinary output.
    void aShiftedCopyMatchesAFullFrame()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        ViewportFrame f;
        QStringList held;                       // what a client that only shifts and patches holds
        const auto follow = [&] {
            if (!h.vt->updateFrame(&f, false)) return;
            const bool shift = f.scrolledBy != 0;
            if (!shift && f.full) {
                held.clear();
                for (int r = 0; r < f.rows; ++r) held << QString();
            }
            while (held.size() < f.rows) held << QString();
            if (shift) {
                const int by = f.scrolledBy;
                QStringList moved = held;
                for (int r = f.scrollTop; r < f.scrollBottom; ++r) {
                    const int from = r + by;
                    moved[r] = (from >= f.scrollTop && from < f.scrollBottom) ? held[from] : QString();
                }
                held = moved;
            }
            for (int r = 0; r < f.rows; ++r)
                if (f.dirty[size_t(r)]) held[r] = f.lines[size_t(r)].text();
        };
        follow();
        for (int i = 0; i < 40; ++i) {
            h.feed(QByteArray("row") + QByteArray::number(i) + "\r\n");
            follow();
        }
        h.feed("\x1b[2;3Hxy");                  // a plain in-place edit between the scrolls
        follow();
        h.feed("\x1b[H\x1b[2Jfresh");           // and a clear, which is never a scroll
        follow();

        ViewportFrame whole;
        QVERIFY(h.vt->updateFrame(&whole, true));
        QStringList expected;
        for (int r = 0; r < whole.rows; ++r) expected << whole.lines[size_t(r)].text();
        QCOMPARE(held, expected);
    }

    // A repaint that is not a scroll must not be dressed up as one, or a client would shift rows
    // that did not move.
    void aClearOrAResizeIsNeverAScroll()
    {
        QFETCH_GLOBAL(QString, core);
        Harness h(core, 5, 20);
        ViewportFrame f;
        h.feed("a\r\nb\r\nc\r\nd\r\ne");
        QVERIFY(h.vt->updateFrame(&f, true));
        h.feed("\x1b[H\x1b[2J");
        QVERIFY(h.vt->updateFrame(&f, false));
        // A clear damages every row without moving one; it went out as a diff of all five rows
        // before this change and still does.
        QCOMPARE(f.scrolledBy, 0);
        for (int r = 0; r < f.rows; ++r)
            QVERIFY(f.dirty[size_t(r)]);
        h.vt->resize(7, 20, 8, 16);
        QVERIFY(h.vt->updateFrame(&f, false));
        QVERIFY(f.full);
        QCOMPARE(f.scrolledBy, 0);
    }
};

QObject *makeCoreTest()
{
    return new CoreTest;
}

#include "CoreTest.moc"
