// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A media row is an OSC 8 linked U+2800 cell, like an image row (#1MGS).
// The link names a small local JSON manifest written by relay-show (#MDA7).
// The core only stores the link; the view validates and draws the media.

#include <QFileInfo>
#include <QString>
#include <QUrl>

namespace relay::inlinemedia {

constexpr char kPrefix[] = "relay-media:";
constexpr char16_t kRowCell = u'⠀';
constexpr int kMaxRows = 20;
constexpr int kMaxCols = 120;

struct MediaRef {
    QString manifest;
    int row = 0;
    int rows = 1;
    int cols = 1;
};

inline QString mediaUri(const MediaRef &ref)
{
    return QStringLiteral("relay-media:%1/%2/%3/%4")
        .arg(ref.row).arg(ref.rows).arg(ref.cols)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(ref.manifest)));
}

inline bool parseMediaUri(const QString &uri, MediaRef *out = nullptr)
{
    if (!uri.startsWith(QLatin1String(kPrefix)) || uri.size() > 8192)
        return false;
    const QString body = uri.mid(int(sizeof(kPrefix) - 1));
    const int a = body.indexOf(QLatin1Char('/'));
    const int b = a < 0 ? -1 : body.indexOf(QLatin1Char('/'), a + 1);
    const int c = b < 0 ? -1 : body.indexOf(QLatin1Char('/'), b + 1);
    if (a < 1 || b <= a + 1 || c <= b + 1 || c == body.size() - 1)
        return false;
    const auto number = [](const QString &s, int *value) {
        if (s.isEmpty() || s.size() > 3)
            return false;
        for (const QChar ch : s)
            if (ch < QLatin1Char('0') || ch > QLatin1Char('9'))
                return false;
        bool ok = false;
        *value = s.toInt(&ok);
        return ok;
    };
    MediaRef ref;
    if (!number(body.left(a), &ref.row) ||
        !number(body.mid(a + 1, b - a - 1), &ref.rows) ||
        !number(body.mid(b + 1, c - b - 1), &ref.cols) ||
        ref.rows < 1 || ref.rows > kMaxRows || ref.cols < 1 ||
        ref.cols > kMaxCols || ref.row >= ref.rows)
        return false;
    const QString encoded = body.mid(c + 1);
    if (encoded.contains(QLatin1Char('/')))
        return false;
    ref.manifest = QUrl::fromPercentEncoding(encoded.toLatin1());
    if (!QFileInfo(ref.manifest).isAbsolute() || !ref.manifest.endsWith(QStringLiteral(".json")))
        return false;
    for (const QChar ch : ref.manifest)
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return false;
    if (out)
        *out = ref;
    return true;
}

} // namespace relay::inlinemedia
