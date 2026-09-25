// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Recently closed panes, tabs and windows.
//
// Closing something in Relay is undoable: every closed item is kept, newest last, until the owner
// drops it or clears the list (owner, 2026-09-25: "remove the max 25 cap"), and
// `closed.restore` brings the newest back while the "Recently closed" list offers any of them. The
// list outlives the process (owner, 2026-09-18: "persist it and save it"), in
// `$XDG_DATA_HOME/relay/state/closed.json` (0600), beside the saved window layout and written the
// same atomic way. It is a separate file on purpose: windows.json stays "the one layout that comes
// back on start", and this is the history of what was closed on the way there.
//
// A record carries the same layout nodes the saved layout uses (src/WindowState.h), so whatever
// restores a saved window restores a closed one, and a pane node's `scrollback` id names the
// terminal text written when the pane was closed. What comes back is the layout, the directories,
// that text and the conversation (`session_id`); the shell itself is new, and programs that were
// running in it are gone.
//
// This header is the part that needs no window: the record, its JSON, the file, and the
// words a list shows for a record. RelayWindow.h keeps the live side (which window, which sibling).
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace relay {
namespace closed {

// Bumped when the file's meaning changes; a file with another version is ignored and replaced.
constexpr int kSchemaVersion = 1;

struct Record {
    enum Kind { Pane, Tab, Window };
    Kind kind = Pane;
    QString id;                          // a UUID without braces; names the record across reloads
    qint64 closedAt = 0;                 // milliseconds since the epoch
    // Pane: how it sat beside the pane that took its focus, and the divider positions of the
    // splitter it left (`slot` is its own index in `sizes`), so it comes back the size it was.
    Qt::Orientation orientation = Qt::Horizontal;
    bool before = false;
    QList<int> sizes;
    int slot = -1;
    int index = 0;                       // Tab: its position; Window: the current tab
    QJsonObject layout;                  // Pane / Tab: a layout node
    QJsonArray tabs;                     // Window: one layout node per tab
    QRect geometry;                      // Window
    // Names the owner gave by hand (/rename-tab): one entry for a Tab, one per tab for a Window,
    // empty where the tab was labelled automatically.
    QStringList tabNames;
    // What each leaf was called when it closed, in layout order. Only ever shown; a restored pane
    // gets its title back from its conversation.
    QStringList titles;
};

// One leaf of a record, for a list to show.
struct PaneInfo {
    QString kind;        // "pane", "explorer", "preview", "plan", "board", ...
    QString title;       // may be empty
    QString cwd;         // a pane's directory, or a tool pane's path
    QString sessionId;   // the conversation that comes back with it; empty for a plain shell
    QString scrollback;  // the id of its saved terminal text (windowstate::readScrollback)
};
struct TabInfo {
    QString name;        // hand-set name, may be empty
    QList<PaneInfo> panes;
};

// `$XDG_DATA_HOME/relay/state/closed.json`. Empty when no data location is available.
QString defaultPath();

QJsonObject toJson(const Record &record);
// False for a record that could not be rebuilt: unknown kind, or no usable layout node
// (windowstate::isUsableNode). A Window keeps its usable tabs and drops the rest, names included.
bool fromJson(const QJsonObject &object, Record *record);

// {"version": kSchemaVersion, "saved": <unix seconds>, "closed": [record, ...]}, oldest first.
QJsonObject document(const QList<Record> &records, qint64 savedAt = 0);

// A missing file is an empty list and no error; an unreadable, malformed or foreign-version file
// is an empty list and a one-line message. Unusable records are dropped; every other one is returned.
QList<Record> load(const QString &path, QString *error = nullptr);
// Atomic and 0600 (windowstate::write). An empty list removes the file.
bool save(const QString &path, const QList<Record> &records, QString *error = nullptr);

// Append, and when `maxItems` is positive keep only the newest that many. Returns the records that
// fell off the front. The list itself is uncapped: nothing passes a limit.
QList<Record> push(QList<Record> *records, Record record, int maxItems = 0);

// Every `scrollback` id the records name: the terminal text that must outlive the layout's prune.
QStringList scrollbackIds(const QList<Record> &records);
// Every `session_id` a record names, in layout order.
QStringList sessionIds(const Record &record);
// The record with `session_id` removed from the panes whose conversation is in `open`: a
// conversation that is already open in another pane must not be resumed a second time.
Record withoutSessions(const Record &record, const QStringList &open, int *removed = nullptr);

// The record's leaves, tab by tab. A Pane or Tab record is one tab.
QList<TabInfo> contents(const Record &record);

// A list's filter: every word of `needle` is somewhere in the record's label, tab names, pane
// titles or directories (case-insensitive). An empty needle matches everything.
bool matches(const Record &record, const QString &needle);

// "Pane", "Tab" or "Window".
QString kindName(Record::Kind kind);
// The one line a list shows: what it was called, else where it was.
//   Pane   → its title, else its directory's name
//   Tab    → its hand-set name, else its panes' titles joined, then " · 3 panes"
//   Window → "4 tabs · 7 panes"
QString label(const Record &record);
// Where it was: the first directory, `home` shortened to "~". Empty when no leaf has one.
QString place(const Record &record, const QString &home);
// "just now", "5 min ago", "3 h ago", "yesterday", "4 days ago", then the date (yyyy-MM-dd).
QString age(qint64 closedAt, qint64 now);

}  // namespace closed
}  // namespace relay
