// SPDX-License-Identifier: AGPL-3.0-or-later
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
//                     agent_mode, input_mode, session_id, scrollback}}
//
// The `scrollback` id is the pane's own id for what outlives a restart: it names both this pane's
// saved terminal text (scrollback/<id>.txt below) and its prompt-box history
// (prompt-history/<id>.txt, src/PromptHistory.h), which is why the two stores are pruned
// together.
//         | {"explorer"|"preview"|"plan": {"path": "..."}}
//         | {"settings": {"mode": "options"|"actions", "tab"?, "search"?, "row"?}}
//
// The `settings` node is the Actions pane or the Options pane (card #XAME): which it was, the
// section tab it was reading, its search text and its highlighted row, so a reopened window puts
// the pane back where it was. The Switchboard's `board` node and the subagent pane's `subagents`
// node are declared where they are restored (RelayWindow::buildNode).
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

// ----- a saved tab's project (card #JN7X) -------------------------------------------------------
//
// A tab is attached to at most one project, and starts attached to none. An **attached** tab is
// saved as a wrapper around the node it always was:
//
//   tab := node                                    (attached to nothing — unchanged)
//         | {"project": "/abs/path", "node": node} (attached)
//
// so no schema bump: an unattached tab is byte-for-byte the shape every earlier Relay wrote, and
// a layout written by an earlier Relay reads as "every tab unattached", which is the quiet
// default anyway. The two readers below take either shape, and **everything that inspects a
// saved tab goes through `tabNode()` first** — isUsableNode(), usableWindows(), scrollbackIds()
// and ClosedStack's walk. Miss one and a wrapper looks like an unknown node kind, which drops
// every attached tab (and with it, whole windows) on the first restore.
QJsonObject tabNode(const QJsonObject &tab);
// The project an attached tab was saved with; empty for the bare shape.
QString tabProject(const QJsonObject &tab);
// The theme a tab was saved with, when it had one of its own (themes are per tab since 2026-09-19).
// It rides in the same wrapper — {"node", "theme"[, "project"]} — and only when it differs from the
// default, so a layout with no tab themes is byte-for-byte what it was. Empty for the bare shape.
QString tabTheme(const QJsonObject &tab);
// The tab's persistent id (card #FEJQ, protocol §30.7): what its helper worker and that worker's
// persisted conversation are keyed by, so a restart brings each tab's helper back with its own
// history rather than its neighbour's. It rides in the same wrapper as `project` and `theme`, and
// only from the first thing that needs one — a tab nobody has asked anything still saves bare.
// Empty for the bare shape, and for a layout written before #FEJQ; RelayWindow mints a new one.
QString tabId(const QJsonObject &tab);

// A pane tree that can be rebuilt: known node kinds only, non-empty splits, bounded depth. A tab
// in the wrapper shape above is judged by the node inside it.
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

// ----- the terminal scrollback of a saved pane ------------------------------------------------
//
// A restored pane used to come back empty (owner report, 2026-09-18). Its text now travels beside
// the layout: one file per pane in `$XDG_DATA_HOME/relay/state/scrollback/<id>.txt` (0600), named
// after the `scrollback` id the pane node carries, written the same atomic way windows.json is.
// The same id also names the pane's prompt history (src/PromptHistory.h).
//
// **Text, not cells.** What is saved is what `TerminalBackend::scrollbackText()` reports — the
// lines, in order, without colour. Two reasons. The engine hands the host text: cells and their
// SGR would need a new `VtCore` call implemented in both cores, which is a far larger change than
// the loss is worth. And absolute colour does not survive: a replayed `38;2;R;G;B` is burnt into
// the new pane's history, and a terminal cannot recolour its scrollback (src/MarkdownAnsi.h), so
// text saved under one theme would come back in the old theme's colours for good. Plain lines
// take the live theme's foreground instead, and the block is marked as restored where it is
// replayed. If styling is ever worth keeping, save the *indexed* SGR only (the palette entries
// the engine resolves at paint time), never RGB.
//
// The file is bounded twice over — at most kScrollbackMaxLines lines and kScrollbackMaxBytes of
// text, newest kept — so no pane can grow the state directory without limit, and files whose pane
// is gone are pruned whenever the layout is written.
constexpr int kScrollbackMaxLines = 5000;
constexpr qint64 kScrollbackMaxBytes = 512 * 1024;

// `$XDG_DATA_HOME/relay/state/scrollback`. Empty when no data location is available.
QString scrollbackDirectory();
// Ids are pane tokens (a UUID without braces). Anything else is refused, so a hand-edited layout
// cannot point the store at `../` or at a file outside it.
bool isScrollbackId(const QString &id);
// `<scrollbackDirectory()>/<id>.txt`; empty for an unusable id or without a data location.
QString scrollbackPath(const QString &id);

// The tail that is worth keeping: trailing blank lines dropped, then the newest lines within both
// caps (lines first, then bytes, counting one newline per line). Pure; the file side uses it.
QStringList clampScrollback(QStringList lines, int maxLines = kScrollbackMaxLines,
                            qint64 maxBytes = kScrollbackMaxBytes);

// Atomic (temp file + rename) and 0600, like write(). Empty content removes the file rather than
// leaving a stale one behind. False with *error set when the id or the directory is unusable.
bool writeScrollback(const QString &id, const QStringList &lines, QString *error = nullptr);
// The saved lines, oldest first; empty when there is no file. Never reads more than the caps.
QStringList readScrollback(const QString &id, int maxLines = kScrollbackMaxLines);

// Every `scrollback` id in a saved layout's window records (pane nodes at any depth).
QStringList scrollbackIds(const QJsonArray &windows);
// Delete the stored scrollback of every pane that is not in `keep`; returns how many files went.
int pruneScrollback(const QStringList &keep);
// Drop the whole store ("Start a fresh window set", or restoring turned off).
void removeAllScrollback();

}  // namespace windowstate
}  // namespace relay
