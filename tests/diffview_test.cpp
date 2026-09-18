// SPDX-License-Identifier: GPL-3.0-or-later
#include "DiffView.h"

#include <QStringList>
#include <QTest>

using relay::DiffLine;
using relay::DiffView;
using relay::ParsedDiff;

namespace {
// What a write_file tool call puts in its preview (backend/relay_core/tools.py): a heading, the
// absolute path, the difflib diff, and a byte count after it.
QString writePreview() {
    return QStringLiteral(
        "WRITE FILE\n"
        "\n"
        "/home/elliott/project/x.py\n"
        "\n"
        "--- a/x.py\n"
        "+++ b/x.py\n"
        "@@ -1,4 +1,5 @@\n"
        " import os\n"
        "-old = 1\n"
        "+new = 1\n"
        "+extra = 2\n"
        " \n"
        " print(new)\n"
        "\n"
        "Old bytes: 40; new bytes: 52.");
}

// Two hunks, whose content lines start with the same markers a header does.
QString twoHunks() {
    return QStringLiteral(
        "--- a/docs/doc.md\n"
        "+++ b/docs/doc.md\n"
        "@@ -1,3 +1,3 @@\n"
        " intro\n"
        "---- old rule\n"
        "-+++ old rule\n"
        "+--- new rule\n"
        "++++ new rule\n"
        "@@ -10,1 +10,2 @@\n"
        " tail\n"
        "+@@ not a hunk\n");
}

QString kinds(const ParsedDiff &diff) {
    static const char *const names[] = {"H", "@", " ", "+", "-", "\\", "?"};
    QString out;
    for (const DiffLine &line : diff.lines) out += QLatin1String(names[line.kind]);
    return out;
}
}  // namespace

class DiffViewTests : public QObject {
    Q_OBJECT
private slots:
    void parsesTheDiffOutOfAToolPreview() {
        const ParsedDiff diff = relay::parseUnifiedDiff(writePreview());
        QCOMPARE(diff.oldPath, QStringLiteral("x.py"));       // the a/ and b/ prefixes are gone
        QCOMPARE(diff.newPath, QStringLiteral("x.py"));
        QVERIFY(!diff.created);
        QCOMPARE(diff.added, 2);
        QCOMPARE(diff.removed, 1);
        QCOMPARE(diff.fileName(), QStringLiteral("x.py"));
        // The heading and the path before the diff, and the byte count after it, are not lines.
        QCOMPARE(kinds(diff), QStringLiteral("HH@ -++  "));
        QCOMPARE(diff.lines.first().text, QStringLiteral("--- a/x.py"));
        QCOMPARE(diff.lines.last().text, QStringLiteral(" print(new)"));
    }

    void numbersBothSidesOfAHunk() {
        const QVector<DiffLine> lines = relay::parseUnifiedDiff(writePreview()).lines;
        // headers and the hunk header have no numbers at all
        QCOMPARE(lines.at(0).oldLine, 0);
        QCOMPARE(lines.at(2).newLine, 0);
        QCOMPARE(lines.at(3).oldLine, 1);   // " import os"
        QCOMPARE(lines.at(3).newLine, 1);
        QCOMPARE(lines.at(4).oldLine, 2);   // "-old = 1" exists only in the old file
        QCOMPARE(lines.at(4).newLine, 0);
        QCOMPARE(lines.at(5).oldLine, 0);   // "+new = 1" only in the new one
        QCOMPARE(lines.at(5).newLine, 2);
        QCOMPARE(lines.at(6).newLine, 3);
        QCOMPARE(lines.at(7).oldLine, 3);   // the blank context line keeps counting
        QCOMPARE(lines.at(7).newLine, 4);
        QCOMPARE(lines.at(8).oldLine, 4);
        QCOMPARE(lines.at(8).newLine, 5);
    }

    void extractsThePreviewsDiffAsText() {
        const QString diff = relay::diffFromPreview(writePreview());
        QVERIFY(diff.startsWith(QStringLiteral("--- a/x.py\n")));
        QVERIFY(diff.endsWith(QStringLiteral(" print(new)\n")));
        QVERIFY(!diff.contains(QStringLiteral("WRITE FILE")));
        QVERIFY(!diff.contains(QStringLiteral("Old bytes")));
        // Re-parsing the extracted text is the same diff.
        QCOMPARE(int(relay::parseUnifiedDiff(diff).lines.size()), 9);
    }

    void aNewFileIsCreated() {
        const ParsedDiff diff = relay::parseUnifiedDiff(QStringLiteral(
            "--- /dev/null\n+++ b/pkg/new.py\n@@ -0,0 +1,3 @@\n+a\n+b\n+c\n"));
        QVERIFY(diff.created);
        QCOMPARE(diff.oldPath, QStringLiteral("/dev/null"));
        QCOMPARE(diff.added, 3);
        QCOMPARE(diff.removed, 0);
        QCOMPARE(diff.path(), QStringLiteral("pkg/new.py"));
        QCOMPARE(diff.fileName(), QStringLiteral("new.py"));
        QCOMPARE(diff.lines.at(3).newLine, 1);
        QCOMPARE(diff.lines.at(5).newLine, 3);
        QCOMPARE(diff.lines.at(5).oldLine, 0);
    }

    void hunkCountsDecideWhatALineIs() {
        const ParsedDiff diff = relay::parseUnifiedDiff(twoHunks());
        // `---- old rule`, `-+++ old rule` and `+@@ not a hunk` are content, not headers.
        QCOMPARE(kinds(diff), QStringLiteral("HH@ --++@ +"));
        QCOMPARE(diff.added, 3);
        QCOMPARE(diff.removed, 2);
        QCOMPARE(diff.lines.at(4).text, QStringLiteral("---- old rule"));
        QCOMPARE(diff.lines.at(4).oldLine, 2);
        QCOMPARE(diff.lines.at(9).oldLine, 10);   // the second hunk starts where its header says
        QCOMPARE(diff.lines.at(10).newLine, 11);
        QCOMPARE(diff.path(), QStringLiteral("docs/doc.md"));
    }

    void aHunkThatPromisedMoreLinesThanItHas() {
        // The header says seven old lines and there are three: the next `@@` still opens a hunk,
        // so a hand-written or truncated diff does not lose everything after the first mistake.
        const ParsedDiff diff = relay::parseUnifiedDiff(QStringLiteral(
            "--- a/x\n+++ b/x\n@@ -1,7 +1,7 @@\n one\n-two\n+2\n@@ -40,1 +40,2 @@\n forty\n+41\n"));
        QCOMPARE(kinds(diff), QStringLiteral("HH@ -+@ +"));
        QCOMPARE(diff.added, 2);
        QCOMPARE(diff.removed, 1);
        QCOMPARE(diff.lines.at(7).oldLine, 40);
    }

    void crlfAndAMissingFinalNewline() {
        const ParsedDiff diff = relay::parseUnifiedDiff(QStringLiteral(
            "--- a/x\r\n+++ b/x\r\n@@ -1,2 +1,2 @@\r\n one\r\n-two\r\n"
            "\\ No newline at end of file\r\n+two!\r\n\\ No newline at end of file\r\n"));
        QCOMPARE(kinds(diff), QStringLiteral("HH@ -\\+\\"));
        QCOMPARE(diff.added, 1);
        QCOMPARE(diff.removed, 1);
        for (const DiffLine &line : diff.lines) QVERIFY(!line.text.contains(QLatin1Char('\r')));
        // The marker line belongs to neither side, so the numbering carries on past it.
        QCOMPARE(diff.lines.at(5).oldLine, 0);
        QCOMPARE(diff.lines.at(6).newLine, 2);
    }

    void aPreviewWithoutADiff() {
        const QString preview = QStringLiteral("WRITE FILE\n\n/tmp/x.py\n\n(No text changes)\n\nOld bytes: 3; new bytes: 3.");
        const ParsedDiff diff = relay::parseUnifiedDiff(preview);
        QVERIFY(diff.isEmpty());
        QCOMPARE(diff.added, 0);
        QCOMPARE(diff.removed, 0);
        QVERIFY(relay::diffFromPreview(preview).isEmpty());
        QVERIFY(relay::parseUnifiedDiff(QString()).isEmpty());
    }

    void titleNamesTheFileAndTheCounts() {
        const ParsedDiff diff = relay::parseUnifiedDiff(writePreview());
        QCOMPARE(relay::diffTitle(diff), QStringLiteral("x.py  +2 −1"));
        QCOMPARE(relay::diffTitle(diff, QStringLiteral("src/x.py")), QStringLiteral("src/x.py  +2 −1"));
        QCOMPARE(relay::diffTitle(ParsedDiff{}), QStringLiteral("Diff  +0 −0"));
    }

    // ----- the widget ---------------------------------------------------------------------------

    void viewShowsTheDiffWithItsMarkers() {
        DiffView view;
        view.setDiff(QStringLiteral("x.py"), writePreview());
        QCOMPARE(view.title(), QStringLiteral("x.py  +2 −1"));
        QCOMPARE(view.lineCount(), 9);
        QCOMPARE(view.hunkCount(), 1);
        const QStringList shown = view.plainText().split(QLatin1Char('\n'));
        QCOMPARE(int(shown.size()), 9);
        // A copy out of the view is still a diff: the numbers are painted in the gutter beside the
        // text, and the +/- markers are part of the text.
        QCOMPARE(shown.at(4), QStringLiteral("-old = 1"));
        QCOMPARE(shown.at(5), QStringLiteral("+new = 1"));
        QVERIFY(!view.plainText().contains(QStringLiteral("Old bytes")));
        view.resize(700, 420);
        QVERIFY(!view.grab().isNull());   // the gutter paints without a window
    }

    void nAndPWalkTheHunks() {
        DiffView view;
        view.setDiff(QString(), twoHunks());
        QCOMPARE(view.hunkCount(), 2);
        QCOMPARE(view.title(), QStringLiteral("doc.md  +3 −2"));   // the name comes from the diff
        QVERIFY(view.nextHunk());
        QVERIFY(view.nextHunk());
        QVERIFY(!view.nextHunk());
        QVERIFY(view.previousHunk());
        QVERIFY(!view.previousHunk());
    }

    void viewSaysSoWhenThereIsNoDiff() {
        DiffView view;
        view.setDiff(QStringLiteral("x.py"), QStringLiteral("(No text changes)"));
        QCOMPARE(view.lineCount(), 0);
        QCOMPARE(view.title(), QStringLiteral("x.py  +0 −0"));
        QCOMPARE(view.plainText(), QStringLiteral("(No text changes)"));
    }
};

QTEST_MAIN(DiffViewTests)
#include "diffview_test.moc"
