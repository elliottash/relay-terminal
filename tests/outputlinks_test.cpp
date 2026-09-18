// SPDX-License-Identifier: GPL-3.0-or-later
// Clickable paths (#YZTK) and the keyboard walk over them (#GWXM): which spans of a line of
// terminal output are links, what they resolve to against a pane's working directory, and how
// the keyboard cursor moves over the ordered list.
//
// The filesystem is a stub (`probe`), so the rules are tested without touching disk: the same
// line is a link in one directory and nothing in another, which is exactly the behaviour the
// cards ask for ("a path that does not exist is not turned into a link").
#include "OutputLinks.h"

#include <QSet>
#include <QTest>

using namespace relay::links;

namespace {

const QString kCwd = QStringLiteral("/home/dev/project");
const QString kHome = QStringLiteral("/home/dev");

// Absolute paths that "exist": the set that ends with '/' are directories.
Probe probe()
{
    static const QSet<QString> files = {
        QStringLiteral("/home/dev/project/src/main.cpp"),
        QStringLiteral("/home/dev/project/src/app.ts"),
        QStringLiteral("/home/dev/project/src/main.rs"),
        QStringLiteral("/home/dev/project/code.py"),
        QStringLiteral("/home/dev/project/notes.txt"),
        QStringLiteral("/home/dev/project/Makefile"),
        QStringLiteral("/home/dev/project/my file.txt"),
        QStringLiteral("/home/dev/project/tests/test_links.py"),
        QStringLiteral("/home/dev/project/weird:name"),
        QStringLiteral("/home/dev/.bashrc"),
        QStringLiteral("/etc/hosts"),
    };
    static const QSet<QString> dirs = {
        QStringLiteral("/home/dev/project"),
        QStringLiteral("/home/dev/project/src"),
        QStringLiteral("/home/dev/project/tests"),
        QStringLiteral("/home/dev"),
        QStringLiteral("/etc"),
    };
    return [](const QString &path) {
        if (dirs.contains(path)) return Entry::Directory;
        if (files.contains(path)) return Entry::File;
        return Entry::Missing;
    };
}

QVector<Found> found(const QString &text) { return scan(text, kCwd, kHome, probe()); }

QStringList targets(const QString &text)
{
    QStringList out;
    for (const Found &f : found(text)) out << f.target.target;
    return out;
}

} // namespace

class OutputLinksTests : public QObject {
    Q_OBJECT

private slots:
    // ---- the path formats the card lists ------------------------------------------------

    void absolutePath()
    {
        const auto links = found(QStringLiteral("wrote /etc/hosts and /home/dev/.bashrc"));
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[0].target.target, QStringLiteral("/etc/hosts"));
        QVERIFY(!links[0].target.directory);
        QCOMPARE(links[1].target.target, QStringLiteral("/home/dev/.bashrc"));
    }

    void relativePathResolvesAgainstTheWorkingDirectory()
    {
        QCOMPARE(targets(QStringLiteral("src/main.cpp")), {QStringLiteral("/home/dev/project/src/main.cpp")});
        QCOMPARE(targets(QStringLiteral("./src/main.cpp")), {QStringLiteral("/home/dev/project/src/main.cpp")});
        QCOMPARE(targets(QStringLiteral("../project/src/main.cpp")), {QStringLiteral("/home/dev/project/src/main.cpp")});
        // The same line in another directory is not a link at all.
        QVERIFY(scan(QStringLiteral("src/main.cpp"), QStringLiteral("/tmp"), kHome, probe()).isEmpty());
    }

    void barePathsFromLsOutput()
    {
        const auto links = found(QStringLiteral("code.py  Makefile  notes.txt  src  tests"));
        QCOMPARE(links.size(), 5);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/code.py"));
        QVERIFY(!links[0].target.directory);
        QCOMPARE(links[3].target.target, QStringLiteral("/home/dev/project/src"));
        QVERIFY(links[3].target.directory); // a folder: the explorer pane, not the preview
        QVERIFY(links[4].target.directory);
    }

    void tildePaths()
    {
        QCOMPARE(targets(QStringLiteral("edit ~/.bashrc")), {QStringLiteral("/home/dev/.bashrc")});
        QCOMPARE(targets(QStringLiteral("cd ~")), {QStringLiteral("/home/dev")});
        QCOMPARE(targets(QStringLiteral("~/project/code.py:3")), {QStringLiteral("/home/dev/project/code.py")});
    }

    void fileLineAndColumn()
    {
        // grep -n
        auto links = found(QStringLiteral("notes.txt:42:the matching text"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/notes.txt"));
        QCOMPARE(links[0].target.line, 42);
        QCOMPARE(links[0].target.column, -1);
        // gcc / clang, with the trailing colon
        links = found(QStringLiteral("src/main.cpp:42:17: error: expected ';'"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/main.cpp"));
        QCOMPARE(links[0].target.line, 42);
        QCOMPARE(links[0].target.column, 17);
        // cargo
        links = found(QStringLiteral("  --> src/main.rs:4:9"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/main.rs"));
        QCOMPARE(links[0].target.line, 4);
        QCOMPARE(links[0].target.column, 9);
    }

    void aNameThatReallyContainsAColonWinsOverFileLine()
    {
        const auto links = found(QStringLiteral("weird:name"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/weird:name"));
        QCOMPARE(links[0].target.line, -1);
    }

    void quotedPathWithSpaces()
    {
        // GNU ls quotes names with spaces on a terminal.
        auto links = found(QStringLiteral("code.py  'my file.txt'  notes.txt"));
        QCOMPARE(links.size(), 3);
        QCOMPARE(links[1].target.target, QStringLiteral("/home/dev/project/my file.txt"));
        // The underline covers the name, not the quotes.
        QCOMPARE(links[1].candidate.text, QStringLiteral("my file.txt"));
        links = found(QStringLiteral("cat \"my file.txt\":7"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/my file.txt"));
        QCOMPARE(links[0].target.line, 7);
    }

    void backslashEscapedSpaces()
    {
        const auto links = found(QStringLiteral("cp my\\ file.txt /tmp"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/my file.txt"));
    }

    void pythonTraceback()
    {
        const QString line = QStringLiteral("  File \"/home/dev/project/code.py\", line 12, in <module>");
        const auto links = found(line);
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/code.py"));
        QCOMPARE(links[0].target.line, 12);
        // The span is the path itself, so the underline sits under it.
        QCOMPARE(line.mid(links[0].candidate.start, links[0].candidate.length),
                 QStringLiteral("/home/dev/project/code.py"));
        // A relative one, single-quoted, resolved against the pane's directory.
        QCOMPARE(targets(QStringLiteral("  File 'code.py', line 3, in main")),
                 {QStringLiteral("/home/dev/project/code.py")});
    }

    void pytestNodeIdsAndFailureLines()
    {
        QCOMPARE(targets(QStringLiteral("FAILED tests/test_links.py::test_scan - AssertionError")),
                 {QStringLiteral("/home/dev/project/tests/test_links.py")});
        const auto links = found(QStringLiteral("tests/test_links.py:88: AssertionError"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.line, 88);
    }

    void tscAndMsvcParentheses()
    {
        const auto links = found(QStringLiteral("src/app.ts(12,5): error TS2304: Cannot find name 'x'."));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/app.ts"));
        QCOMPARE(links[0].target.line, 12);
        QCOMPARE(links[0].target.column, 5);
        // The underline covers `src/app.ts(12,5)`.
        QCOMPARE(links[0].candidate.length, 16);
    }

    void eslintStyleOutput()
    {
        // eslint prints the file on its own line, then "line:col  severity  message".
        QCOMPARE(targets(QStringLiteral("/home/dev/project/src/app.ts")),
                 {QStringLiteral("/home/dev/project/src/app.ts")});
        QVERIFY(found(QStringLiteral("  12:5  error  'x' is not defined  no-undef")).isEmpty());
    }

    void stackFrameInsideParentheses()
    {
        const auto links = found(QStringLiteral("    at Object.<anonymous> (/home/dev/project/src/app.ts:12:5)"));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("/home/dev/project/src/app.ts"));
        QCOMPARE(links[0].target.line, 12);
        QCOMPARE(links[0].target.column, 5);
    }

    // ---- what must NOT become a link ----------------------------------------------------

    void urlsStayUrls()
    {
        auto links = found(QStringLiteral("see http://example.com/src/main.cpp for details"));
        QCOMPARE(links.size(), 1);
        QVERIFY(links[0].target.kind == Kind::Url);
        QCOMPARE(links[0].target.target, QStringLiteral("http://example.com/src/main.cpp"));
        // A trailing sentence stop is not part of the URL.
        links = found(QStringLiteral("docs at https://relay.test/x."));
        QCOMPARE(links.size(), 1);
        QCOMPARE(links[0].target.target, QStringLiteral("https://relay.test/x"));
        // Unknown schemes are still URLs, never relative paths.
        const auto c = candidates(QStringLiteral("ssh://host/src"));
        QCOMPARE(c.size(), 1);
        QVERIFY(c[0].kind == Kind::Url);
    }

    void flagsAreNotPaths()
    {
        QVERIFY(candidates(QStringLiteral("--flag -v --output=src/main.cpp")).isEmpty());
        QVERIFY(found(QStringLiteral("ls --color=always -la")).isEmpty());
    }

    void bareNumbersAndVersionsAreNotPaths()
    {
        QVERIFY(candidates(QStringLiteral("42 1.2.3 192.168.0.1 12:30:05 3,4")).isEmpty());
    }

    void proseThatLooksLikeAPathIsNotOne()
    {
        QVERIFY(found(QStringLiteral("choose either a/b or c/d")).isEmpty());
        QVERIFY(found(QStringLiteral("the src/missing.cpp file was deleted")).isEmpty());
    }

    // ---- resolution and probing ---------------------------------------------------------

    void splitLocationCases()
    {
        QString path;
        int line = 0, column = 0;
        QVERIFY(splitLocation(QStringLiteral("src/main.cpp:42:7"), &path, &line, &column));
        QCOMPARE(path, QStringLiteral("src/main.cpp"));
        QCOMPARE(line, 42);
        QCOMPARE(column, 7);
        QVERIFY(splitLocation(QStringLiteral("a.py:9:"), &path, &line, &column));
        QCOMPARE(path, QStringLiteral("a.py"));
        QCOMPARE(line, 9);
        QCOMPARE(column, -1);
        QVERIFY(splitLocation(QStringLiteral("plain.txt"), &path, &line, &column));
        QCOMPARE(path, QStringLiteral("plain.txt"));
        QCOMPARE(line, -1);
        QVERIFY(!splitLocation(QStringLiteral(":::"), &path, &line, &column));
    }

    void directoriesAreMarked()
    {
        const auto links = found(QStringLiteral("cd /home/dev/project/src"));
        QCOMPARE(links.size(), 1);
        QVERIFY(links[0].target.directory);
    }

    void severalLinksKeepReadingOrder()
    {
        const QString line = QStringLiteral("src/main.cpp:4 https://relay.test/x notes.txt");
        const auto links = found(line);
        QCOMPARE(links.size(), 3);
        QVERIFY(links[0].candidate.start < links[1].candidate.start);
        QVERIFY(links[1].candidate.start < links[2].candidate.start);
        QCOMPARE(links[1].target.kind, Kind::Url);
    }

    // ---- the keyboard cursor (#GWXM) -----------------------------------------------------

    void cursorStartsAtTheNewestLink()
    {
        Cursor cursor;
        cursor.setCount(3);
        QVERIFY(!cursor.active());
        QCOMPARE(cursor.step(-1), 2);
        QVERIFY(cursor.active());
        Cursor forwards;
        forwards.setCount(3);
        QCOMPARE(forwards.step(1), 2); // the first step lands on the newest either way
    }

    void cursorStepsAndWraps()
    {
        Cursor cursor;
        cursor.setCount(3);
        QCOMPARE(cursor.step(-1), 2);
        QCOMPARE(cursor.step(-1), 1);
        QCOMPARE(cursor.step(-1), 0);
        QCOMPARE(cursor.step(-1), 2); // wraps at the oldest
        QCOMPARE(cursor.step(1), 0);  // and at the newest
        QCOMPARE(cursor.step(0), 0);  // a re-read does not move
    }

    void cursorCancelsAndSurvivesAShrinkingList()
    {
        Cursor cursor;
        cursor.setCount(4);
        QCOMPARE(cursor.step(-1), 3);
        cursor.setCount(2);
        QCOMPARE(cursor.index(), 1);
        cursor.cancel();
        QVERIFY(!cursor.active());
        QCOMPARE(cursor.index(), -1);
        cursor.setCount(0);
        QCOMPARE(cursor.step(-1), -1);
        QVERIFY(!cursor.active());
    }
};

QTEST_MAIN(OutputLinksTests)
#include "outputlinks_test.moc"
