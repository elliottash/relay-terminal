// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Decoded pictures for the image rows the view paints (card #1MGS). See core/InlineImage.h for the
// contract.
//
// GUI thread only. Pictures are kept already scaled to the size they are painted at, so a paint
// is one drawImage and never a smooth scale. The key is the path and that size in device pixels;
// a pane that is resized asks for a new size, and until it has been decoded the nearest size the
// cache already holds for that path stands in, stretched.
//
// A small file is decoded on the spot. A bigger one is decoded and scaled on the thread pool, and
// `onReady` runs on the GUI thread when it is done (the view repaints). A path is never decoded
// twice at once, and a file that cannot be read is remembered as such until its mtime changes, so
// a missing picture costs one stat every few seconds, not one decode per frame.
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QString>

#include <functional>
#include <list>
#include <memory>

namespace relay {

class ImageCache {
public:
    enum class State {
        Ready,    // the picture is here (maybe a stand-in at another size while the exact one decodes)
        Pending,  // being decoded, nothing to paint yet
        Missing,  // no such file, or not a picture Qt can read
    };

    ImageCache();
    ~ImageCache();
    ImageCache(const ImageCache &) = delete;
    ImageCache &operator=(const ImageCache &) = delete;

    // The picture's size in pixels as its header says, without decoding it. Invalid when the file
    // is missing or unreadable.
    QSize naturalSize(const QString &path);

    // The picture at `path` scaled to `size` (logical pixels) for a device pixel ratio of `dpr`.
    // The image has its devicePixelRatio set; paint it into a QRect of `size`.
    QImage picture(const QString &path, QSize size, qreal dpr, State *state);

    // Called on the GUI thread when a background decode finishes (successfully or not).
    std::function<void()> onReady;

    // Files up to this size are decoded synchronously; bigger ones on the thread pool.
    void setSyncBytes(qint64 bytes) { m_syncBytes = bytes; }
    void setMaxBytes(qint64 bytes);
    qint64 bytes() const { return m_bytes; }
    // How many decodes ran, for tests: the same picture at the same size decodes once.
    quint64 decodeCount() const { return m_decodes; }
    void clear();

    // Decodes `path` scaled to `device` pixels, refusing anything absurd. Thread-safe.
    static QImage decode(const QString &path, QSize device);

private:
    struct Info {
        QDateTime mtime;
        qint64 fileSize = 0;
        QSize natural;
        bool missing = false;
        qint64 checkedAt = 0;   // ms since the cache's clock started
    };
    struct Entry {
        QImage image;
        std::list<QString>::iterator lru;
    };
    Info &info(const QString &path);
    void insert(const QString &path, const QString &key, const QImage &image);
    void dropPath(const QString &path);
    void evict();
    static QString keyOf(const QString &path, QSize device);

    // Read on the GUI thread by a finished decode before it touches the cache, so a decode that
    // outlives its view is dropped rather than written into freed memory.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    QElapsedTimer m_clock;
    QHash<QString, Info> m_info;
    QHash<QString, Entry> m_entries;       // keyOf(path, device size) -> scaled picture
    QHash<QString, QSet<QString>> m_keysOfPath;
    std::list<QString> m_lru;              // most recently used first
    QSet<QString> m_pending;               // keys being decoded on the pool
    qint64 m_bytes = 0;
    qint64 m_maxBytes = qint64(256) << 20;
    qint64 m_syncBytes = qint64(256) << 10;
    quint64 m_decodes = 0;
    quint64 m_generation = 0;              // bumped by clear(), so a stale decode is dropped
};

} // namespace relay
