// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FilePanes.h"
#include "RemoteFiles.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>

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

private:
    QTemporaryDir m_fake;
    QByteArray m_savedPath;
    const QString m_host = QStringLiteral("testhost");
};

QTEST_MAIN(FilePanesTests)
#include "filepanes_test.moc"
