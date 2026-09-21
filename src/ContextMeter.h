// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <algorithm>
#include <cmath>

namespace relay::context {
// Display accounting only. Relay's transcript budget still governs its own compaction.
struct Reading {
    qint64 used = -1, window = 0, limit = 0;
    double percent = -1;
    bool estimated = false;
    QString guest;

    static Reading fromEvent(const QJsonObject &event) {
        Reading r;
        r.guest = event.value(QStringLiteral("guest")).toString();
        const auto data = r.guest.isEmpty() ? event : event.value(QStringLiteral("guest_context")).toObject();
        r.used = data.contains(QStringLiteral("used_tokens"))
            ? data.value(QStringLiteral("used_tokens")).toVariant().toLongLong() : -1;
        r.window = data.value(QStringLiteral("window")).toVariant().toLongLong();
        if (r.guest.isEmpty()) {
            r.limit = data.value(QStringLiteral("limit_tokens")).toVariant().toLongLong();
            r.estimated = data.value(QStringLiteral("estimated")).toBool();
        }
        const auto share = data.value(QStringLiteral("percent"));
        if (share.isDouble() && std::isfinite(share.toDouble()) && share.toDouble() >= 0)
            r.percent = std::clamp(share.toDouble(), 0.0, 100.0);
        else if (r.window > 0 && data.value(QStringLiteral("used_tokens")).isDouble() && r.used >= 0)
            r.percent = std::clamp(100.0 * r.used / r.window, 0.0, 100.0);
        return r;
    }

    QString label() const {
        if (percent < 0) return QStringLiteral("context unknown");
        const double left = std::clamp(100.0 - percent, 0.0, 100.0);
        return QStringLiteral("%1% left").arg(QString::number(left, 'f', left < 10 ? 1 : 0));
    }
    QString tooltip() const {
        const QString numbers = window > 0 && percent >= 0 && used >= 0
            ? QStringLiteral("%1 of %2 tokens%3").arg(QLocale().toString(used), QLocale().toString(window),
                                                     estimated ? QStringLiteral(" (estimated)") : QString())
            : window > 0 ? QStringLiteral("%1-token window; usage not reported yet").arg(QLocale().toString(window))
                         : QStringLiteral("Context usage has not been reported yet");
        if (!guest.isEmpty())
            return numbers + QStringLiteral("\n%1 manages compaction.").arg(
                guest == QStringLiteral("claude") ? QStringLiteral("Claude Code") : QStringLiteral("Codex"));
        return numbers + QStringLiteral("\nAuto-compacts at %1 tokens").arg(QLocale().toString(limit));
    }
};
} // namespace relay::context
