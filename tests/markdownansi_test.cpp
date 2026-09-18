// SPDX-License-Identifier: GPL-3.0-or-later
// Agent replies rendered into the terminal: Markdown in, ANSI out, whatever the chunking.
#include "MarkdownAnsi.h"

#include <QRegularExpression>
#include <QTest>

using relay::MarkdownAnsi;

namespace {

QString render(const QString &markdown) {
    MarkdownAnsi md;
    // Sequenced deliberately: `feed(...) + finish()` leaves the order to the compiler, and the
    // two are not commutative (see MarkdownAnsi::renderInline).
    QString out = md.feed(markdown);
    out += md.finish();
    return out;
}

// The same text streamed one character at a time, as the slowest provider would send it.
QString renderStreamed(const QString &markdown) {
    MarkdownAnsi md;
    QString out;
    for (const QChar c : markdown) out += md.feed(QString(c));
    return out + md.finish();
}

QString plain(const QString &rendered) {
    static const QRegularExpression sgr(QStringLiteral("\x1b\\[[0-9;]*m"));
    return QString(rendered).remove(sgr);
}

}  // namespace

class MarkdownAnsiTest : public QObject {
    Q_OBJECT
private slots:
    void emphasisIsStyledAndMarkersDropped() {
        const QString out = render(QStringLiteral("a **bold** and *it* and `code` and ~~gone~~\n"));
        QCOMPARE(plain(out), QStringLiteral("a bold and it and code and gone\n"));
        QVERIFY(out.contains(QStringLiteral(";1m")));
        QVERIFY(out.contains(QStringLiteral(";3m")));
        QVERIFY(out.contains(QStringLiteral(";9m")));
        QVERIFY(out.contains(QStringLiteral("33m")));   // `code`, the palette's default
    }

    void literalsStayLiteral() {
        QCOMPARE(plain(render(QStringLiteral("snake_case_name and 2 * 3 * 4 and \\*stars\\*\n"))),
                 QStringLiteral("snake_case_name and 2 * 3 * 4 and *stars*\n"));
        QCOMPARE(plain(render(QStringLiteral("`a*b*c`\n"))), QStringLiteral("a*b*c\n"));
    }

    void blocks() {
        QCOMPARE(plain(render(QStringLiteral("## Title\n"))), QStringLiteral("Title\n"));
        QCOMPARE(plain(render(QStringLiteral("- one\n  - two\n1. first\n"))), QStringLiteral("• one\n  ◦ two\n1. first\n"));
        QCOMPARE(plain(render(QStringLiteral("- [ ] todo\n- [x] done\n"))), QStringLiteral("☐ todo\n☑ done\n"));
        QCOMPARE(plain(render(QStringLiteral("> quoted\n"))), QStringLiteral("▎ quoted\n"));
        QCOMPARE(plain(render(QStringLiteral("---\n"))), QString(40, QChar(0x2500)) + QLatin1Char('\n'));
        QCOMPARE(plain(render(QStringLiteral("**Bold** start\n"))), QStringLiteral("Bold start\n"));
        QCOMPARE(plain(render(QStringLiteral("#include <x>\n"))), QStringLiteral("#include <x>\n"));
    }

    void codeBlocksAreVerbatim() {
        const QString md = QStringLiteral("```cpp\nint *p = **q; // # not a heading\n- x\n```\nafter *it*\n");
        QCOMPARE(plain(render(md)), QStringLiteral("```cpp\nint *p = **q; // # not a heading\n- x\n```\nafter it\n"));
    }

    void links() {
        QCOMPARE(plain(render(QStringLiteral("see [docs](https://x.org/a) now\n"))), QStringLiteral("see docs (https://x.org/a) now\n"));
        QCOMPARE(plain(render(QStringLiteral("array[0] = 1\n"))), QStringLiteral("array[0] = 1\n"));
    }

    void tables() {
        const QString md = QStringLiteral("| Name | Size |\n|---|---:|\n| a | 1 |\n| **long** | 200 |\nafter\n");
        const QString out = plain(render(md));
        QCOMPARE(out, QStringLiteral("Name │ Size\n─────┼─────\n"
                                     "a    │    1\nlong │  200\nafter\n"));
    }

    void streamingMatchesWholeText() {
        const QString md = QStringLiteral(
            "# Plan\n\nWe **fix** the `render` path, see [card](https://x/y).\n\n"
            "1. Parse *each* delta\n2. Hold ***back*** markers\n- [ ] test\n> note\n\n"
            "```sh\n$ make -j\n```\n| a | b |\n|:-:|---|\n| 1 | 2 |\n\n---\ndone_here and \\_x\\_");
        QCOMPARE(plain(renderStreamed(md)), plain(render(md)));
        QCOMPARE(renderStreamed(md), render(md));
    }

    void finishFlushesHeldText() {
        MarkdownAnsi md;
        QCOMPARE(plain(md.feed(QStringLiteral("trailing *"))), QStringLiteral("trailing "));
        QCOMPARE(plain(md.finish()), QStringLiteral("*"));
        QVERIFY(!md.holding());
        QCOMPARE(plain(md.feed(QStringLiteral("-"))), QString());
        QCOMPARE(plain(md.finish()), QStringLiteral("• "));
    }

    // A caller can still replace every colour, and the renderer must use every one it is given.
    void everyColourComesFromThePalette() {
        MarkdownAnsi::Palette p;
        p.base = QStringLiteral("38;2;1;1;1");
        p.heading = QStringLiteral("1;38;2;22;22;22");
        p.marker = QStringLiteral("38;2;3;3;3");
        p.quote = QStringLiteral("3;38;2;4;4;4");
        p.inlineCode = QStringLiteral("38;2;5;5;5");
        p.codeBlock = QStringLiteral("38;2;6;6;6");
        p.dim = QStringLiteral("38;2;7;7;7");
        p.link = QStringLiteral("4;38;2;8;8;8");
        MarkdownAnsi md(p);
        // A level-1 heading underlines, so the bare heading colour is asserted from a level 2.
        QString out = md.feed(QStringLiteral("## Title\n\nplain `code` and [label](https://x.invalid)\n"
                                             "\n- bullet\n\n> quoted\n\n```\nblock\n```\n"));
        out += md.finish();
        for (const QString &sgr : {p.base, p.heading, p.marker, p.quote, p.inlineCode, p.codeBlock,
                                   p.dim, p.link})
            QVERIFY2(out.contains(sgr + QStringLiteral("m")), qPrintable(sgr));
        // None of the defaults survive a palette that replaced them.
        for (const QString &dead : {QStringLiteral("39m"), QStringLiteral("35m"), QStringLiteral("33m"),
                                    QStringLiteral("36m"), QStringLiteral("34m")})
            QVERIFY2(!out.contains(dead), qPrintable(dead));
    }

    // The regression this renderer was given a palette for (owner report, 2026-09-18: "some of the
    // text in beige mode is too light and not readable", and the same in reverse on the dark
    // theme). An absolute colour is burnt into the scrollback, so prose written under one theme
    // stays that colour when the theme changes. The defaults therefore name the terminal's own
    // colours — 39, the faint attribute, and the ANSI indices — which the engine resolves from the
    // active theme every time it paints, scrollback included.
    void theDefaultPaletteNamesNoColourOfItsOwn() {
        const QString out = render(QStringLiteral("# Title\n\ntext with `code`, [a link](https://x.invalid)\n"
                                                 "\n- bullet\n\n> quoted\n\n```\nblock\n```\n"));
        QVERIFY2(!out.contains(QStringLiteral("38;2;")), "truecolor in the default palette");
        QVERIFY2(!out.contains(QStringLiteral("38;5;")), "indexed 256-colour in the default palette");
        QVERIFY(out.contains(QStringLiteral("39m")));   // prose is the terminal's own foreground
    }

    // A table cell is rendered by a second instance: it has to inherit the palette, not just the
    // base colour.
    void aTableCellKeepsThePalette() {
        MarkdownAnsi::Palette p;
        p.inlineCode = QStringLiteral("38;2;9;9;9");
        MarkdownAnsi md(p);
        QString out = md.feed(QStringLiteral("| a | b |\n|---|---|\n| `x` | y |\n"));
        out += md.finish();
        QVERIFY(out.contains(QStringLiteral("38;2;9;9;9m")));
    }
};

QTEST_GUILESS_MAIN(MarkdownAnsiTest)
#include "markdownansi_test.moc"
