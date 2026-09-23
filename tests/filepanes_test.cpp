// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FilePanes.h"
#include "RemoteFiles.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <ctime>

using relay::FileExplorer;
using relay::FilePreview;

namespace {
void writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), qint64(data.size()));
}

QStringList names(const QStringList &paths) {
    QStringList out;
    for (const auto &path : paths) out << QFileInfo(path).fileName();
    return out;
}

// A C++ file of `lines` lines, with the comments, strings and keywords a highlighter has to carry
// state across. Nothing here reads the repository: the file panes are timed on a file of a known
// size, not on whatever happens to be on this disk (#MDSG).
QByteArray sourceLines(int lines) {
    QByteArray out;
    out.reserve(lines * 64);
    for (int i = 0; i < lines; ++i) {
        switch (i % 6) {
        case 0: out += "// the " + QByteArray::number(i) + "th line of this file\n"; break;
        case 1: out += "static const char *name" + QByteArray::number(i) + " = \"a string\";\n"; break;
        case 2: out += "/* a comment that\n"; break;
        case 3: out += "   runs over two lines */\n"; break;
        case 4: out += "int value" + QByteArray::number(i) + "(int x) { return x * 2; }\n"; break;
        default: out += "\n"; break;
        }
    }
    return out;
}

// CPU this process has burnt, in milliseconds: the number the profile reports as "GUI CPU".
double cpuMs() {
    struct timespec ts {};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

// The non-separator ids of a right-click menu, in order.
QStringList menuIds(const QList<relay::FileMenuItem> &items) {
    QStringList ids;
    for (const relay::FileMenuItem &item : items)
        if (!item.isSeparator()) ids << item.id;
    return ids;
}
}  // namespace

class FilePanesTests : public QObject {
    Q_OBJECT
private slots:
    void explorerNavigatesIntoFolderAndUp() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("sub")));
        writeFile(temp.filePath(QStringLiteral("sub/inner.txt")), "x");
        writeFile(temp.filePath(QStringLiteral("top.txt")), "y");
        FileExplorer explorer(temp.path());
        QStringList changes;
        explorer.onDirectoryChanged = [&changes](const QString &path) { changes << path; };
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList({QStringLiteral("sub"), QStringLiteral("top.txt")}));
        QVERIFY(explorer.activateRow(0));  // folders sort first
        QCOMPARE(QFileInfo(explorer.root()).fileName(), QStringLiteral("sub"));
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList({QStringLiteral("inner.txt")}));
        explorer.goUp();
        QCOMPARE(QDir(explorer.root()), QDir(temp.path()));
        QCOMPARE(changes.size(), 2);
    }

    // Left navigates up; Right enters a selected folder only. Both keys are local to the tree so
    // they take precedence over pane navigation. Files, no selection, and filesystem root are safe.
    void leftAndRightNavigateDirectoriesInTheList() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("inner/deeper")));
        writeFile(temp.filePath(QStringLiteral("top.txt")), "x");
        const QString inner = QDir(temp.path()).filePath(QStringLiteral("inner"));
        const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
        FileExplorer explorer(deeper);
        explorer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&explorer));
        QTreeView *view = explorer.findChild<QTreeView *>(QStringLiteral("fileExplorerView"));
        QLineEdit *filter = explorer.findChild<QLineEdit *>(QStringLiteral("fileExplorerFilter"));
        QVERIFY(view && filter);
        const QStringList local = view->property("relayLocalKeys").toStringList();
        QVERIFY(local.contains(QStringLiteral("Left")));
        QVERIFY(local.contains(QStringLiteral("Right")));
        QVERIFY(!filter->property("relayLocalKeys").toStringList().contains(QStringLiteral("Left")));
        QVERIFY(!filter->property("relayLocalKeys").toStringList().contains(QStringLiteral("Right")));

        // Right on the selected directory enters it.
        explorer.setRoot(inner);
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList{QStringLiteral("deeper")});
        auto *localModel = qobject_cast<QFileSystemModel *>(view->model());
        QVERIFY(localModel);
        QModelIndex directory = localModel->index(deeper);
        QVERIFY(directory.isValid());
        view->setCurrentIndex(directory);
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(QDir(explorer.root()), QDir(deeper));

        // Left goes back up and selects the directory we came from.
        QTest::keyClick(view, Qt::Key_Left);
        QCOMPARE(QDir(explorer.root()), QDir(inner));
        QCOMPARE(QFileInfo(localModel->filePath(view->currentIndex())).fileName(), QStringLiteral("deeper"));

        // Right on a file and with no selected row does nothing.
        QModelIndex file = localModel->index(temp.filePath(QStringLiteral("top.txt")));
        QVERIFY(file.isValid());
        view->setCurrentIndex(file);
        const QString beforeFile = explorer.root();
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(explorer.root(), beforeFile);
        view->setCurrentIndex(QModelIndex());
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(explorer.root(), beforeFile);

        // The filesystem root cannot go higher.
        explorer.setRoot(QDir::rootPath());
        const QString root = explorer.root();
        QTest::keyClick(view, Qt::Key_Left);
        QCOMPARE(explorer.root(), root);
    }

    // Alt+Up is the parent folder in the list and the filter (#KYPR). The window's dispatcher binds
    // Alt+Up to the pane above and gives way because both widgets list it in `relayLocalKeys`.
    void altUpIsTheParentFolderAndIsDeclaredLocal() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("inner")));
        const QString inner = QDir(temp.path()).filePath(QStringLiteral("inner"));
        FileExplorer explorer(inner);
        explorer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&explorer));
        QTreeView *view = explorer.findChild<QTreeView *>(QStringLiteral("fileExplorerView"));
        QLineEdit *filter = explorer.findChild<QLineEdit *>(QStringLiteral("fileExplorerFilter"));
        QVERIFY(view && filter);
        QVERIFY(view->property("relayLocalKeys").toStringList().contains(QStringLiteral("Alt+Up")));
        QVERIFY(filter->property("relayLocalKeys").toStringList().contains(QStringLiteral("Alt+Up")));
        QCOMPARE(QKeySequence(int(Qt::AltModifier) | Qt::Key_Up).toString(QKeySequence::PortableText), QStringLiteral("Alt+Up"));
        QTest::keyClick(view, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(QDir(explorer.root()), QDir(temp.path()));
        QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("inner/deeper")));
        explorer.setRoot(QDir(inner).filePath(QStringLiteral("deeper")));
        QTest::keyClick(filter, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(QDir(explorer.root()), QDir(inner));
    }

    void filterNarrowsRows() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral("alpha.py")), "");
        writeFile(temp.filePath(QStringLiteral("beta.md")), "");
        writeFile(temp.filePath(QStringLiteral("Alphabet.txt")), "");
        FileExplorer explorer(temp.path());
        QTRY_COMPARE(explorer.visiblePaths().size(), 3);
        explorer.setFilter(QStringLiteral("alpha"));
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList({QStringLiteral("alpha.py"), QStringLiteral("Alphabet.txt")}));
        explorer.setFilter(QString());
        QTRY_COMPARE(explorer.visiblePaths().size(), 3);
    }

    void hiddenFilesToggle() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral(".secret")), "");
        writeFile(temp.filePath(QStringLiteral("shown")), "");
        FileExplorer explorer(temp.path());
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList({QStringLiteral("shown")}));
        explorer.setShowHidden(true);
        QTRY_COMPARE(explorer.visiblePaths().size(), 2);
    }

    void enterOnFileOpensIt() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral("note.txt")), "hello");
        FileExplorer explorer(temp.path());
        QString opened;
        explorer.onOpenFile = [&opened](const QString &path) { opened = path; };
        explorer.show();
        QTRY_COMPARE(explorer.visiblePaths().size(), 1);
        QTRY_VERIFY(explorer.view()->currentIndex().isValid());
        explorer.view()->setFocus();
        QTest::keyClick(explorer.view(), Qt::Key_Return);
        QCOMPARE(QFileInfo(opened).fileName(), QStringLiteral("note.txt"));
    }

    // Card #SEJ2: Ctrl+Enter is the in-app variant of Enter — the file opens ready to edit,
    // and the plain-open callback stays out of it.
    void ctrlEnterOnFileAsksForAnEditor() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral("note.txt")), "hello");
        FileExplorer explorer(temp.path());
        QString opened, edited;
        explorer.onOpenFile = [&opened](const QString &path) { opened = path; };
        explorer.onEditFile = [&edited](const QString &path) { edited = path; };
        explorer.show();
        QTRY_COMPARE(explorer.visiblePaths().size(), 1);
        QTRY_VERIFY(explorer.view()->currentIndex().isValid());
        explorer.view()->setFocus();
        QTest::keyClick(explorer.view(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(QFileInfo(edited).fileName(), QStringLiteral("note.txt"));
        QVERIFY(opened.isEmpty());
    }

    // Card #SEJ2: Shift+Enter is the chord that leaves the app. The host callback stands in for
    // the desktop here, so the test never opens a real application.
    void shiftEnterOnFileGoesToTheDesktop() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral("note.txt")), "hello");
        FileExplorer explorer(temp.path());
        QString opened, external;
        explorer.onOpenFile = [&opened](const QString &path) { opened = path; };
        explorer.onOpenExternal = [&external](const QString &path) { external = path; };
        explorer.show();
        QTRY_COMPARE(explorer.visiblePaths().size(), 1);
        QTRY_VERIFY(explorer.view()->currentIndex().isValid());
        explorer.view()->setFocus();
        QTest::keyClick(explorer.view(), Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(QFileInfo(external).fileName(), QStringLiteral("note.txt"));
        QVERIFY(opened.isEmpty());
    }

    // A folder only ever navigates, whichever modifier is held (#SEJ2).
    void ctrlEnterOnAFolderStillNavigates() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("sub")));
        writeFile(temp.filePath(QStringLiteral("sub/inner.txt")), "x");
        FileExplorer explorer(temp.path());
        QString edited;
        explorer.onEditFile = [&edited](const QString &path) { edited = path; };
        explorer.show();
        QTRY_COMPARE(explorer.visiblePaths().size(), 1);
        QTRY_VERIFY(explorer.view()->currentIndex().isValid());
        explorer.view()->setFocus();
        QTest::keyClick(explorer.view(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(QFileInfo(explorer.root()).fileName(), QStringLiteral("sub"));
        QVERIFY(edited.isEmpty());
    }

    void typingInListStartsFilter() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral("zeta.txt")), "");
        writeFile(temp.filePath(QStringLiteral("eta.txt")), "");
        FileExplorer explorer(temp.path());
        explorer.show();
        QTRY_COMPARE(explorer.visiblePaths().size(), 2);
        explorer.view()->setFocus();
        QTest::keyClick(explorer.view(), Qt::Key_Z);
        QCOMPARE(explorer.filterEdit()->text(), QStringLiteral("z"));
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList({QStringLiteral("zeta.txt")}));
    }

    void typedFilterThenDownEnterOpensMatchNotFolder() {
        QTemporaryDir dir;
        QDir(dir.path()).mkdir(QStringLiteral("sub"));
        writeFile(dir.filePath(QStringLiteral("README.md")), "# hi\n");
        writeFile(dir.filePath(QStringLiteral("code.py")), "print(1)\n");
        relay::FileExplorer explorer(dir.path());
        explorer.resize(500, 400); explorer.show();
        QString opened;
        explorer.onOpenFile = [&opened](const QString &path) { opened = path; };
        QTRY_COMPARE(explorer.visiblePaths().size(), 3);
        explorer.view()->setFocus();
        QTest::keyClicks(explorer.view(), QStringLiteral("R"));
        QTest::keyClicks(explorer.filterEdit(), QStringLiteral("EAD"));
        QTRY_COMPARE(names(explorer.visiblePaths()), QStringList({QStringLiteral("README.md")}));
        QTest::keyClick(explorer.filterEdit(), Qt::Key_Down);
        QTest::keyClick(explorer.view(), Qt::Key_Return);
        QCOMPARE(QFileInfo(opened).fileName(), QStringLiteral("README.md"));
        QCOMPARE(explorer.root(), dir.path());
    }

    void previewPicksViewerByType() {
        QTemporaryDir temp;
        writeFile(temp.filePath(QStringLiteral("a.txt")), "plain text\n");
        writeFile(temp.filePath(QStringLiteral("b.py")), "def f():\n    return 1\n");
        writeFile(temp.filePath(QStringLiteral("c.md")), "# Title\n\nBody\n");
        writeFile(temp.filePath(QStringLiteral("e.bin")), QByteArray("\x00\x01\x02\x7f\xff\x00", 6));
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(temp.filePath(QStringLiteral("d.png"))));

        FilePreview preview;
        QString title;
        preview.onTitleChanged = [&title](const QString &t) { title = t; };
        QVERIFY(preview.open(temp.filePath(QStringLiteral("a.txt"))));
        QCOMPARE(preview.kind(), FilePreview::Kind::Text);
        QCOMPARE(preview.text(), QStringLiteral("plain text\n"));
        QCOMPARE(title, QStringLiteral("a.txt"));
        QVERIFY(preview.open(temp.filePath(QStringLiteral("b.py"))));
        QCOMPARE(preview.kind(), FilePreview::Kind::Text);
        QVERIFY(preview.open(temp.filePath(QStringLiteral("c.md"))));
        QCOMPARE(preview.kind(), FilePreview::Kind::Markdown);
        QVERIFY(preview.open(temp.filePath(QStringLiteral("d.png"))));
        QCOMPARE(preview.kind(), FilePreview::Kind::Image);
        QVERIFY(preview.open(temp.filePath(QStringLiteral("e.bin"))));
        QCOMPARE(preview.kind(), FilePreview::Kind::Info);
        QCOMPARE(preview.path(), QFileInfo(temp.filePath(QStringLiteral("e.bin"))).absoluteFilePath());
    }

    // Issue #3W58: a .md file opens rendered, and the one thing that leaves the render — a line
    // number from an output link — says so, so the view button can offer the render back.
    void markdownOpensRenderedAndALineNumberSaysItLeftIt() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("doc.md"));
        writeFile(path, "# Title\n\nBody\n\n- one\n- two\n");
        FilePreview preview;
        QVERIFY(preview.open(path));
        QCOMPARE(preview.kind(), FilePreview::Kind::Markdown);
        QVERIFY(!preview.showingSource());
        preview.goToLine(5);
        QVERIFY(preview.showingSource());
        // Reopening the file renders it again; a line of 0 never leaves the render.
        QVERIFY(preview.open(path));
        QVERIFY(!preview.showingSource());
        preview.goToLine(0);
        QVERIFY(!preview.showingSource());
    }

    // Issue #VXTF: the view button names the format, so "Source" cannot be read as the source of
    // whatever else the pane might be holding. The image preview's Fit/100% button is untouched.
    void markdownViewButtonNamesTheFormat() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("doc.md"));
        writeFile(path, "# Title\n\nBody\n");
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(temp.filePath(QStringLiteral("shot.png"))));

        FilePreview preview;
        auto *mode = preview.findChild<QToolButton *>(QStringLiteral("filePreviewMode"));
        QVERIFY(mode);
        QVERIFY(preview.open(path));
        QCOMPARE(mode->text(), QStringLiteral("Source (MD)"));
        preview.goToLine(3);
        QCOMPARE(mode->text(), QStringLiteral("Rendered (MD)"));
        QVERIFY(preview.open(temp.filePath(QStringLiteral("shot.png"))));
        QCOMPARE(mode->text(), QStringLiteral("100%"));
    }

    // Card #SEJ2: the ✎ button turns a read-only local preview into an editor, and Ctrl+S
    // writes the file back to its own path. Clicking ✎ is the slow path, so it teaches the
    // chord (WARP.md, "Shortcut hints").
    void aLocalTextFileEditsAndSaves() {
        QSettings settings;
        settings.remove(QStringLiteral("hints"));
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("note.txt"));
        writeFile(path, "before\n");
        FilePreview preview;
        QVERIFY(preview.open(path));
        QCOMPARE(preview.kind(), FilePreview::Kind::Text);
        auto *edit = preview.findChild<QToolButton *>(QStringLiteral("filePreviewEdit"));
        auto *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QVERIFY(edit && editor);
        QVERIFY(edit->isVisibleTo(&preview));
        QVERIFY(editor->isReadOnly());
        QVERIFY(!preview.isEditable());

        edit->click();
        QVERIFY(preview.isEditable());
        QVERIFY(!editor->isReadOnly());
        QVERIFY(!edit->isVisibleTo(&preview));
        QVERIFY(preview.notice().contains(QStringLiteral("Next time")));
        QVERIFY(preview.notice().contains(QStringLiteral("Ctrl+Enter")));
        QVERIFY(!preview.isDirty());

        // Typed, not set, so the document's modified flag — the ● and Save's signal — is real.
        editor->selectAll();
        editor->textCursor().insertText(QStringLiteral("after\n"));
        QVERIFY(preview.isDirty());
        QVERIFY(preview.title().startsWith(QStringLiteral("● ")));
        QVERIFY(preview.save());
        QVERIFY(!preview.isDirty());
        QCOMPARE(QString::fromUtf8(readFile(path)), QStringLiteral("after\n"));
        QVERIFY(preview.notice().contains(QStringLiteral("Saved")));
        settings.remove(QStringLiteral("hints"));
    }

    // A save that the disk refuses says why and keeps the buffer dirty (#SEJ2).
    void aLocalSaveThatFailsSaysWhy() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("locked.txt"));
        writeFile(path, "before\n");
        FilePreview preview;
        QVERIFY(preview.open(path));
        preview.startEditing();
        auto *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QVERIFY(editor);
        editor->textCursor().insertText(QStringLiteral("x"));
        QVERIFY(preview.isDirty());
        // A directory cannot be replaced by QSaveFile, even when packaging tests
        // run as root. chmod alone does not make a file unwritable for that user.
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QVERIFY(!preview.save());
        QVERIFY(preview.isDirty());
        QVERIFY(!preview.notice().isEmpty());
    }

    void wordWrapTogglesWithoutChangingTheDocument() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("wrap.md"));
        const QByteArray content = "# Title\n" + QByteArray(300, 'x') + " long line\n";
        writeFile(path, content);
        FilePreview preview;
        preview.resize(600, 300);
        preview.show();
        QVERIFY(preview.open(path));
        auto *wrap = preview.findChild<QToolButton *>(QStringLiteral("filePreviewWrap"));
        auto *edit = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        auto *mode = preview.findChild<QToolButton *>(QStringLiteral("filePreviewMode"));
        QVERIFY(wrap && edit && mode);
        QVERIFY(wrap->isHidden());
        preview.startEditing();
        QVERIFY(wrap->isVisible());
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::NoWrap);
        QTest::mouseClick(wrap, Qt::LeftButton);
        QVERIFY(wrap->isChecked());
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QCoreApplication::processEvents();
        QVERIFY(edit->document()->findBlockByNumber(1).layout()->lineCount() > 1);
        QCOMPARE(edit->toPlainText(), QString::fromUtf8(content));
        QVERIFY(!preview.isDirty());
        QTest::mouseClick(mode, Qt::LeftButton);
        QVERIFY(wrap->isHidden());
        QTest::mouseClick(mode, Qt::LeftButton);
        QVERIFY(wrap->isVisible());
        QVERIFY(wrap->isChecked());
        QTest::mouseClick(wrap, Qt::LeftButton);
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::NoWrap);
        QVERIFY(!preview.isDirty());
    }

    // Alt+Z (files.toggleWrap) moves the same switch the button does; a rendered Markdown has no
    // wrap, and the key says so instead of doing nothing silently.
    void toggleWrapMatchesTheButtonAndRefusesARenderedMarkdown() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("wrap-altz.md"));
        writeFile(path, "# Title\nplain source line\n");
        FilePreview preview;
        preview.resize(600, 300);
        preview.show();
        QVERIFY(preview.open(path));
        auto *wrap = preview.findChild<QToolButton *>(QStringLiteral("filePreviewWrap"));
        auto *edit = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        auto *mode = preview.findChild<QToolButton *>(QStringLiteral("filePreviewMode"));
        QVERIFY(wrap && edit && mode);
        preview.startEditing();
        QVERIFY(wrap->isVisible());
        QVERIFY(preview.toggleWrap());
        QVERIFY(wrap->isChecked());
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QVERIFY(preview.toggleWrap());
        QVERIFY(!wrap->isChecked());
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::NoWrap);
        // Back to the rendered view, the key is a no-op that reports it did nothing.
        QTest::mouseClick(mode, Qt::LeftButton);
        QVERIFY(!preview.showingSource());
        QVERIFY(!preview.toggleWrap());
        QVERIFY(!wrap->isChecked());
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::NoWrap);
    }

    // Card #SEJ2: a Markdown file is edited as source, so startEditing() leaves the render.
    void markdownEditingStartsInTheSourceView() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("doc.md"));
        writeFile(path, "# Title\n\nBody\n");
        FilePreview preview;
        QVERIFY(preview.open(path));
        QCOMPARE(preview.kind(), FilePreview::Kind::Markdown);
        QVERIFY(!preview.showingSource());
        preview.startEditing();
        QVERIFY(preview.isEditable());
        QVERIFY(preview.showingSource());
    }

    // The ✎ button is for text: an image has nothing to edit here (#SEJ2).
    void theEditButtonStaysAwayFromAnImage() {
        QTemporaryDir temp;
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(temp.filePath(QStringLiteral("shot.png"))));
        FilePreview preview;
        auto *edit = preview.findChild<QToolButton *>(QStringLiteral("filePreviewEdit"));
        QVERIFY(edit);
        QVERIFY(preview.open(temp.filePath(QStringLiteral("shot.png"))));
        QCOMPARE(preview.kind(), FilePreview::Kind::Image);
        QVERIFY(!edit->isVisibleTo(&preview));
        preview.startEditing();   // a no-op for an image
        QVERIFY(!preview.isEditable());
    }

    // Issue S1JP: a link inside a rendered Markdown preview is handed to the host for a pane of
    // its own. The preview never loads it, so the file that carried the link is still there.
    void aLinkInsideAPreviewIsHandedToTheHost() {
        QTemporaryDir temp;
        const QString from = temp.filePath(QStringLiteral("from.md"));
        const QString to = temp.filePath(QStringLiteral("to.md"));
        writeFile(from, "# From\n\n[to](to.md) and [gone](missing.md)\n");
        writeFile(to, "# To\n");

        FilePreview preview;
        QStringList opened;
        preview.onOpenLink = [&opened](const QString &path) { opened << path; };
        QVERIFY(preview.open(from));

        auto *browser = preview.findChild<QTextBrowser *>(QStringLiteral("filePreviewMarkdown"));
        QVERIFY(browser);
        Q_EMIT browser->anchorClicked(QUrl(QStringLiteral("to.md")));
        QCOMPARE(opened, QStringList{QFileInfo(to).absoluteFilePath()});
        // The pane stayed on the file that carried the link, rendered.
        QCOMPARE(preview.path(), QFileInfo(from).absoluteFilePath());
        QCOMPARE(preview.kind(), FilePreview::Kind::Markdown);
        QVERIFY(!preview.showingSource());

        // A link that resolves nowhere says so instead of opening a pane.
        Q_EMIT browser->anchorClicked(QUrl(QStringLiteral("missing.md")));
        QCOMPARE(opened.size(), 1);
        QVERIFY(preview.notice().contains(QStringLiteral("missing.md")));

        // A `#section` link stays inside the document.
        Q_EMIT browser->anchorClicked(QUrl(QStringLiteral("#from")));
        QCOMPARE(opened.size(), 1);
    }

    void largeTextIsTruncatedWithNotice() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("big.log"));
        writeFile(path, QByteArray(FilePreview::kMaxTextBytes + 1024, 'a'));
        FilePreview preview;
        QVERIFY(preview.open(path));
        QCOMPARE(preview.kind(), FilePreview::Kind::Text);
        QCOMPARE(preview.text().size(), int(FilePreview::kMaxTextBytes));
        QVERIFY(preview.notice().startsWith(QStringLiteral("Showing the first")));
    }

    // ----- syntax highlighting a big file (#MDSG) ----------------------------------------------

    // The whole file is on screen straight away and the colours fill in afterwards, in slices.
    void aBigFileIsShownFirstAndColouredAfterwards() {
        if (!FilePreview::syntaxHighlightingBuiltIn())
            QSKIP("built without KSyntaxHighlighting: nothing colours a file here");
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("big.cpp"));
        writeFile(path, sourceLines(8000));
        FilePreview preview;
        preview.resize(700, 500);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QVERIFY(preview.open(path));
        // Every line is there to read, and only the first screenfuls are coloured so far.
        QCOMPARE(preview.text().count(QLatin1Char('\n')), 8000);
        QVERIFY(preview.highlighting());
        const int eager = preview.highlightedBlocks();
        QVERIFY(eager > 0);
        QVERIFY(eager < 8000);
        // Jumping to the end before the colouring gets there is allowed to show plain text, never
        // the wrong colours; the frontier catches up and the last line ends up coloured like the
        // first. (Line 7994 is one of the `static const char *n = "…";` lines.)
        auto *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QVERIFY(editor);
        preview.goToLine(7994);
        // And it finishes by itself, from the top down, without anything being asked of it.
        QTRY_VERIFY_WITH_TIMEOUT(!preview.highlighting(), 20000);
        QCOMPARE(preview.highlightedBlocks(), preview.text().count(QLatin1Char('\n')) + 1);
        const QTextBlock last = editor->document()->findBlockByNumber(7993);
        QVERIFY(last.text().startsWith(QStringLiteral("static const char *")));
        QVERIFY(last.layout() && !last.layout()->formats().isEmpty());
    }

    // Opening another file stops the first one's colouring rather than letting two run at once.
    void openingAnotherFileStopsTheColouringOfTheFirst() {
        if (!FilePreview::syntaxHighlightingBuiltIn())
            QSKIP("built without KSyntaxHighlighting: nothing colours a file here");
        QTemporaryDir temp;
        const QString big = temp.filePath(QStringLiteral("big.cpp"));
        const QString small = temp.filePath(QStringLiteral("small.cpp"));
        writeFile(big, sourceLines(8000));
        writeFile(small, sourceLines(20));
        FilePreview preview;
        QVERIFY(preview.open(big));
        QVERIFY(preview.highlighting());
        QVERIFY(preview.open(small));
        // The small file is done in the first, synchronous slice: nothing is left running, and
        // what is coloured belongs to the file that is open now.
        QVERIFY(!preview.highlighting());
        QCOMPARE(preview.highlightedBlocks(), 21);
        // No slice of the first file's may fire afterwards and colour this one's blocks.
        QTest::qWait(60);
        QCOMPARE(preview.highlightedBlocks(), 21);
    }

    void missingPathReturnsFalse() {
        FilePreview preview;
        QVERIFY(!preview.open(QStringLiteral("/nonexistent/relay/file.txt")));
        QVERIFY(!preview.open(QDir::tempPath()));  // a directory is not previewable
        QCOMPARE(preview.kind(), FilePreview::Kind::None);
    }

    // ----- the right-click menus (issues #D60R, V9V1) ------------------------------------------

    void explorerMenuOffersTheFolderActions() {
        relay::FileMenuHost host;
        host.canNavigateTerminal = host.canPreview = host.canSetWorkspace = true;
        const QStringList ids = menuIds(relay::explorerMenu(relay::FileMenuTarget::Folder, host));
        QCOMPARE(ids, (QStringList{QStringLiteral("openInternal"), QStringLiteral("openExternal"),
                                   QStringLiteral("openFolder"), QStringLiteral("navigate"),
                                   QStringLiteral("copyPath"), QStringLiteral("copyRelativePath"),
                                   QStringLiteral("newFile"), QStringLiteral("newFolder"),
                                   QStringLiteral("rename"), QStringLiteral("delete"),
                                   QStringLiteral("workspace")}));
    }

    // Issue V9V1: the owner's three entries lead every menu, in their order, with their names.
    void everyMenuLeadsWithTheThreeOpenEntries() {
        relay::FileMenuHost host;
        host.canNavigateTerminal = host.canPreview = host.canSetWorkspace = true;
        const QStringList three{QStringLiteral("openInternal"), QStringLiteral("openExternal"),
                                QStringLiteral("openFolder")};
        for (auto target : {relay::FileMenuTarget::File, relay::FileMenuTarget::Folder})
            QCOMPARE(menuIds(relay::explorerMenu(target, host)).mid(0, 3), three);
        const auto items = relay::explorerMenu(relay::FileMenuTarget::File, host);
        QCOMPARE(items.at(0).label, QStringLiteral("Open internal"));
        QCOMPARE(items.at(1).label, QStringLiteral("Open external"));
        QCOMPARE(items.at(2).label, QStringLiteral("Open folder"));
        // A preview holds one file and has no clicked row: nothing to create, rename or delete.
        QCOMPARE(menuIds(relay::previewMenu(host)),
                 (QStringList{QStringLiteral("openInternal"), QStringLiteral("openExternal"),
                              QStringLiteral("openFolder"), QStringLiteral("copyPath")}));
    }

    // "Open internal" needs a preview host for a file; a folder opens in the explorer itself, so
    // it always works. Neither is ever missing from the menu — the order is a promise.
    void openInternalIsGreyedOutForAFileWithNoPreviewHost() {
        const auto file = relay::explorerMenu(relay::FileMenuTarget::File, relay::FileMenuHost{});
        QCOMPARE(file.at(0).id, QStringLiteral("openInternal"));
        QVERIFY(!file.at(0).enabled);
        QVERIFY(file.at(1).enabled);
        const auto folder = relay::explorerMenu(relay::FileMenuTarget::Folder, relay::FileMenuHost{});
        QCOMPARE(folder.at(0).id, QStringLiteral("openInternal"));
        QVERIFY(folder.at(0).enabled);
    }

    // A preview offers the three entries once a file is open, and nothing before that.
    void previewOffersTheMenuOnceAFileIsOpen() {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("doc.md"));
        writeFile(path, "# Title\n");
        FilePreview preview;
        QVERIFY(preview.menu().isEmpty());
        QVERIFY(preview.open(path));
        QCOMPARE(menuIds(preview.menu()).mid(0, 3),
                 (QStringList{QStringLiteral("openInternal"), QStringLiteral("openExternal"),
                              QStringLiteral("openFolder")}));
        for (const relay::FileMenuItem &item : preview.menu())
            if (!item.isSeparator()) QVERIFY(item.enabled);
    }

    void explorerMenuOnEmptySpaceActsOnTheShownFolder() {
        relay::FileMenuHost host;
        host.canNavigateTerminal = host.canPreview = host.canSetWorkspace = true;
        const QStringList ids = menuIds(relay::explorerMenu(relay::FileMenuTarget::None, host));
        // Nothing was clicked, so there is no "Open internal": that folder is already open here.
        QCOMPARE(ids, (QStringList{QStringLiteral("openExternal"), QStringLiteral("openFolder"),
                                   QStringLiteral("navigate"), QStringLiteral("newFile"),
                                   QStringLiteral("newFolder"), QStringLiteral("workspace")}));
        // Nothing was clicked, so there is nothing to copy, rename or delete.
        QVERIFY(!ids.contains(QStringLiteral("copyPath")));
        QVERIFY(!ids.contains(QStringLiteral("rename")));
        QVERIFY(!ids.contains(QStringLiteral("delete")));
    }

    void explorerMenuDropsEntriesTheHostCannotDo() {
        const QStringList ids = menuIds(relay::explorerMenu(relay::FileMenuTarget::File, relay::FileMenuHost{}));
        QVERIFY(!ids.contains(QStringLiteral("navigate")));
        QVERIFY(!ids.contains(QStringLiteral("workspace")));
        QVERIFY(ids.contains(QStringLiteral("copyPath")));
    }

    void explorerMenuGreysOutWritesInAReadOnlyFolder() {
        relay::FileMenuHost host;
        host.canPreview = true;
        host.writable = false;
        const auto items = relay::explorerMenu(relay::FileMenuTarget::File, host);
        for (const relay::FileMenuItem &item : items) {
            const bool writes = item.id == QStringLiteral("newFile") || item.id == QStringLiteral("newFolder")
                                || item.id == QStringLiteral("rename") || item.id == QStringLiteral("delete");
            QCOMPARE(item.enabled, !writes);
        }
    }

    void explorerMenuNeverHasALooseSeparator() {
        for (auto target : {relay::FileMenuTarget::None, relay::FileMenuTarget::File, relay::FileMenuTarget::Folder}) {
            for (int mask = 0; mask < 16; ++mask) {
                relay::FileMenuHost host;
                host.canNavigateTerminal = mask & 1;
                host.canPreview = mask & 2;
                host.canSetWorkspace = mask & 4;
                host.writable = mask & 8;
                const auto items = relay::explorerMenu(target, host);
                QVERIFY(!items.isEmpty());
                QVERIFY(!items.first().isSeparator());
                QVERIFY(!items.last().isSeparator());
                for (int i = 1; i < items.size(); ++i)
                    QVERIFY(!(items.at(i).isSeparator() && items.at(i - 1).isSeparator()));
                for (const relay::FileMenuItem &item : items)
                    if (!item.isSeparator()) QVERIFY(!item.label.isEmpty());
            }
        }
    }

    void explorerMenuForAPathFollowsWhatIsWiredUp() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("sub")));
        writeFile(temp.filePath(QStringLiteral("a.txt")), "x");
        FileExplorer explorer(temp.path());
        explorer.onNavigateHere = [](const QString &) {};
        explorer.onOpenInPreview = [](const QString &) {};
        QVERIFY(menuIds(explorer.menuFor(QString())).contains(QStringLiteral("navigate")));
        QVERIFY(!menuIds(explorer.menuFor(QString())).contains(QStringLiteral("rename")));
        // "Open internal" leads both, and is live for the file because onOpenInPreview is wired.
        QVERIFY(explorer.menuFor(temp.filePath(QStringLiteral("a.txt"))).at(0).enabled);
        QCOMPARE(explorer.menuFor(temp.filePath(QStringLiteral("a.txt"))).at(0).id, QStringLiteral("openInternal"));
        QCOMPARE(explorer.menuFor(temp.filePath(QStringLiteral("sub"))).at(0).id, QStringLiteral("openInternal"));
        // No "Set as agent workspace" until a host wires it up.
        QVERIFY(!menuIds(explorer.menuFor(temp.filePath(QStringLiteral("sub")))).contains(QStringLiteral("workspace")));
    }

    // ----- files and folders on a host (#S5SH) --------------------------------------------------
    //
    // A fake `ssh` on PATH plays the host: it answers with whatever the test put in `reply` and
    // exits with `code`, and keeps anything it was sent on stdin. Everything else — the fetch,
    // the viewer that is chosen, the editing, the save and the folder listing — is the real code.

    void aRemoteTextFileOpensEditableAndSaves() {
        fakeHost("14:1758153600:640\nlisten 8080;\n");
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/service.conf"))));
        QTRY_COMPARE_WITH_TIMEOUT(preview.kind(), FilePreview::Kind::Text, 10000);
        QVERIFY(preview.isRemote());
        QCOMPARE(preview.remoteHost(), m_host);
        QCOMPARE(preview.remotePath(), QStringLiteral("/srv/service.conf"));
        QCOMPARE(preview.text(), QStringLiteral("listen 8080;\n"));
        // The host is in the title, and nothing here is offered to this machine's applications.
        QCOMPARE(preview.title(), m_host + QStringLiteral(":/srv/service.conf"));
        QCOMPARE(menuIds(preview.menu()), (QStringList{QStringLiteral("openInternal"), QStringLiteral("copyPath")}));

        QPlainTextEdit *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QVERIFY(editor);
        QVERIFY(!editor->isReadOnly());     // a remote file is editable; a local one is not
        QVERIFY(!preview.isDirty());
        // Typed, not set: setPlainText() would clear the document's modified flag, which is
        // exactly the flag the ● and the Save button read.
        editor->selectAll();
        editor->textCursor().insertText(QStringLiteral("listen 9090;\n"));
        QVERIFY(preview.isDirty());
        QVERIFY(preview.title().startsWith(QStringLiteral("● ")));

        answerWith("14:1758153700:640\n", 0);
        QVERIFY(preview.save());
        QTRY_VERIFY_WITH_TIMEOUT(!preview.isDirty(), 10000);
        QCOMPARE(QString::fromUtf8(readFile(m_fake.filePath(QStringLiteral("stdin")))), QStringLiteral("listen 9090;\n"));
        QVERIFY(preview.notice().contains(QStringLiteral("Saved to")));
    }

    // A host's file is the one text path with no size cap, so it is where the highlighting limit
    // bites: past it the file is shown plain rather than colouring for minutes (#MDSG).
    void aRemoteFileTooBigToColourIsShownPlain() {
        QByteArray body(int(FilePreview::kMaxHighlightBytes) + 1, 'a');
        body[100] = '\n';
        fakeHost("14:1758153600:640\n" + body);
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/huge.cpp"))));
        QTRY_COMPARE_WITH_TIMEOUT(preview.kind(), FilePreview::Kind::Text, 20000);
        QCOMPARE(preview.highlightedBlocks(), 0);
        QVERIFY(!preview.highlighting());
    }

    // WARP.md's standing rule: the slow path teaches the fast one. Clicking Save says "Next time:
    // Ctrl+S" — through the same registry every other hint goes through, so it stops after a few
    // showings and never stacks on top of another hint.
    void theSaveButtonTeachesTheShortcut() {
        QSettings settings;
        settings.remove(QStringLiteral("hints"));
        fakeHost("14:1758153600:640\nlisten 8080;\n");
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/service.conf"))));
        QTRY_COMPARE_WITH_TIMEOUT(preview.kind(), FilePreview::Kind::Text, 10000);
        QPlainTextEdit *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QToolButton *saveButton = preview.findChild<QToolButton *>(QStringLiteral("filePreviewSave"));
        QVERIFY(editor && saveButton);

        editor->textCursor().insertText(QStringLiteral("x"));
        answerWith("15:1758153700:640\n", 0);
        saveButton->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview.notice().contains(QStringLiteral("Saved to")), 10000);
        QVERIFY(preview.notice().contains(QStringLiteral("Next time")));
        QVERIFY(preview.notice().contains(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));

        // Ctrl+S is the fast path itself: it never teaches what the user has just done.
        editor->textCursor().insertText(QStringLiteral("y"));
        answerWith("16:1758153800:640\n", 0);
        QVERIFY(preview.save());
        QTRY_VERIFY_WITH_TIMEOUT(!preview.isDirty(), 10000);
        QVERIFY(!preview.notice().contains(QStringLiteral("Next time")));
        settings.remove(QStringLiteral("hints"));
    }

    void aRemoteMarkdownFileRendersAndIsStillEditable() {
        fakeHost("30:1758153600:644\n# Title\n\nA [link](notes.md).\n");
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/readme.md"))));
        QTRY_COMPARE_WITH_TIMEOUT(preview.kind(), FilePreview::Kind::Markdown, 10000);
        QVERIFY(!preview.showingSource());                       // rendered, as a local .md opens
        QVERIFY(preview.text().startsWith(QStringLiteral("# Title")));
        QPlainTextEdit *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QVERIFY(editor && !editor->isReadOnly());                // and editable, unlike a local one
        // A line number takes it to the source, exactly as it does locally.
        preview.goToLine(1);
        QVERIFY(preview.showingSource());
    }

    void aRemoteImageIsShownAsAnImage() {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
        fakeHost(QByteArray::number(png.size()) + ":1758153600:644\n" + png);
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/logo.png"))));
        QTRY_COMPARE_WITH_TIMEOUT(preview.kind(), FilePreview::Kind::Image, 10000);
        QVERIFY(preview.notice().isEmpty());
    }

    void aRemotePdfIsPreviewedOrSaysItCannotBe() {
        // A minimal but real PDF. With Qt PDF built in it previews; without, the pane says so —
        // the same answer a local PDF gets in the same build.
        const QByteArray pdf = "%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n"
                               "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n"
                               "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 99 99]>>endobj\n"
                               "trailer<</Root 1 0 R>>\n%%EOF\n";
        fakeHost(QByteArray::number(pdf.size()) + ":1758153600:644\n" + pdf);
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/report.pdf"))));
        QTRY_VERIFY_WITH_TIMEOUT(preview.kind() == FilePreview::Kind::Pdf || preview.kind() == FilePreview::Kind::Info, 10000);
        if (preview.kind() == FilePreview::Kind::Info)
            QVERIFY(preview.notice().contains(QStringLiteral("not available")));
    }

    void aRemoteBinaryFileIsNotOpenedAsText() {
        fakeHost(QByteArray("8:1758153600:755\n\x7f" "ELF\0\0\0\0", 25));
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/srv/tool.txt"))));
        QTRY_COMPARE_WITH_TIMEOUT(preview.kind(), FilePreview::Kind::Info, 10000);
        QVERIFY(preview.notice().contains(QStringLiteral("binary")));
        QPlainTextEdit *editor = preview.findChild<QPlainTextEdit *>(QStringLiteral("filePreviewText"));
        QVERIFY(editor->isReadOnly());   // nothing to edit, so no Save to offer
    }

    void aFetchThatFailsSaysWhyAndOffersNothingToEdit() {
        fakeHost(QByteArray(), 11 /* UnreadableStatus */);
        FilePreview preview;
        QVERIFY(preview.open(remoteUrl(QStringLiteral("/etc/shadow"))));
        QTRY_VERIFY_WITH_TIMEOUT(preview.notice().contains(QStringLiteral("permission")), 10000);
        QVERIFY(!preview.save());
    }

    void aRemoteFolderListsNavigatesAndOpensFiles() {
        fakeHost("directory|4096|1758153600|sub\n"
                 "regular file|12|1758153600|readme.md\n"
                 "regular file|3|1758153600|.hidden\n");
        FileExplorer explorer(remoteFolderUrl(QStringLiteral("/srv")));
        QVERIFY(explorer.isRemote());
        QCOMPARE(explorer.remoteHost(), m_host);
        QTRY_COMPARE_WITH_TIMEOUT(explorer.visiblePaths().size(), 2, 10000);   // the dot file is hidden
        QCOMPARE(explorer.title(), m_host + QStringLiteral(":/srv"));
        QCOMPARE(explorer.visiblePaths(),
                 (QStringList{remoteFolderUrl(QStringLiteral("/srv/sub")), remoteUrl(QStringLiteral("/srv/readme.md"))}));
        // Hidden files are in the listing already: showing them costs no second round trip.
        explorer.setShowHidden(true);
        QCOMPARE(explorer.visiblePaths().size(), 3);
        explorer.setShowHidden(false);

        // A read-only menu: no rename, no delete, no "open externally" for another machine's file.
        const QStringList ids = menuIds(explorer.menuFor(remoteUrl(QStringLiteral("/srv/readme.md"))));
        QCOMPARE(ids, (QStringList{QStringLiteral("openInternal"), QStringLiteral("copyPath")}));

        // A file opens through the host, a folder navigates into it, and ↑ comes back.
        QString opened;
        explorer.onOpenFile = [&](const QString &path) { opened = path; };
        QVERIFY(explorer.activateRow(1));
        QCOMPARE(opened, remoteUrl(QStringLiteral("/srv/readme.md")));
        answerWith("regular file|1|1758153600|deeper.txt\n", 0);
        QVERIFY(explorer.activateRow(0));
        QCOMPARE(explorer.root(), remoteFolderUrl(QStringLiteral("/srv/sub")));
        QTRY_COMPARE_WITH_TIMEOUT(explorer.visiblePaths().size(), 1, 10000);
        QCOMPARE(explorer.visiblePaths().first(), remoteUrl(QStringLiteral("/srv/sub/deeper.txt")));
        answerWith("directory|4096|1758153600|sub\n", 0);
        explorer.goUp();
        QCOMPARE(explorer.root(), remoteFolderUrl(QStringLiteral("/srv")));
        QTRY_COMPARE_WITH_TIMEOUT(explorer.visiblePaths().size(), 1, 10000);
    }

    void aFolderWithNoConnectionSaysSoRatherThanLookingEmpty() {
        relay::remote::forgetLogin(m_host);
        FileExplorer explorer(remoteFolderUrl(QStringLiteral("/srv")));
        QVERIFY(explorer.isRemote());
        QVERIFY(explorer.visiblePaths().isEmpty());
        QLabel *notice = explorer.findChild<QLabel *>(QStringLiteral("filePreviewNotice"));
        QVERIFY(notice);
        QTRY_VERIFY_WITH_TIMEOUT(notice->text().contains(QStringLiteral("no connection")), 5000);
    }

    // ----- single click (issue #0C7V) ----------------------------------------------------------

    void singleClickIsOnByDefaultAndFollowsTheSetting() {
        QSettings settings;
        settings.remove(QStringLiteral("files/single_click"));
        QVERIFY(FileExplorer::singleClickDefault());   // on when the setting has never been touched
        QTemporaryDir temp;
        FileExplorer explorer(temp.path());
        QVERIFY(explorer.singleClick());
        explorer.setSingleClick(false);
        QVERIFY(!explorer.singleClick());
        settings.setValue(QStringLiteral("files/single_click"), false);
        QVERIFY(!FileExplorer::singleClickDefault());
        FileExplorer other(temp.path());
        QVERIFY(!other.singleClick());                 // a new explorer starts from the setting
        settings.remove(QStringLiteral("files/single_click"));
    }

private:
    // ----- the fake host (#S5SH) ----------------------------------------------------------------
    QByteArray readFile(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    QString remoteUrl(const QString &path) const { return relay::remote::fileUrl(m_host, path); }
    QString remoteFolderUrl(const QString &path) const { return relay::remote::folderUrl(m_host, path); }
    void answerWith(const QByteArray &reply, int code = 0) {
        writeFile(m_fake.filePath(QStringLiteral("reply")), reply);
        writeFile(m_fake.filePath(QStringLiteral("code")), QByteArray::number(code));
    }
    // A fake `ssh` first on PATH, a socket file to stand in for the control master, and a login
    // announced for `m_host` — everything `FilePreview` and `FileExplorer` look for.
    void fakeHost(const QByteArray &reply, int code = 0) {
        if (m_savedPath.isNull()) m_savedPath = qgetenv("PATH");
        const QString bin = m_fake.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        const QString script = QDir(bin).filePath(QStringLiteral("ssh"));
        writeFile(script, (QStringLiteral("#!/bin/sh\n"
                                          "F=%1\n"
                                          "cat > \"$F/stdin\"\n"
                                          "cat \"$F/reply\"\n"
                                          "exit \"$(cat \"$F/code\")\"\n").arg(m_fake.path())).toUtf8());
        QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("PATH", bin.toLocal8Bit() + ':' + m_savedPath);
        const QString socket = m_fake.filePath(QStringLiteral("socket"));
        writeFile(socket, QByteArray());
        relay::remote::announceLogin(m_host, socket);
        answerWith(reply, code);
    }

private slots:
    void cleanup() {
        if (!m_savedPath.isNull()) { qputenv("PATH", m_savedPath); m_savedPath = QByteArray(); }
        relay::remote::forgetLogin(m_host);
    }

    // The one visible difference the fix makes, as two PNGs: a big file the moment it is on
    // screen, and the same file once the colouring has worked its way down it (#MDSG).
    void bigFileScreenshots() {
        const QString directory = qEnvironmentVariable("RELAY_SHOT_DIR");
        if (directory.isEmpty()) QSKIP("set RELAY_SHOT_DIR=<dir> to write the file pane as a PNG");
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("big.cpp"));
        writeFile(path, sourceLines(8000));
        FilePreview preview;
        preview.resize(900, 700);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QVERIFY(preview.open(path));
        QVERIFY(preview.grab().save(directory + QStringLiteral("/file-pane-open.png")));
        QTRY_VERIFY_WITH_TIMEOUT(!preview.highlighting(), 30000);
        QVERIFY(preview.grab().save(directory + QStringLiteral("/file-pane-coloured.png")));
        qInfo("wrote %s/file-pane-open.png and file-pane-coloured.png", qPrintable(directory));
    }

    // What opening a file costs: the freeze before the text is on screen, and the GUI time the
    // whole thing takes including the colouring that now happens afterwards (#MDSG). It prints
    // rather than asserts — the numbers are the machine's — so it only runs when asked for.
    void openCost() {
        if (qEnvironmentVariableIsEmpty("RELAY_PERF_BENCH"))
            QSKIP("set RELAY_PERF_BENCH=1 to time opening a file");
        QTemporaryDir temp;
        FilePreview preview;
        preview.resize(900, 700);
        preview.show();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        // RELAY_PERF_FILE names a real file to open as well, which is how the 15,830-line
        // src/Pane.h of the profile is timed without the bench depending on it being there.
        QStringList paths;
        for (const int lines : {300, 15830}) {
            const QString path = temp.filePath(QStringLiteral("bench-%1.cpp").arg(lines));
            writeFile(path, sourceLines(lines));
            paths << path;
        }
        if (!qEnvironmentVariable("RELAY_PERF_FILE").isEmpty()) paths << qEnvironmentVariable("RELAY_PERF_FILE");
        for (const QString &path : paths) {
            const QByteArray body = readFile(path);
            const int lines = body.count('\n');
            for (const bool warm : {false, true}) {
                const double start = cpuMs();
                QVERIFY(preview.open(path));
                preview.repaint();
                const double firstPaint = cpuMs() - start;
                QTRY_VERIFY_WITH_TIMEOUT(!preview.highlighting(), 60000);
                preview.repaint();
                qInfo("%d lines (%lld B), %s: %.0f ms GUI CPU to first paint, %.0f ms in all",
                      lines, qint64(body.size()), warm ? "warm" : "cold", firstPaint, cpuMs() - start);
                preview.open(temp.path());   // nothing: the next open is not the same path
            }
        }
    }

private:
    QTemporaryDir m_fake;
    QByteArray m_savedPath;
    const QString m_host = QStringLiteral("testhost");
};

QTEST_MAIN(FilePanesTests)
#include "filepanes_test.moc"
