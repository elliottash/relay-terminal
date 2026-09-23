// SPDX-License-Identifier: AGPL-3.0-or-later
// Inline images (card #1MGS): the URI and placement contract (core/InlineImage.h), and the
// session's image protocol interceptor (session/ImageProtocol.h).
#include "core/AnsiSerializer.h"
#include "core/InlineImage.h"
#include "core/VtCore.h"
#include "session/ImageProtocol.h"
#include "session/TerminalSession.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace relay;
using namespace relay::inlineimage;

namespace {

// ---- helpers for the ImageProtocol tests ----------------------------------------------------

// Points the image cache at a scratch directory for one test and puts the old one back after.
struct ScratchCache {
    QTemporaryDir dir;
    QByteArray old = qgetenv("XDG_CACHE_HOME");
    bool had = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
    ScratchCache() { qputenv("XDG_CACHE_HOME", dir.path().toUtf8()); }
    ~ScratchCache()
    {
        if (had)
            qputenv("XDG_CACHE_HOME", old);
        else
            qunsetenv("XDG_CACHE_HOME");
    }
};

QByteArray pngBytes(QSize size, QColor color = Qt::red)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QByteArray kitty(const QByteArray &keys, const QByteArray &payload = {})
{
    return "\x1b_G" + keys + (payload.isNull() ? QByteArray() : ';' + payload) + "\x1b\\";
}

// An interceptor feeding a libvterm core with 10x20-pixel cells, collecting kitty replies.
struct Rig {
    std::unique_ptr<VtCore> core;
    ImageProtocol images;
    QByteArray replies;
    int rows, cols;
    explicit Rig(int rows_ = 10, int cols_ = 40)
        : core(createVtCore(QStringLiteral("libvterm"), rows_, cols_)), rows(rows_), cols(cols_)
    {
        core->resize(rows, cols, 10, 20);
        images.output = [this](const char *d, size_t n) { core->feed(d, n); };
        images.screen = [this] {
            ImageProtocol::Screen s;
            const CursorState cursor = core->activeCursor();
            s.cursorRow = cursor.row;
            s.cursorCol = cursor.col;
            s.rows = rows;
            s.columns = cols;
            s.cellPixels = QSize(10, 20);
            s.altScreen = core->altScreen();
            return s;
        };
        images.reply = [this](const char *d, size_t n) { replies.append(d, int(n)); };
    }
    void feed(const QByteArray &bytes) { images.feed(bytes); }
    void feedBytewise(const QByteArray &bytes)
    {
        for (char c : bytes)
            images.feed(&c, 1);
    }
    std::vector<ImageRef> refs() const
    {
        std::vector<ImageRef> out;
        for (const VtCore::HyperlinkRun &run : core->hyperlinkRuns(QString::fromLatin1(kImagePrefix))) {
            ImageRef ref;
            if (parseImageUri(run.uri, &ref))
                out.push_back(ref);
        }
        return out;
    }
    QStringList runs() const
    {
        QStringList out;
        for (const VtCore::HyperlinkRun &run : core->hyperlinkRuns(QString::fromLatin1(kImagePrefix)))
            out << QStringLiteral("%1,%2 %3").arg(run.startRow).arg(run.startCol).arg(run.uri);
        return out;
    }
    QString text() const { return core->screenText(); }
};

// An interceptor that only collects what it passes on, for the pass-through tests.
struct Pipe {
    ImageProtocol images;
    QByteArray out;
    Pipe()
    {
        images.output = [this](const char *d, size_t n) { out.append(d, int(n)); };
    }
};

// Everything the core holds (history and screen) as ANSI with its OSC 8 links, the way
// VTermBackend::replayableText() writes a live snapshot.
QByteArray replayable(VtCore &core)
{
    const int total = core.historyRows();
    std::vector<Line> lines;
    core.historyLines(0, total, &lines);
    core.scrollViewportToBottom();
    ViewportFrame frame;
    core.updateFrame(&frame, true);
    lines.insert(lines.end(), frame.lines.begin(), frame.lines.end());
    QStringList result;
    for (int i = 0; i < int(lines.size()); ++i) {
        const int absolute = i;
        result << lineToAnsi(lines[size_t(i)], [&](uint32_t id, int col) {
            if (absolute < core.viewportTop() || absolute >= core.viewportTop() + core.rows())
                core.scrollViewportToRow(absolute);
            return core.hyperlinkUri(id, absolute - core.viewportTop(), col);
        });
    }
    core.scrollViewportToBottom();
    while (!result.isEmpty() && result.last().isEmpty())
        result.removeLast();
    return result.join(QStringLiteral("\r\n")).toUtf8();
}

} // namespace

class ImageTest : public QObject {
    Q_OBJECT
private slots:
    // ---- session/ImageProtocol: the byte stream interceptor ----------------------------------

    void kittyPngInOneChunk()
    {
        ScratchCache cache;
        Rig rig;
        const QByteArray png = pngBytes(QSize(40, 40));
        rig.feed("ab" + kitty("a=T,f=100", png.toBase64()) + "cd");
        const std::vector<ImageRef> refs = rig.refs();
        QCOMPARE(int(refs.size()), 2); // 40x40 px in 10x20 cells: 4 x 2
        QCOMPARE(refs[0].cols, 4);
        QCOMPARE(refs[0].rows, 2);
        QVERIFY(refs[0].path.startsWith(cache.dir.path()));
        QFile stored(refs[0].path);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), png);
        // The text after the image lands where kitty leaves the cursor: last row, past the edge.
        const QStringList lines = rig.text().split('\n');
        QVERIFY(lines.value(0).startsWith("ab"));
        QCOMPARE(lines.value(1).mid(6, 2), QStringLiteral("cd"));
        QVERIFY(rig.replies.isEmpty()); // no id, no reply
    }

    void kittySplitByteByByteMatchesOneChunk()
    {
        ScratchCache cache;
        const QByteArray bytes = "x\x1b[1my" + kitty("a=T,f=100,i=3", pngBytes(QSize(30, 50)).toBase64()) + "z\x1b";
        Rig whole, split;
        whole.feed(bytes);
        whole.feed("[0m!");
        split.feedBytewise(bytes);
        split.feedBytewise("[0m!");
        QCOMPARE(split.runs(), whole.runs());
        QCOMPARE(split.text(), whole.text());
        QCOMPARE(split.replies, whole.replies);
        QCOMPARE(whole.replies, QByteArray("\x1b_Gi=3;OK\x1b\\"));
        QVERIFY(!whole.runs().isEmpty());
        QVERIFY(whole.text().contains('!'));
    }

    void kittyChunkedTransfer()
    {
        ScratchCache cache;
        Rig rig;
        const QByteArray b64 = pngBytes(QSize(20, 20)).toBase64();
        QByteArray stream = "<";
        for (int at = 0; at < b64.size(); at += 16) {
            const bool last = at + 16 >= b64.size();
            const QByteArray keys = at == 0 ? "a=T,f=100,i=7,m=1" : last ? "m=0" : "m=1";
            stream += kitty(keys, b64.mid(at, 16));
        }
        stream += ">";
        rig.feedBytewise(stream);
        QCOMPARE(int(rig.refs().size()), 1);
        QCOMPARE(rig.refs()[0].cols, 2);
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=7;OK\x1b\\"));
        QVERIFY(rig.text().startsWith("<"));
        QVERIFY(rig.text().contains(">"));
    }

    void kittyZlibRgba()
    {
        ScratchCache cache;
        Rig rig;
        QByteArray pixels;
        const QList<QRgb> colours{qRgba(255, 0, 0, 255), qRgba(0, 255, 0, 255), qRgba(0, 0, 255, 128),
                                  qRgba(10, 20, 30, 255), qRgba(200, 100, 50, 0), qRgba(1, 2, 3, 4)};
        for (QRgb c : colours)
            pixels.append(char(qRed(c))).append(char(qGreen(c))).append(char(qBlue(c))).append(char(qAlpha(c)));
        const QByteArray zlib = qCompress(pixels).mid(4); // qCompress prefixes a length: kitty sends bare zlib
        rig.feed(kitty("a=T,f=32,s=3,v=2,o=z,i=11", zlib.toBase64()));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=11;OK\x1b\\"));
        QCOMPARE(int(rig.refs().size()), 1);
        const QImage back = QImage(rig.refs()[0].path).convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(back.size(), QSize(3, 2));
        for (int i = 0; i < 6; ++i) {
            const QColor got = back.pixelColor(i % 3, i / 3);
            const QRgb want = colours[i];
            if (qAlpha(want) == 0)
                QCOMPARE(got.alpha(), 0);
            else
                QCOMPARE(got.rgba(), QColor::fromRgba(want).rgba());
        }
        // A zlib bomb for a tiny declared size is refused, and so is garbage.
        rig.replies.clear();
        rig.feed(kitty("a=T,f=24,s=1,v=1,o=z,i=12", qCompress(QByteArray(1 << 20, 'x')).mid(4).toBase64()));
        QVERIFY2(rig.replies.startsWith("\x1b_Gi=12;EINVAL"), rig.replies.constData());
        rig.replies.clear();
        rig.feed(kitty("a=T,f=24,s=1,v=1,o=z,i=13", QByteArray("not zlib").toBase64()));
        QVERIFY2(rig.replies.startsWith("\x1b_Gi=13;EINVAL"), rig.replies.constData());
        QCOMPARE(int(rig.refs().size()), 1);
    }

    void kittyFileAndTempFile()
    {
        ScratchCache cache;
        QTemporaryDir files;
        const QString path = files.path() + "/picture.png";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pngBytes(QSize(20, 40)));
        file.close();
        Rig rig;
        rig.feed(kitty("a=T,t=f,f=100,i=1", path.toUtf8().toBase64()));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=1;OK\x1b\\"));
        QCOMPARE(int(rig.refs().size()), 2);
        QCOMPARE(rig.refs()[0].path, QFileInfo(path).canonicalFilePath()); // the program's own file
        QVERIFY(QDir(cache.dir.path() + "/relay/images/terminal").isEmpty());

        // t=t: read, then deleted only when it is plainly a graphics temp file.
        const QString temp = QDir::tempPath() + "/tty-graphics-protocol-relay-test.png";
        const QString kept = files.path() + "/a-temp-file-of-some-other-kind.png"; // in /tmp, but not named for this
        for (const QString &p : {temp, kept}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(pngBytes(QSize(10, 20), Qt::blue));
        }
        rig.replies.clear();
        rig.feed(kitty("a=T,t=t,f=100,i=2", temp.toUtf8().toBase64()));
        rig.feed(kitty("a=T,t=t,f=100,i=3", kept.toUtf8().toBase64()));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=2;OK\x1b\\\x1b_Gi=3;OK\x1b\\"));
        QVERIFY(!QFile::exists(temp));
        QVERIFY(QFile::exists(kept));

        // Nothing under /proc, /sys or /dev is read, a missing file is ENOENT, a directory EBADF.
        rig.replies.clear();
        rig.feed(kitty("a=T,t=f,f=100,i=4", QByteArray("/proc/self/status").toBase64()));
        rig.feed(kitty("a=T,t=f,f=100,i=5", (files.path() + "/missing.png").toUtf8().toBase64()));
        rig.feed(kitty("a=T,t=f,f=100,i=6", files.path().toUtf8().toBase64()));
        QVERIFY2(rig.replies.contains("i=4;EPERM"), rig.replies.constData());
        QVERIFY2(rig.replies.contains("i=5;ENOENT"), rig.replies.constData());
        QVERIFY2(rig.replies.contains("i=6;EBADF"), rig.replies.constData());
    }

    void kittyTransmitThenPut()
    {
        ScratchCache cache;
        Rig rig;
        rig.feed(kitty("a=t,f=100,i=5", pngBytes(QSize(40, 20)).toBase64()));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=5;OK\x1b\\"));
        QVERIFY(rig.refs().empty()); // transmitted, not shown
        rig.replies.clear();
        rig.feed(kitty("a=p,i=5,p=2,c=8"));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=5,p=2;OK\x1b\\"));
        QCOMPARE(int(rig.refs().size()), 2); // 8 cells wide keeps 2:1, so 2 rows of 20 px
        QCOMPARE(rig.refs()[0].cols, 8);
        // An image number gets an id from the terminal, and a=p finds it by number.
        rig.replies.clear();
        rig.feed(kitty("a=t,f=100,I=9", pngBytes(QSize(10, 20)).toBase64()));
        QVERIFY2(rig.replies.startsWith("\x1b_Gi=") && rig.replies.endsWith(",I=9;OK\x1b\\"), rig.replies.constData());
        rig.replies.clear();
        rig.feed(kitty("a=p,I=9"));
        QVERIFY2(rig.replies.startsWith("\x1b_Gi=") && rig.replies.endsWith(",I=9;OK\x1b\\"), rig.replies.constData());
        QCOMPARE(int(rig.refs().size()), 3);
        // Unknown and deleted ids are ENOENT; the rows already placed stay.
        rig.replies.clear();
        rig.feed(kitty("a=p,i=99"));
        rig.feed(kitty("a=d,d=i,i=5"));
        rig.feed(kitty("a=p,i=5"));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=99;ENOENT:no image with this id\x1b\\"
                                         "\x1b_Gi=5;ENOENT:no image with this id\x1b\\"));
        QCOMPARE(int(rig.refs().size()), 3);
    }

    void kittyQueryRepliesWithoutDisplaying()
    {
        ScratchCache cache;
        Rig rig;
        // What kitten icat, chafa and timg send to detect support.
        rig.feed(kitty("i=31,s=1,v=1,a=q,t=d,f=24", "AAAA"));
        QCOMPARE(rig.replies, QByteArray("\x1b_Gi=31;OK\x1b\\"));
        QVERIFY(rig.refs().empty());
        QVERIFY(QDir(cache.dir.path() + "/relay/images/terminal").isEmpty());
        QVERIFY(rig.text().trimmed().isEmpty());
        // A query with too little data is an error, still without display.
        Rig bad;
        bad.feed(kitty("i=32,s=2,v=2,a=q,t=d,f=24", "AAAA"));
        QVERIFY2(bad.replies.startsWith("\x1b_Gi=32;ENODATA"), bad.replies.constData());
    }

    void kittyQuietKeys()
    {
        ScratchCache cache;
        Rig rig;
        rig.feed(kitty("i=1,s=1,v=1,a=q,f=24,q=1", "AAAA")); // OK suppressed
        rig.feed(kitty("i=2,s=1,v=1,a=q,f=24,q=2", "!!")); // error suppressed too
        QVERIFY(rig.replies.isEmpty());
        rig.feed(kitty("i=3,s=1,v=1,a=q,f=24,q=1", "!!")); // q=1 still reports errors
        QVERIFY2(rig.replies.startsWith("\x1b_Gi=3;EINVAL"), rig.replies.constData());
    }

    void imageAtTheRightMarginKeepsItsColumn()
    {
        ScratchCache cache;
        Rig rig(10, 40);
        rig.feed(QByteArray(39, '.') + kitty("a=T,f=100", pngBytes(QSize(10, 60)).toBase64()) + "\r\nnext");
        const auto runs = rig.core->hyperlinkRuns(QString::fromLatin1(kImagePrefix));
        QCOMPARE(int(runs.size()), 3);
        QList<int> columns;
        for (const VtCore::HyperlinkRun &run : runs)
            columns << run.startCol;
        QCOMPARE(columns.value(0), 39);
        QVERIFY(rig.text().split('\n').value(3).startsWith("next"));
        // Contract bug (core/InlineImage.cpp): writing the row cell in the last column leaves the
        // cursor pending a wrap, and placementBytes' "\b" then lands one column further left.
        QEXPECT_FAIL("", "placementBytes' backspace from the right margin, #1MGS", Continue);
        QCOMPARE(columns, (QList<int>{39, 39, 39}));
    }

    void kittyNoCursorMove()
    {
        ScratchCache cache;
        Rig rig;
        rig.feed("xy" + kitty("a=T,f=100,C=1", pngBytes(QSize(40, 60)).toBase64()));
        QCOMPARE(int(rig.refs().size()), 3);
        QCOMPARE(rig.core->activeCursor().row, 0);
        QCOMPARE(rig.core->activeCursor().col, 2);
    }

    void malformedAndOversizeAreDroppedTextIntact()
    {
        ScratchCache cache;
        Rig rig;
        rig.feed("a" + kitty("a=T,f=100,i=4", "!!!!") + "b");
        rig.feed(kitty("a=T,f=32,s=20000,v=1,i=5", "AAAA") + "c");
        rig.feed(kitty("a=T,f=100,i=6", QByteArray("not an image").toBase64()) + "d");
        QVERIFY2(rig.replies.contains("i=4;EINVAL"), rig.replies.constData());
        QVERIFY2(rig.replies.contains("i=5;EFBIG"), rig.replies.constData());
        QVERIFY2(rig.replies.contains("i=6;EBADPNG"), rig.replies.constData());
        QVERIFY(rig.refs().empty());
        QVERIFY(rig.text().startsWith("abcd"));

        // A body past the sequence cap is dropped whole, and what follows is intact.
        Rig big;
        big.feed("\x1b_Gi=9,a=T,f=100;");
        const QByteArray block(1 << 20, 'A');
        for (qint64 sent = 0; sent <= ImageProtocol::kMaxSequenceBytes; sent += block.size())
            big.feed(block);
        big.feed("\x1b\\tail");
        QCOMPARE(big.replies, QByteArray("\x1b_Gi=9;EFBIG:the image is too large\x1b\\"));
        QVERIFY(big.text().startsWith("tail"));

        // A never-terminated APC ends at the next escape sequence, as in a VT parser.
        Rig open;
        open.feed("\x1b_Ga=T,f=100;AAAA\x1b[31mred\x1b[0m");
        QVERIFY(open.text().startsWith("red"));
        QVERIFY(open.images.atGround());
    }

    void itermInlineImage()
    {
        ScratchCache cache;
        Rig rig;
        const QByteArray b64 = pngBytes(QSize(100, 50)).toBase64();
        rig.feed("[" "\x1b]1337;File=name=" + QByteArray("a.png").toBase64() + ";size=1;width=10;inline=1:" + b64 + "\x07]");
        QCOMPARE(int(rig.refs().size()), 3); // 10 cells wide: 100 px of 10 -> 50 px of 20 = 2.5 rows
        QCOMPARE(rig.refs()[0].cols, 10);
        QVERIFY(rig.text().startsWith("["));
        QVERIFY(rig.text().contains("]"));
        // inline=0 is a download: swallowed, nothing shown. ST works as a terminator too.
        Rig download;
        download.feed("<\x1b]1337;File=inline=0:" + b64 + "\x1b\\>");
        QVERIFY(download.refs().empty());
        QVERIFY(download.text().startsWith("<>"));
        // Pixel and percent sizes, and the multipart form.
        Rig parts;
        parts.feed("\x1b]1337;MultipartFile=inline=1;width=50px;height=50%\x07");
        for (int at = 0; at < b64.size(); at += 100)
            parts.feed("\x1b]1337;FilePart=" + b64.mid(at, 100) + "\x07");
        parts.feed("\x1b]1337;FileEnd\x07");
        // A 5 x 5 box (50 px of 10; half of 10 rows), and the 2:1 picture fitted into it: 5 x 2.
        QCOMPARE(int(parts.refs().size()), 2);
        QCOMPARE(parts.refs()[0].cols, 5);
    }

    void sixelDecodesColours()
    {
        // Two columns six pixels high: red (RGB percent) and blue, then an HLS red row below.
        const QImage image = ImageProtocol::decodeSixel("0;1;0", "\"1;1;2;12#1;2;100;0;0#2;2;0;0;100#1~#2~-#3;1;120;50;100!2~");
        QCOMPARE(image.size(), QSize(2, 12));
        QCOMPARE(QColor(image.pixel(0, 0)), QColor(255, 0, 0));
        QCOMPARE(QColor(image.pixel(1, 5)), QColor(0, 0, 255));
        QCOMPARE(QColor(image.pixel(1, 11)), QColor(255, 0, 0));
        // P2=1: pixels no sixel set are transparent; otherwise they take colour register 0.
        const QImage clear = ImageProtocol::decodeSixel("0;1", "#1;2;0;100;0@");
        QCOMPARE(clear.size(), QSize(1, 1));
        const QImage raster = ImageProtocol::decodeSixel("0;1", "\"1;1;1;6#1;2;0;100;0@");
        QCOMPARE(raster.size(), QSize(1, 6));
        QCOMPARE(QColor(raster.pixel(0, 0)), QColor(0, 255, 0));
        QCOMPARE(qAlpha(raster.pixel(0, 3)), 0);
        const QImage opaque = ImageProtocol::decodeSixel("0;0", "\"1;1;1;6#1;2;0;100;0@");
        QCOMPARE(qAlpha(opaque.pixel(0, 3)), 255);
        QVERIFY(ImageProtocol::decodeSixel("", "").isNull());
        QVERIFY(ImageProtocol::decodeSixel("", "!20000~").isNull()); // over the side cap

        ScratchCache cache;
        Rig rig;
        rig.feedBytewise("s\x1bPq#1;2;100;0;0!20~-!20~-!20~-!20~\x1b\\e");
        QCOMPARE(int(rig.refs().size()), 2); // 20 x 24 px: 2 x 2 cells
        QCOMPARE(rig.refs()[0].cols, 2);
        const QImage stored(rig.refs()[0].path);
        QCOMPARE(stored.size(), QSize(20, 24));
        QCOMPARE(QColor(stored.pixel(19, 23)), QColor(255, 0, 0));
        QVERIFY(rig.text().startsWith("s"));
    }

    void otherSequencesPassThroughUnchanged()
    {
        const QByteArray stream = QByteArray("plain \x1b[31mred\x1b[0m ")
                                  + "\x1b_Xapc payload\x1b\\"             // an APC that is not G
                                  + "\x1b]0;title\x07"                    // OSC, BEL
                                  + "\x1b]1337;SetMark\x07"              // iTerm2, not an image
                                  + "\x1b]1337;Fil\x07"                   // a prefix of File= that is not it
                                  + "\x1b]8;;https://x.test/\x1b\\link\x1b]8;;\x1b\\"
                                  + "\x1bP$qm\x1b\\"                     // DECRQSS
                                  + "\x1bP+q544e\x1b\\"                  // XTGETTCAP
                                  + "\x1bP1000p\x1b\\"                   // a DCS with parameters
                                  + "\x1b\x1b[1m\x1b(B\x1b" "7\x1b" "8 end\x1b";
        Pipe whole;
        whole.images.feed(stream);
        QCOMPARE(whole.out, stream.chopped(1)); // the trailing ESC waits for its next byte
        QVERIFY(!whole.images.atGround());
        whole.images.feed("[m");
        QCOMPARE(whole.out, stream + "[m");
        Pipe split;
        for (char c : stream)
            split.images.feed(&c, 1);
        split.images.feed("[m");
        QCOMPARE(split.out, stream + "[m");
    }

    void sessionPlacesImagesFromDisplayWrites()
    {
        ScratchCache cache;
        QTemporaryDir files;
        const QString path = files.path() + "/shown.png";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(pngBytes(QSize(60, 40)));
        file.close();
        TerminalSession session(QStringLiteral("libvterm"));
        session.resize(10, 40, 12, 20); // the cell size the view reported
        session.writeToDisplay("before " + kitty("a=T,t=f,f=100", path.toUtf8().toBase64()) + " after");
        const auto runs = session.withCore([](VtCore &core) { return core.hyperlinkRuns(QString::fromLatin1(kImagePrefix)); });
        QCOMPARE(int(runs.size()), 2);
        ImageRef ref;
        QVERIFY(parseImageUri(runs[0].uri, &ref));
        QCOMPARE(ref.cols, 5); // 60 px / 12
        QCOMPARE(ref.rows, 2); // 40 px / 20
        QCOMPARE(ref.path, QFileInfo(path).canonicalFilePath());
        QCOMPARE(runs[0].startCol, 7);
        QVERIFY(session.screenText().contains("after"));
    }

    void sessionAnswersTheProgramsQuery()
    {
        // The support probe goes out through the pty and the program reads the reply on stdin.
        ScratchCache cache;
        TerminalSession session(QStringLiteral("libvterm"));
        session.resize(10, 60, 10, 20);
        QSignalSpy finished(&session, &TerminalSession::finished);
        TerminalSession::StartOptions o;
        o.program = QStringLiteral("/bin/sh");
        o.arguments = QStringList{QStringLiteral("-c"),
                                  QStringLiteral("stty raw -echo; printf '\\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\\033\\\\'; "
                                                 "r=$(head -c 12 | od -An -tx1 | tr -d ' \\n'); stty sane; printf '\\nreply:%s\\n' \"$r\"")};
        QVERIFY(session.start(o));
        QVERIFY2(finished.wait(5000), qPrintable(session.screenText()));
        QVERIFY2(session.screenText().contains(QLatin1String("reply:1b5f47693d33313b4f4b1b5c")), qPrintable(session.screenText()));
        const auto runs = session.withCore([](VtCore &core) { return core.hyperlinkRuns(QString::fromLatin1(kImagePrefix)); });
        QVERIFY(runs.empty());
    }

    void placementRowsSurviveALiveReplay()
    {
        std::unique_ptr<VtCore> core = createVtCore(QStringLiteral("libvterm"), 5, 30);
        core->feed("one\r\ntwo\r\nthree\r\n  ", 17);
        const QByteArray bytes = placementBytes(QStringLiteral("/tmp/replay.png"), QSize(6, 4)) + " tail\r\nlast";
        core->feed(bytes.constData(), size_t(bytes.size()));
        const auto before = core->hyperlinkRuns(QString::fromLatin1(kImagePrefix));
        QCOMPARE(int(before.size()), 4);
        QVERIFY(core->historyRows() > 0); // the picture's top rows are in the scrollback
        const QByteArray ansi = replayable(*core);
        std::unique_ptr<VtCore> fresh = createVtCore(QStringLiteral("libvterm"), 5, 30);
        fresh->feed(ansi.constData(), size_t(ansi.size()));
        const auto after = fresh->hyperlinkRuns(QString::fromLatin1(kImagePrefix));
        QCOMPARE(int(after.size()), int(before.size()));
        for (size_t i = 0; i < before.size(); ++i) {
            QCOMPARE(after[i].uri, before[i].uri);
            QCOMPARE(after[i].startCol, before[i].startCol);
            QCOMPARE(after[i].startRow - after[0].startRow, before[i].startRow - before[0].startRow);
        }
        QCOMPARE(fresh->screenText(), core->screenText());
    }

    // ---- core/InlineImage: the contract ------------------------------------------------------

    void uriRoundTrips()
    {
        const ImageRef ref{QStringLiteral("/tmp/a b/c%d.png"), 2, 5, 12};
        const QString uri = imageUri(ref);
        QVERIFY(uri.startsWith(QLatin1String(kImagePrefix)));
        ImageRef back;
        QVERIFY(parseImageUri(uri, &back));
        QCOMPARE(back, ref);
    }
    void malformedUrisAreRefused()
    {
        for (const char *bad : {"relay-image:", "relay-image:1/2/x/tmp/a.png", "relay-image:0/1/1/relative.png",
                                "https://example.com/a.png", "relay-image:0/1/1/"})
            QVERIFY2(!parseImageUri(QString::fromLatin1(bad), nullptr), bad);
        ImageRef clamped;
        QVERIFY(parseImageUri(QStringLiteral("relay-image:9/3/99999//tmp/a.png"), &clamped));
        QCOMPARE(clamped.row, 2);
        QCOMPARE(clamped.cols, kMaxCols);
    }
    void cellsKeepTheAspectRatio()
    {
        const QSize cell(10, 20);
        QCOMPARE(cellsFor(QSize(100, 100), cell, 80, 24), QSize(10, 5));
        QCOMPARE(cellsFor(QSize(2000, 1000), cell, 80, 24), QSize(80, 20));   // width-bound
        QCOMPARE(cellsFor(QSize(100, 2000), cell, 80, 24), QSize(3, 24));     // height-bound
        QCOMPARE(cellsFor(QSize(100, 100), cell, 80, 24, QSize(20, 0)), QSize(20, 10));
        QCOMPARE(cellsFor(QSize(1, 1), cell, 80, 24), QSize(1, 1));
    }
    void placementBecomesLinkedRowsInTheCore()
    {
        std::unique_ptr<VtCore> core = createVtCore(QStringLiteral("libvterm"), 10, 40);
        QVERIFY(core);
        core->feed("ab", 2);
        const QByteArray bytes = placementBytes(QStringLiteral("/tmp/pic.png"), QSize(6, 3));
        core->feed(bytes.constData(), size_t(bytes.size()));
        const CursorState cursor = core->activeCursor();
        const std::vector<VtCore::HyperlinkRun> runs = core->hyperlinkRuns(QString::fromLatin1(kImagePrefix));
        QCOMPARE(int(runs.size()), 3);
        for (int i = 0; i < 3; ++i) {
            ImageRef ref;
            QVERIFY(parseImageUri(runs[size_t(i)].uri, &ref));
            QCOMPARE(ref.row, i);
            QCOMPARE(ref.rows, 3);
            QCOMPARE(ref.cols, 6);
            QCOMPARE(runs[size_t(i)].startCol, 2);  // every row in the image's column
        }
        // The cursor ends just past the image's right edge on its last row, as kitty leaves it.
        QCOMPARE(cursor.row, 2);
        QCOMPARE(cursor.col, 2 + 6);
    }
    void noCursorMoveRestoresTheCursor()
    {
        std::unique_ptr<VtCore> core = createVtCore(QStringLiteral("libvterm"), 10, 40);
        core->feed("xy", 2);
        const QByteArray bytes = placementBytes(QStringLiteral("/tmp/pic.png"), QSize(4, 2), false);
        core->feed(bytes.constData(), size_t(bytes.size()));
        QCOMPARE(core->activeCursor().row, 0);
        QCOMPARE(core->activeCursor().col, 2);
    }
};

QObject *makeImageTest() { return new ImageTest; }

#include "ImageTest.moc"
