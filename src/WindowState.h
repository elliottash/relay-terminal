// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Saved window layout ("reopen where I left off").
//
// Relay writes one layout file per user, `$XDG_DATA_HOME/relay/state/windows.json` (0600), holding
// every open window: geometry + screen, its tabs (order, current, titles) and each tab's pane tree.
// The pane tree uses exactly the node shapes RelayWindow::serializeNode / buildNode already use for
// "restore last closed pane/tab/window", so there is only ever one layout format:
//
//   node := {"split": "h"|"v", "children": [node, ...], "sizes": [int, ...]}
//         | {"pane": {cwd, workspace, engine, engine_core, agent_role, preset, model, effort,
//                     agent_mode, input_mode, session_id}}
//         | {"explorer"|"preview"|"plan": {"path": "..."}}
//
// This header holds the parts that do not need a window: reading and writing the file, clamping a
// window onto a screen that still exists, falling back when a directory is gone, and dropping
// records that could not be rebuilt. main.cpp keeps the widget side.
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace relay {
namespace windowstate {

// Bumped when the file's meaning changes. A file with a different version is ignored (a newer
// Relay must never be fed an older layout, and vice versa) and replaced on the next save.
constexpr int kSchemaVersion = 1;

// How deep a pane tree may nest before the file is treated as corrupt. Guards buildNode()
// against a hand-edited or damaged file.
constexpr int kMaxDepth = 24;

// `$XDG_DATA_HOME/relay/state/windows.json`. Empty when no data location is available.
QString defaultPath();
// The directory of defaultPath(), created 0700 on demand.
QString defaultDirectory();
// `$XDG_RUNTIME_DIR/relay/windows.lock`, the state file's owner lock; falls back next to the
// state file when there is no runtime directory.
QString defaultLockPath();

// {"version": kSchemaVersion, "saved": <unix seconds>, "windows": [...]}.
QJsonObject document(const QJsonArray &windows, qint64 savedAt = 0);
QJsonArray windowsOf(const QJsonObject &document);

// One window record. An invalid geometry is left out; `titles` is informational (Relay derives tab
// titles from the panes) and is stored so the file reads well and tooling can show it.
QJsonObject windowRecord(const QRect &geometry, const QString &screen, const QJsonArray &tabs,
                         int current, const QStringList &titles = {});
QRect geometryOf(const QJsonObject &window);
QString screenOf(const QJsonObject &window);
QJsonArray tabsOf(const QJsonObject &window);
int currentOf(const QJsonObject &window);
QStringList titlesOf(const QJsonObject &window);

// Atomic (temp file + rename) and 0600, so a reader never sees half a file and a crash cannot
// leave a truncated one. Creates the parent directory 0700.
bool write(const QString &path, const QJsonObject &state, QString *error = nullptr);

// Reads and validates. A missing file yields an empty object with no error; an unreadable,
// malformed or foreign-version file yields an empty object and a one-line message in *error.
QJsonObject read(const QString &path, QString *error = nullptr);

// A pane tree that can be rebuilt: known node kinds only, non-empty splits, bounded depth.
bool isUsableNode(const QJsonObject &node, int depth = 0);
// The windows of a document that are worth rebuilding (at least one usable tab each); unusable
// tabs inside a kept window are dropped.
QJsonArray usableWindows(const QJsonObject &document);

// A screen as Qt reports it. `available` is availableGeometry().
struct Screen {
    QString name;
    QRect available;
};

// Put `geometry` back onto a screen that still exists: the one it was saved on when that name is
// still there, otherwise the screen holding its centre, otherwise the first one. The result is
// never larger than the screen and never off it. An invalid geometry, or no screens at all, is
// returned unchanged so the caller can let the window manager place the window.
QRect clampToScreens(const QRect &geometry, const QString &screenName, const QList<Screen> &screens);

// A pane's directory after a restart: its own cwd, else its workspace, else $HOME. `home` is used
// as the last resort and returned even when it does not exist (there is nothing better).
QString resolveDirectory(const QString &cwd, const QString &workspace, const QString &home);

}  // namespace windowstate
}  // namespace relay
