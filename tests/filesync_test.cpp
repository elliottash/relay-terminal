// SPDX-License-Identifier: AGPL-3.0-or-later
// An open local file changing on disk under FilePreview (card #F8R7): a write in place, an atomic
// replace, removal and recreation, a clean merge into unsaved edits, an overlapping conflict and
// its three answers, and a save that races an external write. Runs offscreen; the watcher is the
// real QFileSystemWatcher, so every wait is a QTRY on what the pane shows.
#include "FilePanes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>

using relay::ArtifactDock;
using relay::FilePreview;
using relay::PlanEditor;

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
    void initTestCase() {
        // "Review before apply" is a QSettings value: kept in this run's own folder.
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("settings")));
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("settings")));
    }

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

    // Owner decision D2 of #P2W8 (card #PBZ4): an overlap is merged into the buffer with both
    // versions between markers, as one undo step, never saved — and the agent is told where.
    void anOverlappingPatchLandsBetweenConflictMarkersAsOneUndoStep() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 1, QStringLiteral("my "));
        const QString before = preview.text();
        const QJsonObject result = patch(preview, sha256(kBase), QStringLiteral("one\nTWO\nthree\nfour\nfive\n"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("applied")).toString(), QStringLiteral("conflict"));
        QVERIFY(!result.value(QStringLiteral("saved")).toBool());
        const QJsonObject region = result.value(QStringLiteral("conflicts")).toArray().first().toObject();
        QVERIFY(region.value(QStringLiteral("buffer")).toString().contains(QStringLiteral("my two")));
        QCOMPARE(region.value(QStringLiteral("agent")).toString(), QStringLiteral("TWO\n"));
        QCOMPARE(region.value(QStringLiteral("line")).toInt(), 2);
        QVERIFY(preview.text().contains(QStringLiteral("<<<<<<< editor\nmy two\n")));
        QVERIFY(preview.text().contains(QStringLiteral("=======\nTWO\n>>>>>>> agent\n")));
        QCOMPARE(readAll(m_path), kBase);                                    // markers never reach the disk
        QCOMPARE(preview.agentChanges().last().applied, QStringLiteral("conflict"));
        QVERIFY(preview.findChild<QLabel *>(QStringLiteral("filePreviewAgentText"))->text().contains(QStringLiteral("conflicts with your edits")));
        editorOf(preview)->document()->undo();                               // one step takes it all back
        QCOMPARE(preview.text(), before);
    }

    // ----- the docked agent (card #PBZ4) -------------------------------------------------------

    void theDockIsAboutTheFileAndItsPlugin() {
        const QString md = m_dir.filePath(QStringLiteral("notes.md"));
        writeInPlace(md, "# Notes\n\nfirst\n");
        const QString bundled = m_dir.filePath(QStringLiteral("bundled"));
        QDir().mkpath(bundled + QStringLiteral("/markdown"));
        writeInPlace(bundled + QStringLiteral("/markdown/plugin.json"),
                     R"({"schema_version": 2, "id": "relay.markdown", "name": "Markdown", "activation": {"files": ["*.md"]}, )"
                     R"("commands": [{"name": "outline", "description": "Outline it.", "action": {"kind": "prompt", "prompt": "Outline {file}."}}]})");
        relay::agent::PluginSearch search;
        search.bundled = bundled;
        FilePreview::setPluginSearch(search);
        FilePreview preview;
        QVERIFY(preview.open(md));
        ArtifactDock *dock = preview.artifactDock();
        QVERIFY(dock);
        QVERIFY(!dock->isHidden());
        const relay::agent::ContextSpec spec = dock->context()->spec();
        QCOMPARE(spec.name, QStringLiteral("artifact"));
        QCOMPARE(spec.file, md);
        QCOMPARE(spec.plugin, QStringLiteral("relay.markdown"));
        QVERIFY(spec.screen.contains(QStringLiteral("read only")));
        preview.startEditing();
        typeAt(editorOf(preview), 2, QStringLiteral("x"));
        QVERIFY(dock->context()->spec().screen.contains(QStringLiteral("unsaved edits")));
        QVERIFY(dock->context()->spec().screen.contains(QStringLiteral("Cursor: line 3")));
        // Save and Revert follow the buffer.
        const QList<relay::agent::Action> row = relay::agent::withUniqueLetters(dock->context()->actions());
        QCOMPARE(row.last().label, QStringLiteral("Revert"));
        QVERIFY(row.last().enabled);
        // A picture has no buffer, so no dock.
        const QString png = m_dir.filePath(QStringLiteral("dot.png"));
        QImage(2, 2, QImage::Format_RGB32).save(png);
        FilePreview picture;
        QVERIFY(picture.open(png));
        QVERIFY(picture.artifactDock()->isHidden());
        FilePreview::setPluginSearch({});
    }

    // Found live: the window opens a pane's file in the pane's constructor and only then sets
    // where plugins live, so the first Markdown file of a session had no plugin. The console is
    // built later, on first expand, and that is when the plugin must be read.
    void aFileOpenedBeforeThePluginSearchGetsItsPluginWhenTheConsoleIsBuilt() {
        const QString md = m_dir.filePath(QStringLiteral("early.md"));
        writeInPlace(md, "# Early\n");
        const QString bundled = m_dir.filePath(QStringLiteral("bundled-late"));
        QDir().mkpath(bundled + QStringLiteral("/markdown"));
        writeInPlace(bundled + QStringLiteral("/markdown/plugin.json"),
                     R"({"schema_version": 2, "id": "relay.markdown", "name": "Markdown", "activation": {"files": ["*.md"]}, )"
                     R"("commands": [{"name": "outline", "description": "Outline it.", "action": {"kind": "prompt", "prompt": "Outline {file}."}}]})");
        FilePreview::setPluginSearch({});
        FilePreview preview;
        QVERIFY(preview.open(md));
        ArtifactDock *dock = preview.artifactDock();
        QVERIFY(!dock->context()->plugin().valid());          // the order the window builds a pane in
        relay::agent::PluginSearch search;
        search.bundled = bundled;
        FilePreview::setPluginSearch(search);
        QWidget body;
        dock->onCreateConsole = [&body](relay::agent::Context *, QWidget *) {
            relay::agent::ConsoleHandle handle;
            handle.widget = new QWidget(&body);
            return handle;
        };
        dock->focusHelper();                                   // the first expand builds the console
        QCOMPARE(dock->context()->plugin().id, QStringLiteral("relay.markdown"));
        QCOMPARE(dock->context()->spec().plugin, QStringLiteral("relay.markdown"));
        QCOMPARE(dock->context()->slashCommands().size(), 1);
        FilePreview::setPluginSearch({});
    }

    // U5: the file's record is the undo steps plus a per-turn change list, named by the words
    // that asked once the turn has ended. The typing around a change survives it.
    void theChangeListNamesTheTurnAndTheTypingSurvives() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        typeAt(editorOf(preview), 0, QStringLiteral("my "));
        QVERIFY(patch(preview, sha256(kBase), QStringLiteral("one\ntwo\nthree\nfour\nfive\nsix\n")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\nsix\n"));
        relay::agent::TurnRecord turn;
        turn.turnId = QStringLiteral("turn-1");
        turn.prompt = QStringLiteral("add a line six");
        preview.artifactDock()->context()->turnFinished(turn);
        const QStringList rows = preview.artifactDock()->changeListRows();
        QCOMPARE(rows.value(0), QStringLiteral("Turn · “add a line six”"));
        QVERIFY(rows.value(1).startsWith(QStringLiteral("line 6 · Write notes · merged")));
        editorOf(preview)->document()->undo();                               // the agent's step
        QCOMPARE(preview.text(), QStringLiteral("my one\ntwo\nthree\nfour\nfive\n"));
    }

    // D1: with Review before apply on, a patch waits for Apply and the agent is told so.
    void reviewBeforeApplyHoldsAPatchUntilApply() {
        FilePreview preview;
        QVERIFY(preview.open(m_path));
        preview.startEditing();
        preview.artifactDock()->setReviewBeforeApply(true);
        const QJsonObject held = patch(preview, sha256(kBase), QStringLiteral("one\nTWO\nthree\nfour\nfive\n"));
        QVERIFY(held.value(QStringLiteral("ok")).toBool());
        QCOMPARE(held.value(QStringLiteral("applied")).toString(), QStringLiteral("held"));
        QCOMPARE(preview.text(), QString::fromUtf8(kBase));
        QCOMPARE(preview.heldAgentChanges(), 1);
        QVERIFY(preview.findChild<QToolButton *>(QStringLiteral("filePreviewAgentApply"))->isVisibleTo(&preview));
        typeAt(editorOf(preview), 4, QStringLiteral("my "));                   // the person keeps typing
        QCOMPARE(preview.applyHeldAgentChanges(), 1);
        QCOMPARE(preview.text(), QStringLiteral("one\nTWO\nthree\nfour\nmy five\n"));
        QCOMPARE(preview.heldAgentChanges(), 0);
        // The same request heard by a second console of the tab is not held a second time.
        QJsonObject twice = request(sha256("one\nTWO\nthree\nfour\nmy five\n"), QStringLiteral("one\nTWO\nthree\nfour\nmy five\nsix\n"));
        twice.insert(QStringLiteral("id"), QStringLiteral("br-dup"));
        int answers = 0;
        for (int console = 0; console < 2; ++console)
            FilePreview::answerBufferRequestIn(nullptr, twice, [&](const QJsonObject &) { ++answers; });
        QCOMPARE(answers, 1);
        QCOMPARE(preview.heldAgentChanges(), 1);
        preview.discardHeldAgentChanges();
        // Discard drops it, and the setting is remembered for this folder's next pane.
        QVERIFY(patch(preview, sha256(kBase), QStringLiteral("zero\n")).value(QStringLiteral("applied")) == QStringLiteral("held"));
        preview.discardHeldAgentChanges();
        QCOMPARE(preview.heldAgentChanges(), 0);
        FilePreview again;
        QVERIFY(again.open(m_path));
        QVERIFY(again.artifactDock()->reviewBeforeApply());
        preview.artifactDock()->setReviewBeforeApply(false);
    }

    // A plan pane's agent writes the disk; a clean plan follows it as one undo step.
    void aCleanPlanFollowsTheAgentsWrite() {
        const QString plan = m_dir.filePath(QStringLiteral("plan.md"));
        writeInPlace(plan, "# Plan\n\n1. one\n");
        PlanEditor editor;
        QVERIFY(editor.open(plan));
        QCOMPARE(editor.artifactDock()->context()->spec().file, plan);
        writeAtomically(plan, "# Plan\n\n1. one\n2. two\n");
        QTRY_COMPARE(editor.text(), QStringLiteral("# Plan\n\n1. one\n2. two\n"));
        QVERIFY(!editor.isDirty());
        editor.editor()->document()->undo();
        QCOMPARE(editor.text(), QStringLiteral("# Plan\n\n1. one\n"));
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
