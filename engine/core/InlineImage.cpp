// SPDX-License-Identifier: AGPL-3.0-or-later
#include "InlineImage.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace relay {
namespace inlineimage {

QString imageUri(const ImageRef &ref)
{
    return QLatin1String(kImagePrefix) + QString::number(ref.row) + QLatin1Char('/') + QString::number(ref.rows)
           + QLatin1Char('/') + QString::number(ref.cols) + QLatin1Char('/')
           + QString::fromLatin1(QUrl::toPercentEncoding(ref.path, "/"));
}

bool parseImageUri(const QString &uri, ImageRef *out)
{
    if (!uri.startsWith(QLatin1String(kImagePrefix)))
        return false;
    const QString rest = uri.mid(int(sizeof(kImagePrefix)) - 1);
    // Three numbers, then the path, which may itself contain '/'.
    int at = 0;
    int numbers[3] = {0, 0, 0};
    for (int &n : numbers) {
        const int slash = rest.indexOf(QLatin1Char('/'), at);
        if (slash <= at)
            return false;
        bool ok = false;
        n = rest.mid(at, slash - at).toInt(&ok);
        if (!ok)
            return false;
        at = slash + 1;
    }
    const QString path = QUrl::fromPercentEncoding(rest.mid(at).toLatin1());
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return false;
    ImageRef ref;
    ref.path = path;
    ref.rows = std::clamp(numbers[1], 1, kMaxRows);
    ref.cols = std::clamp(numbers[2], 1, kMaxCols);
    ref.row = std::clamp(numbers[0], 0, ref.rows - 1);
    if (out)
        *out = ref;
    return true;
}

QSize cellsFor(QSize pixels, QSize cellPixels, int maxCols, int maxRows, QSize requested)
{
    const int cw = std::max(1, cellPixels.width()), ch = std::max(1, cellPixels.height());
    maxCols = std::clamp(maxCols, 1, kMaxCols);
    maxRows = std::clamp(maxRows, 1, kMaxRows);
    const double pw = std::max(1, pixels.width()), ph = std::max(1, pixels.height());
    // Width and height in cells, as fractions, before fitting.
    double cols = pw / cw, rows = ph / ch;
    if (requested.width() > 0 && requested.height() > 0) {
        cols = requested.width();
        rows = requested.height();
    } else if (requested.width() > 0) {
        cols = requested.width();
        rows = cols * cw * (ph / pw) / ch;
    } else if (requested.height() > 0) {
        rows = requested.height();
        cols = rows * ch * (pw / ph) / cw;
    }
    const double scale = std::min({1.0, maxCols / cols, maxRows / rows});
    cols *= scale;
    rows *= scale;
    return QSize(std::clamp(int(std::ceil(cols - 1e-6)), 1, maxCols), std::clamp(int(std::ceil(rows - 1e-6)), 1, maxRows));
}

QByteArray placementBytes(const QString &path, QSize cells, bool moveCursor, int startCol)
{
    const int rows = std::clamp(cells.height(), 1, kMaxRows);
    const int cols = std::clamp(cells.width(), 1, kMaxCols);
    const QByteArray cell = QString(QChar(kRowCell)).toUtf8();
    QByteArray out;
    if (!moveCursor)
        out += "\x1b" "7";
    for (int row = 0; row < rows; ++row) {
        out += "\x1b]8;;";
        out += imageUri({path, row, rows, cols}).toUtf8();
        out += "\x1b\\";
        out += cell;
        out += "\x1b]8;;\x1b\\";
        if (row + 1 >= rows)
            continue;
        if (startCol < 0)
            out += "\b\n";
        else if (startCol == 0)
            out += "\r\n";
        else
            out += "\r\n\x1b[" + QByteArray::number(startCol) + 'C';
    }
    if (!moveCursor)
        out += "\x1b" "8";
    else if (cols > 1)
        out += "\x1b[" + QByteArray::number(cols - 1) + 'C';
    return out;
}

QString imageCacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString dir = base + QStringLiteral("/relay/images/terminal");
    QDir().mkpath(dir);
    return dir;
}

QString storeImageBytes(const QByteArray &encoded, const QString &suffix)
{
    if (encoded.isEmpty())
        return {};
    const QString name = QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex().left(32));
    const QString path = imageCacheDir() + QLatin1Char('/') + name + QLatin1Char('.') + suffix;
    if (QFileInfo(path).size() == encoded.size()) {
        // Touch it, so pruning counts from its last use rather than its first.
        QFile file(path);
        if (file.open(QIODevice::ReadWrite))
            file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
        return path;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() || !file.commit())
        return {};
    return path;
}

void pruneImageCache(int days, qint64 maxBytes)
{
    QDir dir(imageCacheDir());
    QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed); // oldest first
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    qint64 total = 0;
    for (const QFileInfo &f : files)
        total += f.size();
    for (const QFileInfo &f : files) {
        if (f.lastModified() >= cutoff && total <= maxBytes)
            break;
        if (QFile::remove(f.absoluteFilePath()))
            total -= f.size();
    }
}

} // namespace inlineimage
} // namespace relay
