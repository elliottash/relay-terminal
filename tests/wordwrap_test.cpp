// SPDX-License-Identifier: AGPL-3.0-or-later
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

// The rows the shared rule (relay::wrap::rows) lays out for one logical line of
// visible text: one cell per character with its grid width, word boundaries at
// the first non-space after spaces — exactly what FoldLayer::layout() builds
// (#R2WQ). Continuation rows carry their hanging indent as leading spaces, the
// way the terminal shows a cursor-forward indent.
QStringList sharedRows(const QString &text, int columns, int firstIndent, int hangIndent)
{
    QVector<int> widths;
    QVector<char> startsWord, spaces;
    uint high = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        uint cp = c.unicode();
        if (c.isHighSurrogate()) { high = cp; continue; }
        if (c.isLowSurrogate() && high) { cp = QChar::surrogateToUcs4(char16_t(high), c.unicode()); high = 0; }
        widths << WordWrap::cellWidth(cp);
        spaces << char(c == QLatin1Char(' ') ? 1 : 0);
    }
    startsWord << char(1);
    for (int i = 1; i < widths.size(); ++i)
        startsWord << char(!spaces.at(i) && spaces.at(i - 1) ? 1 : 0);
    const QVector<relay::wrap::Row> rows =
        relay::wrap::rows(widths, startsWord, spaces, columns, firstIndent, hangIndent);
    QStringList out;
    for (const relay::wrap::Row &r : rows) {
        QString row(r.indent, QLatin1Char(' '));
        for (int i = r.first; i < r.first + r.count; ++i)
            row += text.at(i);
        out << row;
    }
    return out;
}

// The visual rows the terminal shows for the wrapper's bytes: WordWrap writes
// rows of its own and leaves a word wider than the line to the terminal, which
// soft-wraps it at the edge. That autowrap, applied to shown(), is what the
// shared rule has to reproduce.
QStringList terminalRows(const QString &text, int columns)
{
    QStringList out;
    QStringList byteRows = shown(text).split(QLatin1Char('\n'));
    if (!byteRows.isEmpty() && byteRows.last().isEmpty())
        byteRows.removeLast();   // the artifact of the input ending in '\n'
    for (const QString &row : byteRows) {
        if (row.isNull())
            continue;
        QString line;
        int used = 0;
        uint high = 0;
        for (int i = 0; i < row.size(); ++i) {
            const QChar c = row.at(i);
            uint cp = c.unicode();
            if (c.isHighSurrogate()) { high = cp; continue; }
            if (c.isLowSurrogate() && high) { cp = QChar::surrogateToUcs4(char16_t(high), c.unicode()); high = 0; }
            const int w = WordWrap::cellWidth(cp);
            if (used > 0 && used + w > columns) {
                out << line;
                line.clear();
                used = 0;
            }
            line += c;
            used += w;
        }
        out << line;
    }
    return out;
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
    // ---- the shared whole-line rule (#R2WQ): one rule, used by the streaming
    // wrapper and by the rows the view lays out itself. Its rows must be the
    // rows the terminal shows for WordWrap's output — that is what makes a
    // re-wrapped block agree with the bytes the pane printed.
    void sharedRuleMatchesTheStreamedWrapper() {
        const QStringList texts = {
            QStringLiteral("the tree carries pre-existing uncommitted edits"),
            QStringLiteral("• one two three four"),
            QStringLiteral("  ◦ one two three"),
            QStringLiteral("12. one two three"),
            QStringLiteral("    code and more"),
            QStringLiteral("ab abcdefghij c"),
            QStringLiteral("abcd efgh ijk"),
            QStringLiteral("one  two   three four"),
            QStringLiteral("ab 中文字 x y"),
            QStringLiteral("short"),
            QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa b"),
        };
        for (const QString &text : texts) {
            for (int columns : {6, 8, 9, 12, 20, 40}) {
                const QStringList expected = terminalRows(wrap(text + QStringLiteral("\n"), columns), columns);
                const int hang = relay::wrap::hangingIndent(text, columns);
                const QStringList laid = sharedRows(text, columns, 0, hang);
                QCOMPARE(laid.join(QLatin1Char('\n')), expected.join(QLatin1Char('\n')));
                for (const QString &row : laid)
                    QVERIFY2(row.size() <= columns, qPrintable(row));
            }
        }
    }

    void sharedRuleIndentsAndMarkers() {
        using relay::wrap::isMarker;
        QVERIFY(isMarker(QStringLiteral("•")));
        QVERIFY(isMarker(QStringLiteral("-")));
        QVERIFY(isMarker(QStringLiteral(">")));
        QVERIFY(isMarker(QStringLiteral("12.")));
        QVERIFY(isMarker(QStringLiteral("2)")));
        QVERIFY(!isMarker(QStringLiteral("word")));
        QVERIFY(!isMarker(QStringLiteral("1234.")));
        QCOMPARE(relay::wrap::hangingIndent(QStringLiteral("• one two"), 12), 2);
        QCOMPARE(relay::wrap::hangingIndent(QStringLiteral("  ◦ one two"), 12), 4);
        QCOMPARE(relay::wrap::hangingIndent(QStringLiteral("12. one two"), 12), 4);
        QCOMPARE(relay::wrap::hangingIndent(QStringLiteral("    code"), 12), 4);
        // Past half the row there is no indent at all.
        QCOMPARE(relay::wrap::hangingIndent(QStringLiteral("       code"), 12), 0);
        // A one-column marker plus the space after it.
        QCOMPARE(relay::wrap::hangingIndent(QStringLiteral("▎ one two"), 12), 2);
    }

    void sharedRuleBreaksAtWordBoundariesOnly() {
        // No row ends inside a word, and no row is left stranded short.
        const QStringList rows =
            sharedRows(QStringLiteral("commit RELAY_ENGINE_WITH_GHOSTTY describes the core"), 20, 0, 0);
        for (const QString &row : rows) {
            QVERIFY2(row.size() <= 20, qPrintable(row));
            QVERIFY2(!row.isEmpty(), "no empty rows");
        }
        QCOMPARE(rows.first(), QStringLiteral("commit "));   // a space that fits stays on the row
        // A word wider than the row is broken at the edge, as the terminal does.
        const QStringList overlong =
            sharedRows(QStringLiteral("ab abcdefghij c"), 6, 0, 0);
        QCOMPARE(overlong, QStringList({QStringLiteral("ab "), QStringLiteral("abcdef"), QStringLiteral("ghij c")}));
        // A space that would cross the edge is dropped, not carried over.
        QCOMPARE(sharedRows(QStringLiteral("abcd efgh ijk"), 9, 0, 0),
                 QStringList({QStringLiteral("abcd efgh"), QStringLiteral("ijk")}));
        // Off: the whole line is one row.
        QCOMPARE(sharedRows(QStringLiteral("a b c"), 0, 0, 0), QStringList({QStringLiteral("a b c")}));
        // An empty line is one empty row at the first indent.
        QCOMPARE(relay::wrap::rows({}, {}, {}, 20, 3, 3).size(), 1);
        QCOMPARE(relay::wrap::rows({}, {}, {}, 20, 3, 3).first().indent, 3);
    }
};

QTEST_GUILESS_MAIN(WordWrapTest)
#include "wordwrap_test.moc"
