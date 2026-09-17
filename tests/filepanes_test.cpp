// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilePanes.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
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
};

QTEST_MAIN(FilePanesTests)
#include "filepanes_test.moc"
