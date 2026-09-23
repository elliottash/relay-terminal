// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ImageCache.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace relay {

namespace {
// A file is stat'ed again at most this often, which is what notices it was replaced or deleted.
constexpr qint64 kRecheckMs = 2000;
// Anything whose header claims more pixels than this is decoded at a reduced size when the format
// can (JPEG), and refused when it cannot: 64 Mpx is 256 MB of ARGB before any scaling.
constexpr qint64 kMaxDecodePixels = qint64(64) << 20;
constexpr int kMaxInfo = 4096;

QSize headerSize(QImageReader &reader)
{
    QSize size = reader.size();
    if (size.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90))
        size.transpose();
    return size;
}
} // namespace

ImageCache::ImageCache()
{
    m_clock.start();
}

ImageCache::~ImageCache()
{
    *m_alive = false;
}

QString ImageCache::keyOf(const QString &path, QSize device)
{
    return QString::number(device.width()) + QLatin1Char('x') + QString::number(device.height()) + QLatin1Char(' ') + path;
}

QImage ImageCache::decode(const QString &path, QSize device)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    reader.setAllocationLimit(int(kMaxDecodePixels * 4 >> 20));
#endif
    const QSize raw = reader.size();   // before the EXIF rotation, which is what setScaledSize takes
    if (raw.isValid() && qint64(raw.width()) * raw.height() > kMaxDecodePixels) {
        if (!reader.supportsOption(QImageIOHandler::ScaledSize))
            return {};
        const double f = std::sqrt(double(kMaxDecodePixels) / (double(raw.width()) * raw.height()));
        reader.setScaledSize(QSize(std::max(1, int(raw.width() * f)), std::max(1, int(raw.height() * f))));
    }
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (image.size() != device)
        image = image.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

ImageCache::Info &ImageCache::info(const QString &path)
{
    auto it = m_info.find(path);
    const qint64 now = m_clock.elapsed();
    if (it != m_info.end() && now - it->checkedAt < kRecheckMs)
        return *it;
    if (it == m_info.end() && m_info.size() >= kMaxInfo)
        m_info.clear();
    const QFileInfo file(path);
    const QDateTime mtime = file.isFile() ? file.lastModified() : QDateTime();
    const qint64 size = file.isFile() ? file.size() : -1;
    if (it == m_info.end()) {
        it = m_info.insert(path, Info());
    } else if (it->mtime == mtime && it->fileSize == size) {
        it->checkedAt = now;
        return *it;
    } else {
        dropPath(path);   // replaced, grown or deleted: what was decoded from it is stale
    }
    Info &in = *it;
    in.mtime = mtime;
    in.fileSize = size;
    in.checkedAt = now;
    in.natural = QSize();
    if (size >= 0) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        in.natural = headerSize(reader);
    }
    in.missing = !in.natural.isValid() || in.natural.isEmpty();
    return in;
}

QSize ImageCache::naturalSize(const QString &path)
{
    const Info &in = info(path);
    return in.missing ? QSize() : in.natural;
}

QImage ImageCache::picture(const QString &path, QSize size, qreal dpr, State *state)
{
    *state = State::Missing;
    if (size.isEmpty())
        return {};
    const Info &in = info(path);
    if (in.missing)
        return {};
    const QSize device(std::max(1, qRound(size.width() * dpr)), std::max(1, qRound(size.height() * dpr)));
    const QString key = keyOf(path, device);
    auto hit = m_entries.find(key);
    if (hit != m_entries.end()) {
        m_lru.splice(m_lru.begin(), m_lru, hit->lru);
        *state = State::Ready;
        QImage image = hit->image;
        image.setDevicePixelRatio(dpr);
        return image;
    }
    if (!m_pending.contains(key)) {
        if (in.fileSize <= m_syncBytes) {
            ++m_decodes;
            const QImage image = decode(path, device);
            if (image.isNull()) {
                m_info[path].missing = true;
                return {};
            }
            insert(path, key, image);
            *state = State::Ready;
            QImage out = image;
            out.setDevicePixelRatio(dpr);
            return out;
        }
        ++m_decodes;
        m_pending.insert(key);
        const std::shared_ptr<bool> alive = m_alive;
        const quint64 generation = m_generation;
        QThreadPool::globalInstance()->start([this, alive, generation, path, device, key] {
            const QImage image = decode(path, device);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [this, alive, generation, path, key, image] {
                if (!*alive)
                    return;
                m_pending.remove(key);
                if (generation != m_generation) {
                    // Cleared meanwhile: this picture may be stale, so it is asked for again.
                } else if (image.isNull()) {
                    auto it = m_info.find(path);
                    if (it != m_info.end())
                        it->missing = true;
                } else {
                    insert(path, key, image);
                }
                if (onReady)
                    onReady();
            }, Qt::QueuedConnection);
        });
    }
    // While the exact size decodes, the closest one already here stands in, stretched.
    *state = State::Pending;
    QImage best;
    qint64 bestDistance = -1;
    for (const QString &other : m_keysOfPath.value(path)) {
        const QImage &candidate = m_entries.value(other).image;
        const qint64 distance = std::llabs(qint64(candidate.width()) * candidate.height() - qint64(device.width()) * device.height());
        if (bestDistance < 0 || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    if (!best.isNull()) {
        *state = State::Ready;
        best.setDevicePixelRatio(qreal(best.width()) / std::max(1, size.width()));
    }
    return best;
}

void ImageCache::insert(const QString &path, const QString &key, const QImage &image)
{
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        m_bytes -= it->image.sizeInBytes();
        m_lru.erase(it->lru);
        m_entries.erase(it);
    }
    m_lru.push_front(key);
    m_entries.insert(key, Entry{image, m_lru.begin()});
    m_keysOfPath[path].insert(key);
    m_bytes += image.sizeInBytes();
    evict();
}

void ImageCache::dropPath(const QString &path)
{
    for (const QString &key : m_keysOfPath.take(path)) {
        auto it = m_entries.find(key);
        if (it == m_entries.end())
            continue;
        m_bytes -= it->image.sizeInBytes();
        m_lru.erase(it->lru);
        m_entries.erase(it);
    }
}

void ImageCache::evict()
{
    // The picture just used is never evicted, even alone over the cap: it is on screen.
    while (m_bytes > m_maxBytes && m_lru.size() > 1) {
        const QString key = m_lru.back();
        m_lru.pop_back();
        auto it = m_entries.find(key);
        if (it == m_entries.end())
            continue;
        m_bytes -= it->image.sizeInBytes();
        const QString path = key.mid(key.indexOf(QLatin1Char(' ')) + 1);
        auto keys = m_keysOfPath.find(path);
        if (keys != m_keysOfPath.end()) {
            keys->remove(key);
            if (keys->isEmpty())
                m_keysOfPath.erase(keys);
        }
        m_entries.erase(it);
    }
}

void ImageCache::setMaxBytes(qint64 bytes)
{
    m_maxBytes = std::max<qint64>(0, bytes);
    evict();
}

void ImageCache::clear()
{
    ++m_generation;
    m_entries.clear();
    m_keysOfPath.clear();
    m_lru.clear();
    m_info.clear();
    m_bytes = 0;
}

} // namespace relay
