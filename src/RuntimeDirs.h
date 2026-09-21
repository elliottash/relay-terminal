// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Private runtime directories, and the sweep that removes the ones a crash left behind (issue 9JYK).
//
// Every pane keeps a `QTemporaryDir` at `/tmp/relay-XXXXXX` (its `state.json` and `input.txt`: the
// shell's PATH and the names of its aliases), and every window manager one at
// `/tmp/relay-open-XXXXXX` (the `open.sock` that `relay open PATH` talks to). `QTemporaryDir`
// removes them in its destructor, so a quit — including a signal since 0f49c89 — cleans up. A
// crash, an OOM kill or `kill -KILL` does not: the owner's /tmp had 876 of them.
//
// A sweep is only safe if it can tell a dead Relay's directory from a live one's, because more than
// one Relay runs at a time and deleting a live pane's directory breaks that pane. So each directory
// carries an **owner file**, written as soon as it is created:
//
//     relay-owner 1
//     pid 12345
//     starttime 987654321
//
// One `key value` per line, mode 0600, written atomically (QSaveFile: temp file + rename), so a
// sweeping Relay never reads half a mark. `starttime` is field 22 of `/proc/<pid>/stat` — the boot
// ticks at which that process started — and it is what makes the mark trustworthy: pids are
// recycled, and without the start time a sweep would spare an unrelated process's directory (or,
// worse, believe a live pane dead is impossible but a dead one alive is a leak that never ends).
// The owner is alive iff `/proc/<pid>/stat` still exists *and* records the same start time.
//
// When the start time cannot be read at all (no /proc), markOwned() writes nothing and returns
// false: an unmarked directory is kept for the grace period, which is the safe way to be wrong.
//
// sweep() is deliberately timid. It looks only at names `QTemporaryDir` itself could have made
// (`relay-XXXXXX` / `relay-open-XXXXXX`, six characters of its own [A-Za-z0-9] alphabet), only at
// real directories that are not symlinks, owned by this uid and mode 0700, whose canonical path is
// directly inside the temp root — /tmp also holds `relay-qa-*` and `relay-*-shots` directories that
// are none of Relay's business. It never follows a symlink while removing, and it stops after a
// fixed number of directories so a /tmp with thousands of leftovers cannot slow a start.
#include <QDir>
#include <QString>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace relay {
namespace runtimedirs {

// A directory as a poll last saw it, so a poll does not list a directory that has not changed
// (card #057J). Pane::pollGuestEvents() asks 12.5 times a second, per pane, whether the guest
// event spool in its runtime directory holds anything, and it holds nothing unless a guest agent
// is running in that pane: QDir::exists() + entryList() on an empty directory costs 13.3 µs and
// three system calls, a bare stat() 0.45 µs and one.
//
// Creating, renaming or removing a file inside a directory bumps that directory's mtime, so
// nothing can appear in it without changed() saying so on the very next look.
class DirStamp {
public:
    // True when the directory may hold something different from the last look — on the first
    // look, when its mtime moved, and when it is a different directory wearing the same path (a
    // pane that was given a fresh runtime dir: a new inode). A path that cannot be stat'd is not
    // a change and is forgotten, so it reports a change again when it comes back.
    //
    // Stamp *then* list: the mtime recorded is the one from before the caller reads the
    // directory, so a file that appears while it is being read is still seen at the next look.
    bool changed(const QString &path);

private:
    bool m_seen = false;
#ifndef Q_OS_WIN
    ino_t m_inode = 0;
    timespec m_mtime{};
#endif
};

// The owner file's name inside a runtime directory, and the format version on its first line.
inline QString ownerFileName() { return QStringLiteral("owner"); }
constexpr int kOwnerVersion = 1;

// How long an unmarked directory (an older Relay's, or one whose mark is a millisecond from being
// written) is left alone before the sweep treats it as rubbish.
//
// A week, not a day: a Relay built before the owner mark existed may still be running, and an idle
// pane only touches its directory when its shell prints a prompt. With a day's grace, a pane left
// alone overnight in such a Relay would have had its directory deleted under it by the next
// Relay to start. Rubbish waiting a week costs nothing; every Relay since marks what it makes.
constexpr qint64 kDefaultGraceSeconds = 7 * 24 * 60 * 60;
// At most this many candidate directories are looked at per sweep, so startup stays quick.
constexpr int kDefaultMaxDirectories = 400;
// ...and it gives up after this long even if the cap is not reached (a slow or huge /tmp).
constexpr qint64 kDefaultBudgetMs = 1500;

// Who owns a runtime directory: a pid, and the start time that tells it from a recycled pid.
struct Owner {
    qint64 pid = 0;
    qulonglong startTime = 0;   // /proc/<pid>/stat field 22, in boot ticks
    bool isValid() const { return pid > 0; }
};

// This process's identity. Invalid when /proc/self/stat cannot be read.
Owner self();
// The owner recorded in `dir`; invalid when there is no owner file or it cannot be understood.
Owner readOwner(const QString &dir);

// Write this process's mark into `dir` (mode 0600, atomic). False when there is nothing to write
// (no /proc) or the file could not be created; the directory then simply looks unmarked.
bool markOwned(const QString &dir);

// Is that process still running, and still the same process? True only when `/proc/<pid>/stat`
// exists and its start time matches.
bool ownerAlive(const Owner &owner);
// The same for the owner file in `dir`. An unmarked directory is not "alive".
bool ownerAlive(const QString &dir);

// Exactly a name QTemporaryDir would have made for us: `relay-` or `relay-open-` plus six
// characters of [A-Za-z0-9]. Nothing else is ever a sweep candidate.
bool isRuntimeDirName(const QString &name);

struct SweepResult {
    int removed = 0;      // dead owner, or unmarked and older than the grace period
    int keptAlive = 0;    // a Relay (this one or another) is using it
    int keptYoung = 0;    // unmarked but too recent to judge
    int errors = 0;       // could not be read, or could not be fully removed
};

// Remove the runtime directories of Relays that are gone. Safe to call while other Relays run.
// `graceSeconds` and `maxDirectories` are parameters so the tests can exercise both.
SweepResult sweep(const QString &tempRoot = QDir::tempPath(),
                  qint64 graceSeconds = kDefaultGraceSeconds,
                  int maxDirectories = kDefaultMaxDirectories);

}  // namespace runtimedirs
}  // namespace relay
