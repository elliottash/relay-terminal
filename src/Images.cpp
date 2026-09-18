// SPDX-License-Identifier: GPL-3.0-or-later
#include "Images.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>

namespace relay {
namespace images {

QString cacheDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath() + QStringLiteral("/relay");
    const QString dir = base + QStringLiteral("/images");
    QDir().mkpath(dir);
    return dir;
}

QString sniff(const QByteArray &head) {
    if (head.startsWith(QByteArrayLiteral("\x89PNG\r\n\x1a\n"))) return QStringLiteral("image/png");
    if (head.startsWith(QByteArrayLiteral("\xff\xd8\xff"))) return QStringLiteral("image/jpeg");
    if (head.startsWith(QByteArrayLiteral("GIF87a")) || head.startsWith(QByteArrayLiteral("GIF89a")))
        return QStringLiteral("image/gif");
    if (head.size() >= 12 && head.startsWith(QByteArrayLiteral("RIFF")) && head.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return QStringLiteral("image/webp");
    return {};
}

QString mediaTypeOf(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return sniff(file.read(16));
}

QString composerToken(const QString &path) {
    if (path.contains(QLatin1Char(' ')))
        return QStringLiteral("@\"") + path + QLatin1Char('"');
    return QLatin1Char('@') + path;
}

QString newCapturePath(const QString &dir, const QString &what, const QDateTime &when) {
    const QString stamp = when.toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString base = QStringLiteral("%1/relay-%2-%3").arg(dir, what, stamp);
    QString path = base + QStringLiteral(".png");
    for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n)
        path = QStringLiteral("%1-%2.png").arg(base).arg(n);
    return path;
}

QString savePng(const QImage &image, const QString &path) {
    if (image.isNull()) return {};
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Written through a buffer first so an image too large for the worker never reaches the disk.
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) return {};
    if (encoded.size() > kMaxImageBytes) return {};
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    const bool written = file.write(encoded) == encoded.size();
    file.close();
    if (!written) { QFile::remove(path); return {}; }
    return path;
}

bool hasImage(const QMimeData *data) {
    if (!data) return false;
    if (data->hasImage()) return true;
    if (!data->hasUrls()) return false;
    const auto urls = data->urls();
    for (const QUrl &url : urls)
        if (url.isLocalFile() && isImageFile(url.toLocalFile())) return true;
    return false;
}

QStringList fromMimeData(const QMimeData *data, const QString &dir) {
    QStringList paths;
    if (!data) return paths;
    if (data->hasUrls()) {
        const auto urls = data->urls();
        for (const QUrl &url : urls) {
            if (!url.isLocalFile()) continue;
            const QString path = url.toLocalFile();
            // A named file is attached where it is: copying it would hide which file was meant.
            if (isImageFile(path) && !paths.contains(path)) paths << path;
        }
    }
    if (!paths.isEmpty()) return paths;
    if (data->hasImage()) {
        const QImage image = qvariant_cast<QImage>(data->imageData());
        const QString saved = savePng(image, newCapturePath(dir, QStringLiteral("paste"),
                                                            QDateTime::currentDateTime()));
        if (!saved.isEmpty()) paths << saved;
    }
    return paths;
}

int pruneCache(const QString &dir, int days, const QDateTime &now) {
    if (days <= 0) return 0;
    QDir folder(dir);
    if (!folder.exists()) return 0;
    int removed = 0;
    const auto entries = folder.entryInfoList({QStringLiteral("relay-*.png")}, QDir::Files);
    for (const QFileInfo &entry : entries) {
        if (entry.lastModified().daysTo(now) < days) continue;
        if (QFile::remove(entry.absoluteFilePath())) ++removed;
    }
    return removed;
}

}  // namespace images
}  // namespace relay
