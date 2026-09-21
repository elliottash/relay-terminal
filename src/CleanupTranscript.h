// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace relay::board {

// Tool calls and agent prose already use the console's ordinary renderer. These are the
// cleanup-specific events that otherwise live only in the board's transient notice/panel.
inline QString cleanupTranscriptNote(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString();
    if (type == QStringLiteral("board_cleanup_started")) {
        const QString mode = event.value(QStringLiteral("dry_run")).toBool()
            ? QStringLiteral("Cleanup preview · nothing will be written")
            : QStringLiteral("Cleanup · applying changes");
        return QStringLiteral("%1 · reading %2 cards\n")
            .arg(mode).arg(event.value(QStringLiteral("cards")).toInt());
    }
    if (type == QStringLiteral("board_activity") && event.value(QStringLiteral("cleanup")).toBool()) {
        const QString card = event.value(QStringLiteral("id")).toString();
        const QString target = card.isEmpty() ? QStringLiteral("board") : QStringLiteral("#") + card;
        return QStringLiteral("◆ %1 · %2\n").arg(target, event.value(QStringLiteral("summary")).toString());
    }
    if (type != QStringLiteral("board_cleanup_summary"))
        return {};
    const bool dry = event.value(QStringLiteral("dry_run")).toBool();
    const QString outcome = event.value(QStringLiteral("outcome")).toString();
    const QString state = outcome == QStringLiteral("cancelled") ? QStringLiteral("stopped")
        : outcome == QStringLiteral("error") ? QStringLiteral("failed") : QStringLiteral("finished");
    const auto counts = event.value(QStringLiteral("counts")).toObject();
    QStringList lines;
    lines << QStringLiteral("%1 %2 · %3 %4%5")
        .arg(dry ? QStringLiteral("Cleanup preview") : QStringLiteral("Cleanup"), state)
        .arg(counts.value(dry ? QStringLiteral("proposed") : QStringLiteral("writes")).toInt())
        .arg(dry ? QStringLiteral("proposed") : QStringLiteral("written"),
             dry ? QStringLiteral(" · nothing was written") : QString());
    for (const auto &value : event.value(QStringLiteral("refusals")).toArray()) {
        const auto refusal = value.toObject();
        lines << QStringLiteral("Refused %1: %2").arg(refusal.value(QStringLiteral("tool")).toString(),
                                                      refusal.value(QStringLiteral("error")).toString());
    }
    const QString changelog = event.value(QStringLiteral("changelog")).toString();
    if (!changelog.isEmpty())
        lines << QStringLiteral("Changelog: ") + changelog;
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace relay::board
