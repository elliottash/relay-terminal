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
//                     agent_mode, input_mode, session_id, scrollback, queue?}}
// `queue` is an ordered array of pending pane-side prompts/commands. Restored rows are paused
// until the person resumes them; old layouts without it have an empty queue.
//
// The `scrollback` id is the pane's own id for what outlives a restart: it names both this pane's
// saved terminal text (scrollback/<id>.txt below) and its prompt-box history
// (prompt-history/<id>.txt, src/PromptHistory.h), which is why the two stores are pruned
// together.
//         | {"explorer"|"preview"|"plan": {"path": "..."}}
//         | {"settings": {"mode": "options"|"actions", "tab"?, "search"?, "row"?}}
//         | {"sessions": {"cwd": "...", "tab"?, "query"?}}
//
// The `settings` node is the Actions pane or the Options pane (card #XAME): which it was, the
// section tab it was reading, its search text and its highlighted row, so a reopened window puts
// the pane back where it was. The `sessions` node is the Sessions & Projects pane (card #8EXS):
// the tab it was left on ("projects"|"globals"; omitted for the default "sessions" list) and its
// search text; it comes back beside the first terminal pane of the tab it lands in, the way the
// models pane does. The Switchboard's `board` node and the subagent pane's `subagents` node are
// declared where they are restored (RelayWindow::buildNode).
//
// This header holds the parts that do not need a window: reading and writing the file, clamping a
// window onto a screen that still exists, falling back when a directory is gone, and dropping
// records that could not be rebuilt. main.cpp keeps the widget side.
#include "TerminalBackend.h"  // relay::ProseBlock, relay::FoldLine: the prose trailer (#MTCS)

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

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
// **Text, with SGR where the engine can give it.** What is saved is what
// `TerminalBackend::formattedScrollbackText()` / `formattedScreenText()` report: the lines, in
// order, with their attributes and colours as ANSI SGR (card #VJDD). Engines that cannot
// reconstruct escape sequences fall back to the plain `scrollbackText()` / `screenText()` and the
// block is still readable. Only SGR is serialized — no cursor movement, and no OSC but the OSC 8
// links of inline image and media rows (`relay-image:` and `relay-media:` URIs), so a restored pane
// draws them again, and of prose blocks (`relay://prose/`, #MTCS), so a restored pane can anchor
// its own word-wrapped output again. Replay (relay::restorableAnsi) filters the file down to CSI
// SGR and those links; a hand-edited or truncated file cannot drive the terminal.
//
// The tradeoff is colour and theme: an indexed colour (0-255) is resolved by the palette in force
// when it is painted, so it follows a palette-based theme change, but an RGB colour
// (`38;2;R;G;B`) is burnt into the new pane's history and cannot be recoloured by a theme switch
// (src/MarkdownAnsi.h). Restored output therefore keeps the colours it was printed in rather than
// taking the live theme's foreground; a program that wants theme-following colour should use the
// indexed palette.
//
// The file is bounded twice over — at most kScrollbackMaxLines lines and kScrollbackMaxBytes of
// text, newest kept — so no pane can grow the state directory without limit, and files whose pane
// is gone are pruned whenever the layout is written.
constexpr int kScrollbackMaxLines = 5000;
constexpr qint64 kScrollbackMaxBytes = 512 * 1024;

// The rows carry the OSC 8 runs of Relay's own word-wrapped output (kProsePrefix, #R2WQ), so a
// restored pane can find each block's rows again; and after the rows the file carries a prose
// trailer (#MTCS) — one separator line no serialized row can equal, then one JSON record per
// block with the block's *logical* lines and the width they were wrapped at. A pane that
// restores hands the records back through TerminalBackend::setProseBlock(), so resizing it
// re-wraps the blocks instead of leaving rows hard-wrapped at the width they were saved at. The
// trailer costs the same byte cap as the rows do; a file without one (an older Relay, a
// hand-edit) restores exactly as before. A hand-edited trailer is inert paint: it can only
// register blocks under prose URIs whose rows are themselves in the file, with text, SGR and
// link targets no worse than the rows already carry.
constexpr int kScrollbackMaxProseBlocks = 512;
// The separator: an APC sequence (ESC _ ... ESC \). Saved rows contain printable text, CSI SGR
// and OSC 8 only (engine/core/AnsiSerializer), so no serialized row can be this line.
QString proseTrailerSeparator();

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
// `prose` (the pane's word-wrapped blocks, TerminalBackend::proseBlocks()) is written as the
// file's trailer and bounded by the same byte cap as the rows.
bool writeScrollback(const QString &id, const QStringList &lines,
                     const QVector<ProseBlock> &prose = {}, QString *error = nullptr);
// The saved lines, oldest first; empty when there is no file. Never reads more than the caps.
QStringList readScrollback(const QString &id, int maxLines = kScrollbackMaxLines);
// The prose trailer the file carries, as `prose` above writes it: empty for an old-format file,
// and every record whose URI is not under kProsePrefix is dropped rather than trusted.
QVector<ProseBlock> readScrollbackProse(const QString &id);

// Every `scrollback` id in a saved layout's window records (pane nodes at any depth).
QStringList scrollbackIds(const QJsonArray &windows);
// Delete the stored scrollback of every pane that is not in `keep`; returns how many files went.
int pruneScrollback(const QStringList &keep);
// Drop the whole store ("Start a fresh window set", or restoring turned off).
void removeAllScrollback();

}  // namespace windowstate

// ----- the terminal text of a *conversation* (card #0TJ9) --------------------------------------
//
// `windowstate`'s store above is keyed by pane: it is what "reopen where I left off" needs, and
// nothing else can use it. A pane that leaves the layout has its file pruned at the next layout
// write, so opening that conversation from the sessions manager days later found nothing — the
// owner's report, and the two layers measured in
// `docs/qa_evidence/2026-09-20-session-resume-scrollback/`.
//
// So a conversation's terminal text is saved beside the conversation, under the session's own id,
// and lives exactly as long as the session does. Same format as the per-pane store — plain UTF-8
// lines, the same `clampScrollback` caps, `QSaveFile`, 0600, directories 0700 — because a reader
// should not have to care which store a block of text came out of:
//
//   Relay session   <session_dir>/<session_id>.scrollback.txt
//   what a rewind   <session_dir>/<session_id>.rewound-<n>.scrollback.txt
//     undid                       (n is the `rewound_n` of the worker's `rewound` event)
//   guest session   <data>/relay/sessions/guests/<source>/<id>.scrollback.txt
//
// A guest (claude, codex) has no Relay session directory of its own, and Relay never writes inside
// `~/.claude` or `~/.codex`, so its sidecar lives in Relay's own tree — `guest-meta.json` is the
// precedent. `<session_dir>` arrives from the worker (`session_configured`, or a sessions-manager
// row's `session_dir`), so it is Relay's own path, but every id that becomes a file name is
// validated first: a Relay id is 32 lowercase hex (backend `sessions.SESSION_ID`), a guest id is a
// canonical UUID (both `claude` and `codex` name their transcripts with one), and the source is
// exactly `claude` or `codex`. Anything else is refused and nothing is written.
namespace sessiontext {

// 32 lowercase hex, the shape `relay_core.sessions.new_id()` mints and `check_id` enforces.
bool isSessionId(const QString &id);
// 8-4-4-4-12 hex, either case: `~/.claude/projects/<slug>/<uuid>.jsonl` and codex's
// `rollout-<stamp>-<uuid>.jsonl` both end in one, and that is what the guest rows carry.
bool isGuestId(const QString &id);
// The two guest sources Relay knows (protocol 26.7). Exactly these, lower-case.
bool isGuestSource(const QString &source);

// `<sessionDir>/<id>.scrollback.txt`, empty unless `sessionDir` is an absolute path with no `..`
// in it and `id` is a session id.
QString sessionPath(const QString &sessionDir, const QString &id);
// `<sessionDir>/<id>.rewound-<n>.scrollback.txt` for n in [1, 9999]; empty otherwise.
QString rewoundPath(const QString &sessionDir, const QString &id, int n);
// `<data>/relay/sessions/guests`. Empty when no data location is available.
QString guestDirectory();
// `<guestDirectory()>/<source>/<id>.scrollback.txt`; empty for an unknown source or id.
QString guestPath(const QString &source, const QString &id);

// Atomic, 0600, parent directory created 0700; the lines are clamped by `clampScrollback` first,
// and empty content removes the file rather than leaving stale text behind. False with *error set
// when the path is unusable — which is also what an unvalidated id gets, since it yields no path.
// `prose` rides the same trailer the per-pane store carries (see windowstate above, #MTCS).
bool write(const QString &path, const QStringList &lines, const QVector<ProseBlock> &prose = {},
           QString *error = nullptr);
// The saved lines, oldest first; empty when there is no file. Reads only the file's tail.
QStringList read(const QString &path, int maxLines = windowstate::kScrollbackMaxLines);
// The file's prose trailer, if it carries one (the per-pane store's `readScrollbackProse`).
QVector<ProseBlock> readProse(const QString &path);
// Every sidecar of one session: its text file and its `rewound-<n>` files. What a delete has to
// take with it, and what a test asserts over.
QStringList sidecars(const QString &sessionDir, const QString &id);

// Whether saved text holds anything the conversation printed. False when, with formatting,
// whitespace and line breaks gone, all that is left is Relay's own restore rules (`marks`) and
// shell prompts (`user@host:path$`): what a pane that never showed the conversation saves. Joined
// before matching, because a narrow pane wraps a rule or a prompt over several lines. Such text
// must neither overwrite a real record nor stand in for one — the transcript is drawn instead.
bool hasContent(const QStringList &lines, const QStringList &marks);

// Where in a pane's saved lines the turn whose prompt was `prompt` began — what a rewind of that
// turn undid, from there to the end. Relay prints a turn's first line as `✦ <prompt>`, wrapped at
// the pane's width, so the anchor is the *last* line that is that marker followed by the start of
// the prompt's own first line; a turn asked twice is rewound at its latest telling. -1 when there
// is no such line: the turn has scrolled out of the engine's history, or the prompt was sent from
// somewhere with no terminal at all (the phone), and then nothing in the text can say where the
// turn began. Pure, so tests/windowstate_test.cpp can pin the wrapped and repeated cases.
int turnStart(const QStringList &lines, const QString &prompt);

}  // namespace sessiontext
}  // namespace relay
