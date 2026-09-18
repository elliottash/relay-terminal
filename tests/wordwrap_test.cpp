// SPDX-License-Identifier: GPL-3.0-or-later
// Relay's own text in the terminal breaks between words, never inside one.
#include "MarkdownAnsi.h"
#include "WordWrap.h"

#include <QRegularExpression>
#include <QTest>

using relay::MarkdownAnsi;
using relay::WordWrap;

namespace {

QString wrap(const QString &text, int columns, bool streamed = false) {
    WordWrap w;
    w.setColumns(columns);
    QString out;
    if (streamed) for (const QChar c : text) out += w.feed(QString(c));
    else out = w.feed(text);
    return out + w.flush();
}

// What the rows show: SGR dropped, cursor-forward as the blank cells it leaves.
QString shown(const QString &rendered) {
    static const QRegularExpression sgr(QStringLiteral("\x1b\\[[0-9;]*m"));
    static const QRegularExpression forward(QStringLiteral("\x1b\\[(\\d+)C"));
    QString text = QString(rendered).remove(sgr);
    for (auto m = forward.match(text); m.hasMatch(); m = forward.match(text))
        text.replace(m.capturedStart(), m.capturedLength(), QString(m.captured(1).toInt(), QLatin1Char(' ')));
    return text;
}

}  // namespace

class WordWrapTest : public QObject {
    Q_OBJECT
private slots:
    void breaksBetweenWords() {
        const QString text = QStringLiteral("the tree carries pre-existing uncommitted edits\n");
        QCOMPARE(shown(wrap(text, 20)), QStringLiteral("the tree carries \npre-existing \nuncommitted edits\n"));
        QCOMPARE(wrap(text, 20, true), wrap(text, 20));
        for (const QString &row : shown(wrap(text, 20)).split('\n')) QVERIFY(row.size() <= 20);
    }

    void fitsExactlyAndOff() {
        QCOMPARE(wrap(QStringLiteral("abcd efgh\n"), 9), QStringLiteral("abcd efgh\n"));
        // A space at the edge is dropped rather than opening the next row.
        QCOMPARE(wrap(QStringLiteral("abcd efgh ijk\n"), 9), QStringLiteral("abcd efgh\nijk\n"));
        QCOMPARE(wrap(QStringLiteral("abcd efgh ijk\n"), 0), QStringLiteral("abcd efgh ijk\n"));
    }

    void holdsOnlyTheWordInProgress() {
        WordWrap w;
        w.setColumns(40);
        QCOMPARE(w.feed(QStringLiteral("hello wor")), QStringLiteral("hello "));
        QVERIFY(w.holding());
        QCOMPARE(w.feed(QStringLiteral("ld ")), QStringLiteral("world "));
        QCOMPARE(w.feed(QStringLiteral("x")), QString());
        QCOMPARE(w.flush(), QStringLiteral("x"));
        QVERIFY(!w.holding());
    }

    void escapesTakeNoWidth() {
        const QString bold = QStringLiteral("\x1b[1m");
        const QString off = QStringLiteral("\x1b[0m");
        const QString text = QStringLiteral("aaaa ") + bold + QStringLiteral("bbbb") + off + QStringLiteral(" cc\n");
        QCOMPARE(shown(wrap(text, 9)), QStringLiteral("aaaa bbbb\ncc\n"));
        QCOMPARE(wrap(text, 9, true), wrap(text, 9));
        // An OSC 8 hyperlink around a word is zero-width too.
        const QString link = QStringLiteral("\x1b]8;;relay://x\x1b\\link\x1b]8;;\x1b\\");
        QCOMPARE(shown(wrap(QStringLiteral("aaaa ") + link + QStringLiteral(" b\n"), 9)).remove(QRegularExpression(QStringLiteral("\x1b\\]8;;[^\x1b]*\x1b\\\\"))),
                 QStringLiteral("aaaa link\nb\n"));
    }

    void bulletsHangUnderTheirText() {
        QCOMPARE(shown(wrap(QStringLiteral("• one two three four\n"), 12)), QStringLiteral("• one two \n  three four\n"));
        QCOMPARE(shown(wrap(QStringLiteral("  ◦ one two three\n"), 12)), QStringLiteral("  ◦ one two \n    three\n"));
        QCOMPARE(shown(wrap(QStringLiteral("12. one two three\n"), 12)), QStringLiteral("12. one two \n    three\n"));
        QCOMPARE(shown(wrap(QStringLiteral("    code and more\n"), 12)), QStringLiteral("    code and\n    more\n"));
        // The next line starts flush again.
        QCOMPARE(shown(wrap(QStringLiteral("• aa bb cc\nnext line here\n"), 8)), QStringLiteral("• aa bb \n  cc\nnext \nline \nhere\n"));
    }

    void wordsWiderThanTheLineAreLeftToTheTerminal() {
        QCOMPARE(wrap(QStringLiteral("ab abcdefghij c\n"), 6), QStringLiteral("ab \nabcdefghij c\n"));
        QCOMPARE(wrap(QStringLiteral("ab abcdefghij c d\n"), 6, true), QStringLiteral("ab \nabcdefghij c\nd\n"));
        QCOMPARE(wrap(QStringLiteral("abcdefghij c ef\n"), 6), QStringLiteral("abcdefghij c\nef\n"));
    }

    void wideCharactersCountTwo() {
        QCOMPARE(WordWrap::cellWidth(0x4e2d), 2);
        QCOMPARE(WordWrap::cellWidth(0x1f600), 2);
        QCOMPARE(WordWrap::cellWidth(0x301), 0);
        QCOMPARE(wrap(QStringLiteral("ab 中文字 x\n"), 9), QStringLiteral("ab 中文字\nx\n"));
        QCOMPARE(wrap(QStringLiteral("ab 😀😀 x\n"), 6, true), QStringLiteral("ab \n😀😀 x\n"));
    }

    void carriageReturnStartsTheRowOver() {
        QCOMPARE(wrap(QStringLiteral("aaaa\r\x1b[2Kbb cc dd\n"), 6), QStringLiteral("aaaa\r\x1b[2Kbb cc \ndd\n"));
    }

    void markdownRepliesWrapCleanly() {
        MarkdownAnsi md;
        WordWrap w;
        w.setColumns(30);
        const QString reply = QStringLiteral(
            "Also noted: the tree carries **pre-existing** uncommitted doc/tracker edits from an earlier session.\n"
            "- a bullet whose words run past the edge of the pane\n");
        QString out;
        for (const QChar c : reply) out += w.feed(md.feed(QString(c)));
        out += w.feed(md.finish()) + w.flush();
        const QStringList rows = shown(out).split('\n');
        for (const QString &row : rows) QVERIFY2(row.size() <= 30, qPrintable(row));
        QCOMPARE(rows.join(' ').simplified(), QStringLiteral(
            "Also noted: the tree carries pre-existing uncommitted doc/tracker edits from an earlier session. "
            "• a bullet whose words run past the edge of the pane"));
        QVERIFY(rows.contains(QStringLiteral("  past the edge of the pane")) || rows.contains(QStringLiteral("  the pane")));
    }
};

QTEST_GUILESS_MAIN(WordWrapTest)
#include "wordwrap_test.moc"
