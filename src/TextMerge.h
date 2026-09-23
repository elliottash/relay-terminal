// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Revisions, base snapshots and a three-way line merge for an open text buffer (card #F8R7).
// QtCore only, so a test, the local file pane and the SSH file pane can share it without a
// window.
//
// The contract every editor of a shared file follows:
//
//   1. When a buffer is filled from a file it keeps a `Snapshot` of what it read: the bytes, the
//      text as the editor holds it (`editorText()`), and a `Revision` (content hash plus whatever
//      stat the source can give). That snapshot is the *base*.
//   2. When the source may have changed (a watcher event, a save about to happen, an agent patch
//      about to land) the editor reads the source again into a second snapshot and decides by
//      `Revision::sameContent()` — the hash, never the event or the mtime alone.
//   3. A changed source goes through `reconcile(base, buffer, disk)`, whose `Outcome` says what
//      the buffer becomes: nothing, the disk's text, the buffer's own text marked clean, a clean
//      merge of both, or a conflict carrying all three versions. After any outcome but a
//      conflict the disk snapshot is the new base; a conflict keeps the old base until the user
//      picks a side.
//   4. `editsBetween(buffer, target)` turns "the buffer becomes this text" into the few ranges
//      that actually differ, so the editor can apply them as one undoable step and leave the
//      cursor, the selection and the scroll where they were.
//
// FilePreview (src/FilePanes.cpp) does this for a local file. A remote file fills a Snapshot from
// the fetched bytes and a `relay::remote::FileStat` (no inode); an agent patch to an open buffer
// passes the Revision it was computed against and is refused with a conflict when that is not the
// buffer's base.
#include <QByteArray>
#include <QString>
#include <QVector>

namespace relay::merge {

// ----- revisions -------------------------------------------------------------------------------

struct Revision {
    bool exists = false;
    qint64 size = -1;       // bytes on disk (the whole file, even when the read was capped)
    qint64 mtimeMs = -1;    // last modification, ms since the epoch; -1 when unknown
    quint64 inode = 0;      // 0 when the platform or the source does not say
    QByteArray hash;        // SHA-256 of the bytes that were read

    // The content is the same: both missing, or both present with the same hash.
    bool sameContent(const Revision &other) const {
        return exists == other.exists && (!exists || hash == other.hash);
    }
    // The stat is the same, which is cheap to check and says nothing changed *unless* the write
    // kept the size and landed inside the same mtime tick. Only a shortcut for a directory event
    // about some other file; a file event always rehashes.
    bool sameStat(const Revision &other) const {
        return exists == other.exists && size == other.size && mtimeMs == other.mtimeMs && inode == other.inode;
    }
};

struct Snapshot {
    QByteArray bytes;       // what was read (at most the cap)
    QString text;           // editorText(QString::fromUtf8(bytes))
    Revision revision;
    bool truncated = false; // the file was longer than the cap: `bytes` is only its head
};

// The revision of these bytes: hash and size, nothing else.
Revision revisionOf(const QByteArray &bytes);
// Stat `path` without reading it (hash left empty). exists=false when it is not a regular file.
Revision statLocalFile(const QString &path);
// Read up to `cap` bytes of `path` and describe them. A missing or unreadable file gives a
// snapshot whose revision has exists=false.
Snapshot readLocalFile(const QString &path, qint64 cap);

// The text a QPlainTextEdit hands back from toPlainText() after setPlainText(text): CR LF, a lone
// CR, U+2028 and U+2029 become LF, and NBSP becomes a space. Base, buffer and disk are compared in
// this form, because it is the only form the buffer can be in.
QString editorText(const QString &text);

// ----- three-way merge -------------------------------------------------------------------------

struct Labels {
    QString ours = QStringLiteral("mine");
    QString base = QStringLiteral("loaded");
    QString theirs = QStringLiteral("disk");
};

struct Conflict {
    int line = 0;           // 0-based line of its `<<<<<<<` marker in MergeResult::text
    QString base, ours, theirs;
};

struct MergeResult {
    // The merge. When there are conflicts, each one is written in diff3 form:
    //   <<<<<<< mine / (ours) / ||||||| loaded / (base) / ======= / (theirs) / >>>>>>> disk
    QString text;
    QVector<Conflict> conflicts;
    bool clean() const { return conflicts.isEmpty(); }
};

// Line-based diff3. Lines keep their terminators, so a missing newline at the end of the file is
// a change like any other. A change on one side only is taken; the same change on both sides is
// taken once; changes to overlapping base lines conflict, and so does an insertion that touches
// another change (including two different insertions at the same point), because nothing says
// which comes first. Changes to adjacent but distinct lines merge.
MergeResult merge3(const QString &base, const QString &ours, const QString &theirs, const Labels &labels = Labels());

// ----- what a buffer does about a changed source ----------------------------------------------

enum class Outcome {
    Unchanged,   // disk == base: nothing happened to the text
    TakeDisk,    // buffer == base: the buffer becomes the disk's text and is clean again
    Converged,   // buffer == disk: nothing to apply, the buffer is clean again
    Merged,      // both changed, without overlap: the buffer becomes `text`, still unsaved
    Conflict,    // both changed the same lines: `merge` has all three versions
};

struct Reconciliation {
    Outcome outcome = Outcome::Unchanged;
    QString text;        // what the buffer should become for TakeDisk / Merged
    MergeResult merge;   // for Merged and Conflict
};

Reconciliation reconcile(const QString &base, const QString &buffer, const QString &disk, const Labels &labels = Labels());

// ----- applying a new text to a buffer in place -----------------------------------------------

struct TextEdit {
    int position = 0;    // in `from`
    int removed = 0;     // characters of `from` replaced
    QString inserted;
};

// The edits, in ascending order of position and never overlapping, that turn `from` into `to`.
// Apply them from the last to the first so the earlier positions stay valid.
QVector<TextEdit> editsBetween(const QString &from, const QString &to);

}  // namespace relay::merge
