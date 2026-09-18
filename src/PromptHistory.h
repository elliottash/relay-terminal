// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The prompt box's history, kept across restarts (owner report, 2026-09-18: "conversation history
// isnt persisting on exit and re-open. i cant do up arrows to see what i did before").
//
// Relay writes one history file per user, `$XDG_DATA_HOME/relay/state/prompt-history.txt` (0600),
// holding the lines submitted from the composer, oldest first. It is the store behind Up and Down
// in the prompt box (src/RichEditor.h) and behind the history half of the ghost-text suggestions.
//
// **One file for every pane, tab and window**, like a shell's own history: a pane opened now
// starts with what was typed in the pane beside it, and what a pane remembers outlives it. Entries
// are appended one at a time, the moment they are submitted, so a Relay that is killed rather than
// quit loses nothing — and so two Relays running at once do not overwrite each other. O_APPEND
// makes each of those writes atomic; the file is only ever rewritten whole when it is trimmed.
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

// How many entries are kept, and how long one may be. A longer entry (a big paste) still goes in
// the pane's live history for the length of the session; it is not worth a line in the file.
constexpr int kMaxEntries = 1000;
constexpr int kMaxEntryChars = 10000;
// The file is rewritten to kMaxEntries once it passes this, not on every append.
constexpr qint64 kTrimBytes = 512 * 1024;

// `$XDG_DATA_HOME/relay/state`, the directory windows.json is in. Empty with no data location.
QString defaultDirectory();
// `<defaultDirectory()>/prompt-history.txt`. Empty when there is no data location.
QString defaultPath();

// One entry as its line in the file, and back. decode(encode(text)) == text.
QString encode(const QString &entry);
QString decode(const QString &line);

// Is this entry worth a line in the file? Empty, whitespace-only and over-long entries are not.
bool storable(const QString &entry);

// The newest `maxEntries` entries, oldest first. A missing or unreadable file yields an empty
// list, and a line that cannot be decoded is skipped rather than failing the read.
QStringList read(const QString &path, int maxEntries = kMaxEntries);

// The last entry in the file, without reading the rest of it; empty when there is none.
QString lastEntry(const QString &path);

// Append one entry, creating the directory 0700 and the file 0600. False (with *error) when the
// path is unusable or the write failed; a non-storable entry is a no-op and returns true, because
// nothing is wrong. An entry identical to the one already at the end is skipped the same way — a
// prompt box dedupes against its own last line, which cannot see what another pane or a paired
// phone appended in between. Trims the file when it has grown past kTrimBytes.
bool append(const QString &path, const QString &entry, QString *error = nullptr);

// Rewrite the file with its newest `maxEntries` entries, atomically and 0600.
bool trim(const QString &path, int maxEntries = kMaxEntries, QString *error = nullptr);

// Forget everything ("Clear prompt history"). Removing the file is the whole of it; every live
// prompt box notices on its next Up, because that re-reads a file that has changed.
bool clear(const QString &path, QString *error = nullptr);

}  // namespace prompthistory
}  // namespace relay
