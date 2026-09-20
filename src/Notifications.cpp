// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Notifications.h"
#include <QSettings>

namespace relay {

const QString NotificationCenter::kindInfo = QStringLiteral("info");
const QString NotificationCenter::kindSuccess = QStringLiteral("success");
const QString NotificationCenter::kindWarning = QStringLiteral("warning");
const QString NotificationCenter::kindError = QStringLiteral("error");

NotificationCenter &NotificationCenter::instance() {
    static NotificationCenter centre;
    return centre;
}

QString NotificationCenter::post(const QString &title, const QString &body, const QString &kind, const QString &source) {
    if (title.trimmed().isEmpty()) return {};
    Notification note;
    note.id = QStringLiteral("n%1").arg(m_nextId++);
    note.at = QDateTime::currentDateTime();
    note.title = title.trimmed();
    note.body = body.trimmed();
    note.kind = kind.isEmpty() ? kindInfo : kind;
    note.source = source;
    m_entries.append(note);
    while (m_entries.size() > kMaxEntries) m_entries.removeFirst();
    Q_EMIT changed();
    return note.id;
}

QString NotificationCenter::postWithAction(const QString &title, const QString &body, const QString &kind,
                                           const QString &source, const QString &actionLabel, const QString &actionId) {
    const QString id = post(title, body, kind, source);
    if (id.isEmpty()) return id;
    for (Notification &note : m_entries) {
        if (note.id != id) continue;
        note.actionLabel = actionLabel;
        note.actionId = actionId;
        break;
    }
    Q_EMIT changed();
    return id;
}

void NotificationCenter::amend(const QString &id, const QString &title, const QString &body,
                               const QString &actionLabel, const QString &actionId,
                               const QString &kind) {
    for (Notification &note : m_entries) {
        if (note.id != id) continue;
        if (!title.trimmed().isEmpty()) note.title = title.trimmed();
        if (!body.trimmed().isEmpty()) note.body = body.trimmed();
        if (!kind.trimmed().isEmpty()) note.kind = kind.trimmed();
        note.actionLabel = actionLabel;
        note.actionId = actionId;
        Q_EMIT changed();
        return;
    }
}

QList<Notification> NotificationCenter::entries() const {
    QList<Notification> newestFirst;
    newestFirst.reserve(m_entries.size());
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) newestFirst.append(*it);
    return newestFirst;
}

int NotificationCenter::count() const { return int(m_entries.size()); }

int NotificationCenter::unseen() const {
    int unread = 0;
    for (const Notification &note : m_entries) if (!note.seen) ++unread;
    return unread;
}

void NotificationCenter::markAllSeen() {
    bool any = false;
    for (Notification &note : m_entries) if (!note.seen) { note.seen = true; any = true; }
    if (any) Q_EMIT changed();
}

void NotificationCenter::markSeen(const QString &id) {
    for (Notification &note : m_entries) {
        if (note.id != id || note.seen) continue;
        note.seen = true;
        Q_EMIT changed();
        return;
    }
}

void NotificationCenter::remove(const QString &id) {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id != id) continue;
        m_entries.removeAt(i);
        Q_EMIT changed();
        return;
    }
}

void NotificationCenter::clear() {
    if (m_entries.isEmpty()) return;
    m_entries.clear();
    Q_EMIT changed();
}

bool NotificationCenter::desktopEnabled() {
    return QSettings().value(QStringLiteral("notifications/desktop"), true).toBool();
}

void NotificationCenter::setDesktopEnabled(bool on) {
    QSettings().setValue(QStringLiteral("notifications/desktop"), on);
}

QString NotificationCenter::relativeTime(const QDateTime &at, const QDateTime &now) {
    if (!at.isValid()) return {};
    const qint64 seconds = at.secsTo(now);
    if (seconds < 45) return QStringLiteral("now");
    if (seconds < 3600) return QStringLiteral("%1 min ago").arg((seconds + 30) / 60);
    if (at.date() == now.date()) return QStringLiteral("%1 h ago").arg(seconds / 3600);
    if (at.daysTo(now) < 7) return at.toString(QStringLiteral("ddd HH:mm"));
    return at.toString(QStringLiteral("d MMM HH:mm"));
}

}  // namespace relay
