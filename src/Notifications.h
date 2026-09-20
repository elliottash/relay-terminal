// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Relay's notification centre: the list behind the bell in the window header.
//
// Anything worth telling the user about after the fact is posted here — a long command that
// finished, an agent turn that ended or failed, a password prompt waiting, a shell killed for
// memory. The bell shows how many of them have not been seen yet; the popup lists them newest
// first and can take the user back to the pane that posted one.
//
// Desktop notifications (notify-send) stay what they always were: only when the window is not
// the active one, and only while "notifications/desktop" is on. The centre itself always keeps
// the entry, so nothing is lost when the user was looking at the window the whole time.
//
// One centre per process, shared by every window. Entries live in memory for the session; the
// oldest are dropped past kMaxEntries.
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

namespace relay {

struct Notification {
    QString id;        // "n12", unique within the session
    QDateTime at;
    QString title;
    QString body;
    QString kind;      // kindInfo / kindSuccess / kindWarning / kindError
    QString source;    // pane session token, so the popup can go back to the pane; may be empty
    bool seen = false;
    // ----- an entry that offers something (card #FEJQ, protocol §30.6) ------------------------
    // An agent changed a setting, and the entry that says so carries the way back: "Agent changed
    // Thinking display · collapse → never · [Undo]". The popup draws `actionLabel` as a button and
    // hands `actionId` back to whoever is listening — a string, never a callback, so the offer
    // outlives the turn, the worker and the popup that drew it. Both empty: no button.
    QString actionLabel, actionId;
};

class NotificationCenter : public QObject {
    Q_OBJECT
public:
    static NotificationCenter &instance();

    static constexpr int kMaxEntries = 200;
    static const QString kindInfo, kindSuccess, kindWarning, kindError;

    // Adds an entry and returns its id. An empty title is ignored (returns an empty id).
    QString post(const QString &title, const QString &body = QString(),
                 const QString &kind = kindInfo, const QString &source = QString());

    // The same, with a button on the entry (see Notification::actionLabel). The caller decides
    // what `actionId` means and listens for it on the popup.
    QString postWithAction(const QString &title, const QString &body, const QString &kind,
                           const QString &source, const QString &actionLabel, const QString &actionId);

    // Rewrite an entry that is already in the list: the offer it made has been taken, so it says
    // what happened instead of offering it again ("Undone: Thinking display"). An empty title or
    // body leaves that field as it was; the action is always replaced, so empty strings take the
    // button away. Unknown ids are ignored.
    void amend(const QString &id, const QString &title, const QString &body,
               const QString &actionLabel, const QString &actionId);

    // Newest first.
    QList<Notification> entries() const;
    int count() const;
    int unseen() const;

    void markAllSeen();
    // One entry — the one a jump landed on (#NQP9) — rather than every entry the popup saw.
    // Emits changed only when the entry actually turned seen.
    void markSeen(const QString &id);
    void remove(const QString &id);
    void clear();

    // notify-send while the window is not active ("notifications/desktop", on by default).
    static bool desktopEnabled();
    static void setDesktopEnabled(bool on);

    // "now", "4 min ago", "2 h ago", "Tue 09:15" — the popup's timestamps.
    static QString relativeTime(const QDateTime &at, const QDateTime &now = QDateTime::currentDateTime());

Q_SIGNALS:
    // The list changed: posted, seen, removed or cleared.
    void changed();

private:
    QList<Notification> m_entries;   // newest last, so appending is cheap
    int m_nextId = 1;
};

}  // namespace relay
