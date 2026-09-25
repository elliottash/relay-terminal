// SPDX-License-Identifier: AGPL-3.0-or-later
// An open local file changing on disk under FilePreview (card #F8R7): a write in place, an atomic
// replace, removal and recreation, a clean merge into unsaved edits, an overlapping conflict and
// its three answers, and a save that races an external write. Runs offscreen; the watcher is the
// real QFileSystemWatcher, so every wait is a QTRY on what the pane shows.
#include "FilePanes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>

using relay::FilePreview;

namespace {
void writeInPlace(const QString &path, const QByteArray &data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(data), qint64(data.size()));
}

// What editors, git and QSaveFile do: a temporary in the same folder renamed over the file.
void writeAtomically(const QString &path, const QByteArray &data) {
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
    QVERIFY(file.commit());
}

QByteArray readAll(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray("<missing>");
    return file.readAll();
}

QPlainTextEdit *editorOf(FilePreview &preview) {
    return preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
}

// Types `text` at the start of 0-based line `line`, the way a keystroke would: through the view's
// own cursor, so it is on the undo stack.
void typeAt(QPlainTextEdit *editor, int line, const QString &text) {
    QTextCursor cursor(editor->document()->findBlockByNumber(line));
    editor->setTextCursor(cursor);
    editor->insertPlainText(text);
}

const QByteArray kBase = "one\ntwo\nthree\nfour\nfive\n";
}  // namespace

class FileSyncTests : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        m_path = m_dir.filePath(QStringLiteral("notes-%1.txt").arg(++m_counter));
        writeInPlace(m_path, kBase);
    }

    void aCleanBufferFollowsAnInPlaceWriteKeepingTheCursor() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        QPlainTextEdit *editor = editorOf(preview);
        QTextCursor cursor(editor->document()->findBlockByNumber(3));   // "four"
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 2);
        editor->setTextCursor(cursor);

        writeInPlace(m_path, "zero\none\ntwo\nthree\nfour\nfive\n");
        QTRY_COMPARE(preview.text(), QStringLiteral("zero\none\ntwo\nthree\nfour\nfive\n"));
        QVERIFY(!preview.isDirty());
        QVERIFY(!preview.hasConflict());
        // Still on "four", two characters in, one line further down.
        QCOMPARE(editor->textCursor().block().text(), QStringLiteral("four"));
        QCOMPARE(editor->textCursor().positionInBlock(), 2);
        // And a read-only preview follows it the same way.
        FilePreview readOnly;
        QVERIFY(readOnly.open(m_path));
        writeInPlace(m_path, "just this\n");
        QTRY_COMPARE(readOnly.text(), QStringLiteral("just this\n"));
    }

    void anAtomicReplaceIsSeenAndTheWatchComesBack() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        writeAtomically(m_path, "replaced once\n");
        QTRY_COMPARE(preview.text(), QStringLiteral("replaced once\n"));
        // The rename took the old inode's watch with it; the second replace must still be seen,
        // and so must a later write in place to the new inode.
        writeAtomically(m_path, "replaced twice\n");
        QTRY_COMPARE(preview.text(), QStringLiteral("replaced twice\n"));
        writeInPlace(m_path, "then in place\n");
        QTRY_COMPARE(preview.text(), QStringLiteral("then in place\n"));
    }

    void aNonOverlappingChangeMergesIntoUnsavedEditsAsOneUndoStep() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        QPlainTextEdit *editor = editorOf(preview);
        typeAt(editor, 0, QStringLiteral("my "));
        QVERIFY(preview.isDirty());
        const int caret = editor->textCursor().position();

        writeAtomically(m_path, "one\ntwo\nthree\nfour\nFIVE\nsix\n");
        QTRY_COMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nFIVE\nsix\n"));
        QVERIFY(preview.isDirty());
        QVERIFY(!preview.hasConflict());
        QCOMPARE(editor->textCursor().position(), caret);   // the change was below the caret
        QCOMPARE(preview.base().text, QStringLiteral("one\ntwo\nthree\nfour\nFIVE\nsix\n"));
        // One undo takes the merge out, the next takes the typing out.
        editor->document()->undo();
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\n"));
        editor->document()->undo();
        QCOMPARE(preview.text(), QString::fromUtf8(kBase));

        // Saving the merge writes it, and the watcher's echo of that write asks nothing.
        editor->document()->redo();
        editor->document()->redo();
        QVERIFY(preview.save());
        QCOMPARE(readAll(m_path), QByteArray("my one\ntwo\nthree\nfour\nFIVE\nsix\n"));
        QTest::qWait(300);
        QVERIFY(!preview.hasConflict());
        QVERIFY(!preview.isDirty());
    }

    void anOverlappingChangeStopsAtTheConflictBar() {
        FilePreview preview;
        preview.show();
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        QPlainTextEdit *editor = editorOf(preview);
        typeAt(editor, 1, QStringLiteral("mine "));
        const QString mine = preview.text();

        writeInPlace(m_path, "one\ndisk two\nthree\nfour\nfive\n");
        QTRY_VERIFY(preview.hasConflict());
        QCOMPARE(preview.text(), mine);   // untouched
        auto *bar = preview.findChild<QWidget *>(QStringLiteral("filePreviewConflict"));
        QVERIFY(bar && bar->isVisible());
        QVERIFY(preview.findChild<QLabel *>(QStringLiteral("filePreviewConflictText"))->text().contains(QStringLiteral("1 overlapping change")));

        // Show merge: all three versions, between markers, as one undo step.
        preview.findChild<QToolButton *>(QStringLiteral("filePreviewConflictMerge"))->click();
        QVERIFY(!preview.hasConflict());
        QCOMPARE(preview.text(), QStringLiteral("one\n<<<<<<< mine\nmine two\n||||||| loaded\ntwo\n=======\ndisk two\n>>>>>>> disk\nthree\nfour\nfive\n"));
        QVERIFY(preview.isDirty());
        QCOMPARE(editor->textCursor().block().text(), QStringLiteral("<<<<<<< mine"));
        editor->document()->undo();
        QCOMPARE(preview.text(), mine);
        // The disk version the markers came from is the base now: saving is not a race.
        QCOMPARE(preview.base().text, QStringLiteral("one\ndisk two\nthree\nfour\nfive\n"));
    }

    void takeDiskAndKeepMine() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        QPlainTextEdit *editor = editorOf(preview);
        typeAt(editor, 2, QStringLiteral("mine "));
        writeInPlace(m_path, "one\ntwo\ndisk three\nfour\nfive\n");
        QTRY_VERIFY(preview.hasConflict());
        QVERIFY(preview.resolveConflict(FilePreview::Resolution::TakeDisk));
        QCOMPARE(preview.text(), QStringLiteral("one\ntwo\ndisk three\nfour\nfive\n"));
        QVERIFY(!preview.isDirty());
        editor->document()->undo();   // your text is one undo away
        QCOMPARE(preview.text(), QStringLiteral("one\ntwo\nmine three\nfour\nfive\n"));

        // Keep mine: the buffer stays, and the next save writes it without asking again.
        QVERIFY(preview.isDirty());
        writeInPlace(m_path, "one\ntwo\nother three\nfour\nfive\n");
        QTRY_VERIFY(preview.hasConflict());
        QVERIFY(preview.resolveConflict(FilePreview::Resolution::KeepMine));
        QCOMPARE(preview.text(), QStringLiteral("one\ntwo\nmine three\nfour\nfive\n"));
        QVERIFY(preview.save());
        QCOMPARE(readAll(m_path), QByteArray("one\ntwo\nmine three\nfour\nfive\n"));
    }

    void deletionKeepsTheBufferAndRecreationReconciles() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 0, QStringLiteral("my "));
        const QString mine = preview.text();

        QVERIFY(QFile::remove(m_path));
        QTRY_VERIFY(preview.deletedOnDisk());
        QCOMPARE(preview.text(), mine);
        QVERIFY(preview.isDirty());
        QVERIFY(preview.notice().contains(QStringLiteral("deleted on disk")));

        // Back, with a change below the edit: merged in, and the pane no longer says deleted.
        writeInPlace(m_path, "one\ntwo\nthree\nfour\nfive\nsix\n");
        QTRY_VERIFY(!preview.deletedOnDisk());
        QTRY_COMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\nsix\n"));
        QVERIFY(!preview.notice().contains(QStringLiteral("deleted on disk")));

        // Deleted again, then saved: the buffer is written back.
        QVERIFY(QFile::remove(m_path));
        QTRY_VERIFY(preview.deletedOnDisk());
        QVERIFY(preview.save());
        QCOMPARE(readAll(m_path), QByteArray("my one\ntwo\nthree\nfour\nfive\nsix\n"));
        QVERIFY(!preview.deletedOnDisk());
    }

    void aSaveThatRacesAnExternalWriteAsksInsteadOfOverwriting() {
        FilePreview preview;
        preview.show();
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        QPlainTextEdit *editor = editorOf(preview);
        typeAt(editor, 0, QStringLiteral("my "));

        // The write lands and the save follows before the watcher has had a turn.
        writeInPlace(m_path, "one\ntwo\nthree\nfour\nfive\nsix\n");
        QVERIFY(!preview.save());
        QVERIFY(preview.hasConflict());
        QCOMPARE(readAll(m_path), QByteArray("one\ntwo\nthree\nfour\nfive\nsix\n"));   // not overwritten
        QCOMPARE(preview.findChild<QToolButton *>(QStringLiteral("filePreviewConflictMine"))->text(), QStringLiteral("Overwrite"));
        // The watcher's own look at the same write changes nothing: the bar is about that text.
        QTest::qWait(300);
        QVERIFY(preview.hasConflict());
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\n"));

        // Merge, then save: both changes are on disk.
        QVERIFY(preview.resolveConflict(FilePreview::Resolution::Merge));
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\nsix\n"));
        QVERIFY(preview.save());
        QCOMPARE(readAll(m_path), QByteArray("my one\ntwo\nthree\nfour\nfive\nsix\n"));

        // Overwrite writes the buffer; Reload takes the disk's.
        typeAt(editor, 1, QStringLiteral("my "));
        writeInPlace(m_path, "theirs\n");
        QVERIFY(!preview.save());
        QVERIFY(preview.resolveConflict(FilePreview::Resolution::KeepMine));
        QCOMPARE(readAll(m_path), QByteArray("my one\nmy two\nthree\nfour\nfive\nsix\n"));
        QVERIFY(!preview.isDirty());

        typeAt(editor, 2, QStringLiteral("my "));
        writeInPlace(m_path, "theirs again\n");
        QVERIFY(!preview.save());
        QVERIFY(preview.resolveConflict(FilePreview::Resolution::TakeDisk));
        QCOMPARE(preview.text(), QStringLiteral("theirs again\n"));
        QVERIFY(!preview.isDirty());
        QCOMPARE(readAll(m_path), QByteArray("theirs again\n"));
    }

    void aChoiceAboutTextThatMovedAgainIsAskedAgain() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 0, QStringLiteral("my "));
        writeInPlace(m_path, "first\n");
        QVERIFY(!preview.save());
        QVERIFY(preview.hasConflict());
        writeInPlace(m_path, "second\n");
        // Overwrite was about "first"; "second" nobody has seen, so it is not written over.
        QVERIFY(!preview.resolveConflict(FilePreview::Resolution::KeepMine));
        QVERIFY(preview.hasConflict());
        QCOMPARE(readAll(m_path), QByteArray("second\n"));
        QVERIFY(preview.resolveConflict(FilePreview::Resolution::KeepMine));
        QCOMPARE(readAll(m_path), QByteArray("my one\ntwo\nthree\nfour\nfive\n"));
    }

    void aFileTooLongToShowWholeIsNotEditable() {
        const QString big = m_dir.filePath(QStringLiteral("big.txt"));
        writeInPlace(big, QByteArray(FilePreview::kMaxTextBytes + 16, 'x'));
        FilePreview preview;
        QVERIFY(preview.open(big));
        QVERIFY(preview.base().truncated);
        preview.startEditing();
        QVERIFY(!preview.isEditable());
        QVERIFY(!preview.findChild<QToolButton *>(QStringLiteral("filePreviewEdit"))->isVisibleTo(&preview));
    }

private:
    QTemporaryDir m_dir;
    QString m_path;
    int m_counter = 0;

private slots:
    // ----- agent writes through the open buffer (#F8R7, protocol 35) ---------------------------

    void aPatchToACleanBufferIsAppliedAndSavedAsOneUndoStep() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        const QJsonObject entry = preview.openBufferEntry();
        QCOMPARE(entry.value(QStringLiteral("path")).toString(), m_path);
        QCOMPARE(entry.value(QStringLiteral("sha256")).toString(), sha256(kBase));
        QVERIFY(!entry.value(QStringLiteral("dirty")).toBool());
        QVERIFY(FilePreview::openBuffers(nullptr).contains(entry));

        const QJsonObject result = patch(preview, sha256(kBase), QStringLiteral("one\nTWO\nthree\nfour\nfive\n"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("applied")).toString(), QStringLiteral("exact"));
        QVERIFY(result.value(QStringLiteral("saved")).toBool());
        QCOMPARE(readAll(m_path), QByteArray("one\nTWO\nthree\nfour\nfive\n"));
        QCOMPARE(preview.agentChanges().size(), 1);
        QCOMPARE(preview.agentChanges().last().turnId, QStringLiteral("turn-1"));
        QCOMPARE(preview.agentChanges().last().firstLine, 2);
        // Taking it back is saved back too.
        QVERIFY(preview.undoAgentChange());
        QCOMPARE(preview.text(), QString::fromUtf8(kBase));
        QCOMPARE(readAll(m_path), kBase);
    }

    void aPatchMergesAroundUnsavedTypingAndLeavesItUnsaved() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 0, QStringLiteral("my "));
        QVERIFY(preview.openBufferEntry().value(QStringLiteral("dirty")).toBool());

        // Worked out against the disk's text, which is the loaded base.
        const QJsonObject result = patch(preview, sha256(kBase), QStringLiteral("one\ntwo\nthree\nfour\nFIVE\n"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("applied")).toString(), QStringLiteral("merged"));
        QVERIFY(!result.value(QStringLiteral("saved")).toBool());
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nFIVE\n"));
        QCOMPARE(readAll(m_path), kBase);   // the user's save takes both
        editorOf(preview)->document()->undo();
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\n"));
    }

    void anOverlappingPatchIsAConflictAndChangesNothing() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 1, QStringLiteral("my "));
        const QString before = preview.text();
        const QJsonObject result = patch(preview, sha256(kBase), QStringLiteral("one\nTWO\nthree\nfour\nfive\n"));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("conflict"));
        const QJsonObject region = result.value(QStringLiteral("conflicts")).toArray().first().toObject();
        QVERIFY(region.value(QStringLiteral("buffer")).toString().contains(QStringLiteral("my two")));
        QCOMPARE(preview.text(), before);
        QVERIFY(preview.agentChanges().isEmpty());
    }

    void aPatchAgainstUnknownTextIsStaleButAnUniqueEditStillApplies() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        const QString unknown = sha256("something else entirely\n");
        QCOMPARE(patch(preview, unknown, QStringLiteral("x\n")).value(QStringLiteral("error")).toString(),
                 QStringLiteral("stale"));
        QJsonObject edit = request(unknown, QStringLiteral("one\ntwo\nTHREE\nfour\nfive\n"));
        edit.insert(QStringLiteral("tool"), QStringLiteral("edit_file"));
        edit.insert(QStringLiteral("old_string"), QStringLiteral("three"));
        edit.insert(QStringLiteral("new_string"), QStringLiteral("THREE"));
        QJsonObject result;
        preview.answerBufferRequest(edit, [&](const QJsonObject &r) { result = r; });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(preview.text(), QStringLiteral("one\ntwo\nTHREE\nfour\nfive\n"));
    }

    void aReadAnswersTheBufferAndAnUnknownPathIsNotOpen() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 0, QStringLiteral("my "));
        QJsonObject result;
        FilePreview::answerBufferRequestIn(nullptr, QJsonObject{{QStringLiteral("id"), QStringLiteral("br-1")},
                                                                {QStringLiteral("op"), QStringLiteral("read")},
                                                                {QStringLiteral("path"), m_path}},
                                           [&](const QJsonObject &r) { result = r; });
        QCOMPARE(result.value(QStringLiteral("type")).toString(), QStringLiteral("buffer_result"));
        QCOMPARE(result.value(QStringLiteral("id")).toString(), QStringLiteral("br-1"));
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\n"));
        QVERIFY(result.value(QStringLiteral("dirty")).toBool());
        FilePreview::answerBufferRequestIn(nullptr, QJsonObject{{QStringLiteral("op"), QStringLiteral("read")},
                                                                {QStringLiteral("path"), m_dir.filePath(QStringLiteral("nobody.txt"))}},
                                           [&](const QJsonObject &r) { result = r; });
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("not_open"));
    }

private:
    static QString sha256(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    }
    QJsonObject request(const QString &base, const QString &content) const {
        return QJsonObject{{QStringLiteral("id"), QStringLiteral("br-1")}, {QStringLiteral("op"), QStringLiteral("patch")},
                           {QStringLiteral("path"), m_path}, {QStringLiteral("tool"), QStringLiteral("write_file")},
                           {QStringLiteral("base_sha256"), base}, {QStringLiteral("content"), content},
                           {QStringLiteral("intent"), QStringLiteral("Write notes")},
                           {QStringLiteral("turn_id"), QStringLiteral("turn-1")}, {QStringLiteral("model"), QStringLiteral("m")}};
    }
    QJsonObject patch(FilePreview &preview, const QString &base, const QString &content) const {
        QJsonObject result;
        preview.answerBufferRequest(request(base, content), [&](const QJsonObject &r) { result = r; });
        return result;
    }
};

QTEST_MAIN(FileSyncTests)
#include "filesync_test.moc"
