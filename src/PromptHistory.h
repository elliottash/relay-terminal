// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The prompt box's history, kept across restarts (owner report, 2026-09-18: "conversation history
// isnt persisting on exit and re-open. i cant do up arrows to see what i did before").
//
// **One file per pane** (owner, 2026-09-19: "the up/down history seems to be getting commands from
// other panes, not just mine" — "i want pane histories for up/down"). A pane's file is
// `$XDG_DATA_HOME/relay/state/prompt-history/<id>.txt` (0600), named after the same layout id that
// names its scrollback (`state/scrollback/<id>.txt`, src/WindowState.h): the id a pane keeps for
// its text is the id its prompt history travels under, through a restart and through "restore last
// closed". Up and Down in a pane walk only what was typed at that pane — the pane beside it has
// its own file, and a pane opened now starts empty. It is the store behind Up and Down in the
// prompt box (src/RichEditor.h) and behind the history half of the ghost-text suggestions.
//
// Entries are appended one at a time, the moment they are submitted, so a Relay that is killed
// rather than quit loses nothing. O_APPEND makes each of those writes atomic. The file is only
// ever rewritten whole when it is trimmed. Files whose pane is gone for good are pruned when the
// window layout is written, exactly as the scrollback store is.
//
// **One line per entry.** A prompt may be several lines, so a newline inside an entry is stored as
// `\n` and a backslash as `\\` (encode/decode below). Nothing else is escaped, and a file written
// by hand with plain one-line entries reads back exactly as it looks.
//
// What is never written here: a line that went to a running program's stdin, and a password — both
// refused upstream by relay::input::retainable (src/InputPolicy.h), which is what keeps a password
// out of every store Relay has. This file holds what the person typed at Relay itself.
#include <QString>
#include <QStringList>

namespace relay {
namespace prompthistory {

// How many entries one pane keeps, and how long one may be. A longer entry (a big paste) still
// goes in the pane's live history for the length of the session; it is not worth a line in the
// file.
constexpr int kMaxEntries = 1000;
constexpr int kMaxEntryChars = 10000;
// A pane's file is rewritten to kMaxEntries once it passes this, not on every append.
constexpr qint64 kTrimBytes = 512 * 1024;

// `$XDG_DATA_HOME/relay/state/prompt-history`, the directory beside windows.json. Empty with no
// data location.
QString directory();
// `<directory()>/<paneId>.txt`; empty for an id that is not a pane token (the same rule
// windowstate::isScrollbackId applies: 8-64 characters of [A-Za-z0-9-]) or without a data
// location.
QString pathFor(const QString &paneId);

// The file every pane shared until 2026-09-19. Nothing reads it any more; `clearAll()` is the
// only thing that removes it.
QString legacyPath();

// One entry as its line in the file, and back. decode(encode(text)) == text.
QString encode(const QString &entry);
QString decode(const QString &line);

// Is this entry worth a line in the file? Empty, whitespace-only and over-long entries are not.
bool storable(const QString &entry);

// The newest `maxEntries` entries of one file, oldest first. A missing or unreadable file yields
// an empty list, and a line that cannot be decoded is skipped rather than failing the read.
QStringList read(const QString &path, int maxEntries = kMaxEntries);

// The last entry in the file, without reading the rest of it; empty when there is none.
QString lastEntry(const QString &path);

// Append one entry to one pane's file, creating the directory 0700 and the file 0600. False
// (with *error) when the path is unusable or the write failed; a non-storable entry is a no-op
// and returns true, because nothing is wrong. An entry identical to the one already at the end is
// skipped the same way — a phone prompt written at the door (Pane::submitRemote) is also
// remembered when the command runs, and is stored once. Trims the file past kTrimBytes.
bool append(const QString &path, const QString &entry, QString *error = nullptr);

// Rewrite a pane's file with its newest `maxEntries` entries, atomically and 0600.
bool trim(const QString &path, int maxEntries = kMaxEntries, QString *error = nullptr);

// Delete every per-pane file whose id is not in `keepPaneIds` — the same id list that prunes the
// scrollback store, taken from the saved layout and the recently-closed list. Returns how many
// files went. `dir` is directory(); a test passes its own.
int prune(const QString &dir, const QStringList &keepPaneIds);

// Remove every file under `dir`, the directory with them. False (with *error) when the directory
// exists and could not be removed; a missing one is fine. A test passes its own directory.
bool clearDirectory(const QString &dir, QString *error = nullptr);

// Forget everything ("Clear prompt history"): clearDirectory(directory()), and the legacy shared
// file with it. Every live prompt box notices on its next Up, because that re-reads a file that
// is gone.
bool clearAll(QString *error = nullptr);

}  // namespace prompthistory
}  // namespace relay
