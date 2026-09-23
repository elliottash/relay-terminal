// SPDX-License-Identifier: AGPL-3.0-or-later
// Inline images (card #1MGS): the URI and placement contract (core/InlineImage.h).
#include "core/InlineImage.h"
#include "core/VtCore.h"

#include <QtTest>

using namespace relay;
using namespace relay::inlineimage;

class ImageTest : public QObject {
    Q_OBJECT
private slots:
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
