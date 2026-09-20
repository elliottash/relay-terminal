// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsCache.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>

namespace relay::settings {
namespace {

// One lock for the lot. relay::log::level() is read from the worker threads as well as the GUI
// thread, and a QSettings object may not be shared between them at all; an uncontended QMutex is
// tens of nanoseconds against the ~15 µs a QSettings construction costs, which is the trade here.
struct Store {
    QMutex mutex;
    QHash<QString, QVariant> values;
    quint64 generation = 1;
    qint64 reads = 0;
};

Store &store() {
    static Store instance;
    return instance;
}

}  // namespace

QVariant value(const QString &key, const QVariant &fallback) {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    const auto found = s.values.constFind(key);
    if (found != s.values.constEnd()) return *found;
    ++s.reads;
    // A key that is not in the file caches its fallback, so an unset setting is not re-read for
    // ever; invalidate() drops it the moment one is written.
    const QVariant read = QSettings().value(key, fallback);
    s.values.insert(key, read);
    return read;
}

bool boolValue(const QString &key, bool fallback) { return value(key, fallback).toBool(); }
int intValue(const QString &key, int fallback) { return value(key, fallback).toInt(); }
QString stringValue(const QString &key, const QString &fallback) { return value(key, fallback).toString(); }

void invalidate() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    s.values.clear();
    ++s.generation;
}

quint64 generation() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    return s.generation;
}

qint64 reads() {
    Store &s = store();
    QMutexLocker lock(&s.mutex);
    return s.reads;
}

}  // namespace relay::settings
