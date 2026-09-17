// SPDX-License-Identifier: GPL-3.0-or-later
#include "Hints.h"
#include <QDateTime>
#include <QSettings>

namespace relay {

ShortcutHints &ShortcutHints::instance() {
    static ShortcutHints hints;
    return hints;
}

bool ShortcutHints::enabled() const {
    return QSettings().value(QStringLiteral("hints/enabled"), true).toBool();
}

void ShortcutHints::setEnabled(bool on) {
    QSettings().setValue(QStringLiteral("hints/enabled"), on);
}

int ShortcutHints::shownCount(const QString &id) const {
    return QSettings().value(QStringLiteral("hints/count/") + id, 0).toInt();
}

bool ShortcutHints::shouldShow(const QString &id, int limit, int cooldownSeconds) {
    if (!enabled() || id.isEmpty()) return false;
    QSettings settings;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now - settings.value(QStringLiteral("hints/last_any"), 0).toLongLong() < kGlobalGapSeconds) return false;
    const int count = settings.value(QStringLiteral("hints/count/") + id, 0).toInt();
    if (limit > 0 && count >= limit) return false;
    if (now - settings.value(QStringLiteral("hints/last/") + id, 0).toLongLong() < cooldownSeconds) return false;
    settings.setValue(QStringLiteral("hints/count/") + id, count + 1);
    settings.setValue(QStringLiteral("hints/last/") + id, now);
    settings.setValue(QStringLiteral("hints/last_any"), now);
    return true;
}

void ShortcutHints::resetAll() {
    QSettings settings;
    settings.remove(QStringLiteral("hints/count"));
    settings.remove(QStringLiteral("hints/last"));
    settings.remove(QStringLiteral("hints/last_any"));
    settings.remove(QStringLiteral("hints/idle_index"));
}

QString ShortcutHints::nextTime(const QString &shortcut, const QString &what) {
    if (shortcut.isEmpty()) return {};
    return what.isEmpty() ? QStringLiteral("Next time: %1").arg(shortcut)
                          : QStringLiteral("Next time: %1 · %2").arg(shortcut, what);
}

ShortcutHints::Tip ShortcutHints::nextIdleTip(const QList<Tip> &tips) {
    if (tips.isEmpty()) return {};
    QSettings settings;
    int index = settings.value(QStringLiteral("hints/idle_index"), 0).toInt();
    for (int step = 0; step < tips.size(); ++step) {
        const Tip &tip = tips.at((index + step) % tips.size());
        if (shownCount(tip.id) < 3) {
            if (!shouldShow(tip.id, 3, 1800)) return {};
            settings.setValue(QStringLiteral("hints/idle_index"), (index + step + 1) % tips.size());
            return tip;
        }
    }
    return {};
}

}  // namespace relay
