// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI-side rules behind image context (issue EM1E): what counts as an image, how a pasted or
// captured image is named and written, what a paste or a drop is carrying, the `@` token the
// composer gets, and the cache sweep. No window, no worker and no provider.
#include "Images.h"

#include <QDir>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace relay::images;

namespace {

QImage square(int side = 8) {
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::red);
    return image;
}

QByteArray png() {
    return QByteArrayLiteral("\x89PNG\r\n\x1a\n") + QByteArray(16, '\0');
}

}  // namespace

class ImagesTests : public QObject {
    Q_OBJECT

private slots:
    void sniffsTheTypeFromTheBytes() {
        QCOMPARE(sniff(png()), QStringLiteral("image/png"));
        QCOMPARE(sniff(QByteArrayLiteral("\xff\xd8\xff\xe0")), QStringLiteral("image/jpeg"));
        QCOMPARE(sniff(QByteArrayLiteral("GIF89a") + QByteArray(8, '\0')), QStringLiteral("image/gif"));
        QCOMPARE(sniff(QByteArrayLiteral("RIFF") + QByteArray(4, '\0') + QByteArrayLiteral("WEBPxxxx")),
                 QStringLiteral("image/webp"));
        QCOMPARE(sniff(QByteArrayLiteral("#!/bin/sh\n")), QString());
        QCOMPARE(sniff(QByteArray()), QString());
    }

    void aMislabelledFileIsJudgedByItsContent() {
        QTemporaryDir dir;
        const QString jpgName = dir.filePath(QStringLiteral("actually.jpg"));
        QFile file(jpgName);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(png());
        file.close();
        QCOMPARE(mediaTypeOf(jpgName), QStringLiteral("image/png"));
        QVERIFY(isImageFile(jpgName));

        const QString textName = dir.filePath(QStringLiteral("notes.png"));
        QFile text(textName);
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("plain text pretending to be a picture");
        text.close();
        QVERIFY(!isImageFile(textName));
        QVERIFY(!isImageFile(dir.filePath(QStringLiteral("missing.png"))));
    }

    void composerTokensQuoteAPathWithSpaces() {
        QCOMPARE(composerToken(QStringLiteral("/tmp/shot.png")), QStringLiteral("@/tmp/shot.png"));
        QCOMPARE(composerToken(QStringLiteral("/tmp/my shot.png")), QStringLiteral("@\"/tmp/my shot.png\""));
    }

    void captureNamesAreStampedAndNeverCollide() {
        QTemporaryDir dir;
        const QDateTime when(QDate(2026, 9, 17), QTime(14, 30, 5));
        const QString first = newCapturePath(dir.path(), QStringLiteral("pane"), when);
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("relay-pane-20260917-143005.png"));
        QVERIFY(!savePng(square(), first).isEmpty());
        const QString second = newCapturePath(dir.path(), QStringLiteral("pane"), when);
        QVERIFY(second != first);
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("relay-pane-20260917-143005-2.png"));
    }

    void savingWritesARealPngAndRefusesAnEmptyImage() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shot.png"));
        QCOMPARE(savePng(square(), path), path);
        QVERIFY(isImageFile(path));
        QCOMPARE(mediaTypeOf(path), QStringLiteral("image/png"));
        QVERIFY(savePng(QImage(), dir.filePath(QStringLiteral("empty.png"))).isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("empty.png"))));
    }

    void aDroppedImageFileIsAttachedWhereItIs() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("dropped.png"));
        QVERIFY(!savePng(square(), path).isEmpty());
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(path)});
        QVERIFY(hasImage(&data));
        // The file is used where it is: nothing is copied into the cache.
        QCOMPARE(fromMimeData(&data, dir.path()), QStringList{path});
    }

    void aDroppedNonImageIsNotAnImage() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("notes.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();
        QMimeData data;
        data.setUrls({QUrl::fromLocalFile(path)});
        QVERIFY(!hasImage(&data));
        QVERIFY(fromMimeData(&data, dir.path()).isEmpty());
    }

    void pastedImageDataIsWrittenIntoTheCacheFolder() {
        QTemporaryDir dir;
        QMimeData data;
        data.setImageData(square(12));
        QVERIFY(hasImage(&data));
        const QStringList paths = fromMimeData(&data, dir.path());
        QCOMPARE(paths.size(), 1);
        QVERIFY(paths.first().startsWith(dir.path()));
        QVERIFY(QFileInfo(paths.first()).fileName().startsWith(QStringLiteral("relay-paste-")));
        QVERIFY(isImageFile(paths.first()));
    }

    void plainTextIsNotAnImage() {
        QMimeData data;
        data.setText(QStringLiteral("ls -la"));
        QVERIFY(!hasImage(&data));
        QVERIFY(fromMimeData(&data, QDir::tempPath()).isEmpty());
        QVERIFY(!hasImage(nullptr));
    }

    void theCacheSweepOnlyTakesOldRelayCaptures() {
        QTemporaryDir dir;
        const QString old = dir.filePath(QStringLiteral("relay-paste-20260101-000000.png"));
        const QString fresh = dir.filePath(QStringLiteral("relay-paste-20260917-000000.png"));
        const QString other = dir.filePath(QStringLiteral("holiday.png"));
        for (const QString &path : {old, fresh, other}) QVERIFY(!savePng(square(), path).isEmpty());
        const QDateTime now = QDateTime::currentDateTime();
        QCOMPARE(pruneCache(dir.path(), 7, now.addDays(8)), 3 - 1);   // both captures, not holiday.png
        QVERIFY(QFileInfo::exists(other));
        QCOMPARE(pruneCache(dir.path(), 0, now), 0);
        QCOMPARE(pruneCache(dir.filePath(QStringLiteral("nowhere")), 7, now), 0);
    }

    void theCapMatchesTheWorker() {
        // backend/relay_core/provider.py MAX_IMAGE_BYTES; a mismatch means the GUI accepts a file
        // the worker then refuses.
        QCOMPARE(kMaxImageBytes, qint64(3 * 1024 * 1024));
    }
};

QTEST_MAIN(ImagesTests)
#include "images_test.moc"
