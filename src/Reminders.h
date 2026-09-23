// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimer>
#include <functional>

namespace relay {

struct Reminder {
    QString id;
    QString text;
    QDateTime due;
};

// One store for all panes. The file lock makes claiming a due reminder atomic across Relay
// processes; a periodic poll also catches reminders made by a different running process.
class Reminders {
public:
    explicit Reminders(const QString &path);
    static Reminders &instance();
    void onDue(std::function<void(const Reminder &)> callback) { m_onDue = std::move(callback); }

    bool add(const QString &text, const QDateTime &due, Reminder *created, QString *error);
    bool list(QList<Reminder> *items, QString *error) const;
    bool cancel(const QString &id, QString *error);
    void checkDue(const QDateTime &now = QDateTime::currentDateTimeUtc());

private:
    bool read(QList<Reminder> *items, QString *error) const;
    bool write(const QList<Reminder> &items, QString *error) const;
    QString m_path;
    QTimer m_poll;
    std::function<void(const Reminder &)> m_onDue;
};

}  // namespace relay
