// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Reminders.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace relay {

Reminders::Reminders(const QString &path) : m_path(path) {
    m_poll.setInterval(15000);
    QObject::connect(&m_poll, &QTimer::timeout, [this] { checkDue(); });
    m_poll.start();
    QTimer::singleShot(0, &m_poll, [this] { checkDue(); });
}

Reminders &Reminders::instance() {
    static Reminders store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                           + QStringLiteral("/reminders.json"));
    return store;
}

bool Reminders::read(QList<Reminder> *items, QString *error) const {
    items->clear();
    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot read the reminders file.");
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error) *error = QStringLiteral("The reminders file is damaged; no reminders were changed.");
        return false;
    }
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject row = value.toObject();
        const QDateTime due = QDateTime::fromString(row.value(QStringLiteral("due")).toString(), Qt::ISODateWithMs);
        if (row.value(QStringLiteral("id")).toString().isEmpty() || !due.isValid()) {
            if (error) *error = QStringLiteral("The reminders file has an invalid entry; no reminders were changed.");
            return false;
        }
        items->append({row.value(QStringLiteral("id")).toString(),
                       row.value(QStringLiteral("text")).toString(), due});
    }
    return true;
}

bool Reminders::write(const QList<Reminder> &items, QString *error) const {
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        if (error) *error = QStringLiteral("Cannot create the reminders folder.");
        return false;
    }
    QJsonArray rows;
    for (const Reminder &item : items)
        rows.append(QJsonObject{{QStringLiteral("id"), item.id}, {QStringLiteral("text"), item.text},
                                {QStringLiteral("due"), item.due.toUTC().toString(Qt::ISODateWithMs)}});
    QSaveFile file(m_path);
    const QByteArray data = QJsonDocument(rows).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Cannot save reminders.");
        return false;
    }
    return true;
}

bool Reminders::add(const QString &text, const QDateTime &due, Reminder *created, QString *error) {
    if (text.trimmed().isEmpty() || text.size() > 2000 || !due.isValid()
        || due <= QDateTime::currentDateTimeUtc()) {
        if (error) *error = QStringLiteral("Give reminder text and a future time.");
        return false;
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(2000)) { if (error) *error = QStringLiteral("Reminders are busy; try again."); return false; }
    QList<Reminder> items;
    if (!read(&items, error)) return false;
    if (items.size() >= 100) { if (error) *error = QStringLiteral("At most 100 reminders may be pending."); return false; }
    const Reminder item{QUuid::createUuid().toString(QUuid::WithoutBraces), text.trimmed(), due.toUTC()};
    items.append(item);
    if (!write(items, error)) return false;
    if (created) *created = item;
    return true;
}

bool Reminders::list(QList<Reminder> *items, QString *error) const {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(2000)) { if (error) *error = QStringLiteral("Reminders are busy; try again."); return false; }
    if (!read(items, error)) return false;
    std::sort(items->begin(), items->end(), [](const Reminder &a, const Reminder &b) { return a.due < b.due; });
    return true;
}

bool Reminders::cancel(const QString &id, QString *error) {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(2000)) { if (error) *error = QStringLiteral("Reminders are busy; try again."); return false; }
    QList<Reminder> items;
    if (!read(&items, error)) return false;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).id != id) continue;
        items.removeAt(i);
        return write(items, error);
    }
    if (error) *error = QStringLiteral("No pending reminder has that id.");
    return false;
}

void Reminders::checkDue(const QDateTime &now) {
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(100)) return;
    QList<Reminder> items;
    QString error;
    if (!read(&items, &error)) return;
    QList<Reminder> pending, due;
    for (const Reminder &item : items) (item.due <= now ? due : pending).append(item);
    if (due.isEmpty() || !write(pending, &error)) return;
    lock.unlock();  // Claim is durable before an alert can fire, including in another process.
    for (const Reminder &item : due) if (m_onDue) m_onDue(item);
}

}  // namespace relay
