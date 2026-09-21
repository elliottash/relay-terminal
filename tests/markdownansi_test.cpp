// SPDX-License-Identifier: AGPL-3.0-or-later
// Agent replies rendered into the terminal: Markdown in, ANSI out, whatever the chunking.
#include "MarkdownAnsi.h"

#include "LabelLinks.h"

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

// The same, plus the OSC 8 runs a link's label is wrapped in when an anchor is set (card #MDKN):
// what the terminal actually shows.
QString plainer(const QString &rendered) {
    static const QRegularExpression osc(QStringLiteral("\x1b\\][^\x07\x1b]*(\x07|\x1b\\\\)"));
    return plain(QString(rendered).remove(osc));
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
        QVERIFY(out.contains(QStringLiteral("1;97m"))); // `code`: bold, no hue (the palette's default since 2026-09-19)
    }

    // Card #CVHT: the three labelled bolds the system prompt teaches are coloured by their first
    // word — **Done:** green, **Need:** amber, **Problem:** red — while any other bold stays plain.
    // Card #4E13 put them in the pane states' shared language: done is the Done glyph's own green,
    // need the amber of every needs-you mark.
    void labelledBoldsAreColoured() {
        const QString done = render(QStringLiteral("**Done:** the fix is in\n"));
        QCOMPARE(plain(done), QStringLiteral("Done: the fix is in\n"));
        QVERIFY2(done.contains(QStringLiteral(";1;32m")), qPrintable(done));
        QVERIFY(done.indexOf(QStringLiteral(";1;32m")) < done.indexOf(QStringLiteral("Done")));

        const QString need = render(QStringLiteral("text, then **Need:** your call\n"));
        QCOMPARE(plain(need), QStringLiteral("text, then Need: your call\n"));
        QVERIFY2(need.contains(QStringLiteral(";1;33m")), qPrintable(need));

        const QString problem = render(QStringLiteral("**Problem:** the build broke\n"));
        QCOMPARE(plain(problem), QStringLiteral("Problem: the build broke\n"));
        QVERIFY2(problem.contains(QStringLiteral(";1;31m")), qPrintable(problem));

        // Matched case-insensitively, past the colon, and the colour covers the whole run.
        QVERIFY(render(QStringLiteral("**FIXED** the parser\n")).contains(QStringLiteral(";1;32m")));
        QVERIFY(render(QStringLiteral("**Done: fixed, and the run goes on**\n"))
                    .contains(QStringLiteral(";1;32m")));

        // An unlabelled bold run is plain bold, exactly as before, with no role colour.
        const QString plainBold = render(QStringLiteral("a **Bold** label\n"));
        QCOMPARE(plain(plainBold), QStringLiteral("a Bold label\n"));
        QVERIFY(plainBold.contains(QStringLiteral(";1m")));
        QVERIFY(!plainBold.contains(QStringLiteral(";1;32m")));
        QVERIFY(!plainBold.contains(QStringLiteral(";1;33m")));
        QVERIFY(!plainBold.contains(QStringLiteral(";1;31m")));
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

    // A row that is not a table used to kill the process. renderTable() decided "not a table after
    // all" and sent the row back through renderInline(); the inner renderer saw a line starting
    // with `|`, collected it as a table of its own, and called renderTable() again when it
    // finished — for as long as the stack held (Relay died of this twice on 2026-09-19). One row
    // with no separator line under it is all it takes, and an agent writes one by accident.
    void aTableRowThatIsNotATableIsJustALine() {
        QCOMPARE(plain(render(QStringLiteral("| one row and no separator |\n"))),
                 QStringLiteral("| one row and no separator |\n"));
        // Two rows, still no separator: both are lines, in order, and neither recurses.
        QCOMPARE(plain(render(QStringLiteral("| a | b |\n| c | d |\n"))),
                 QStringLiteral("| a | b |\n| c | d |\n"));
        // The same text one character at a time, which is how it actually arrives.
        QCOMPARE(plain(renderStreamed(QStringLiteral("| one row |\n"))), QStringLiteral("| one row |\n"));
        // A pipe inside a cell of a real table is a character in that cell, not a nested table.
        const QString nested = plain(render(QStringLiteral("| a | b |\n|---|---|\n| x | \\| y |\n")));
        QVERIFY2(nested.contains(QStringLiteral("| y")), qPrintable(nested));
    }

    void streamingMatchesWholeText() {
        const QString md = QStringLiteral(
            "# Plan\n\nWe **fix** the `render` path, see [card](https://x/y).\n\n"
            "**Done:** shipped; **Problem:** none; **Need:** nothing.\n\n"
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
        p.done = QStringLiteral("38;2;10;10;10");
        p.need = QStringLiteral("38;2;11;11;11");
        p.problem = QStringLiteral("38;2;12;12;12");
        MarkdownAnsi md(p);
        // A level-1 heading underlines, so the bare heading colour is asserted from a level 2.
        QString out = md.feed(QStringLiteral("## Title\n\nplain `code` and [label](https://x.invalid)\n"
                                             "\n- bullet\n\n> quoted\n\n```\nblock\n```\n"
                                             "\n**Done:** a **Need:** b **Problem:** c\n"));
        out += md.finish();
        for (const QString &sgr : {p.base, p.heading, p.marker, p.quote, p.inlineCode, p.codeBlock,
                                   p.dim, p.link, p.done, p.need, p.problem})
            QVERIFY2(out.contains(sgr + QStringLiteral("m")), qPrintable(sgr));
        // None of the defaults survive a palette that replaced them.
        for (const QString &dead : {QStringLiteral("97m"), QStringLiteral("39m"), QStringLiteral("35m"),
                                    QStringLiteral("33m"), QStringLiteral("36m"), QStringLiteral("34m"),
                                    QStringLiteral("32m"), QStringLiteral("31m")})
            QVERIFY2(!out.contains(dead), qPrintable(dead));
    }

    // The regression this renderer was given a palette for (owner report, 2026-09-18: "some of the
    // text in beige mode is too light and not readable", and the same in reverse on the dark
    // theme). An absolute colour is burnt into the scrollback, so prose written under one theme
    // stays that colour when the theme changes. The defaults therefore name the terminal's own
    // colours — 97, the faint attribute, and the ANSI indices — which the engine resolves from the
    // active theme every time it paints, scrollback included.
    void theDefaultPaletteNamesNoColourOfItsOwn() {
        const QString out = render(QStringLiteral("# Title\n\ntext with `code`, [a link](https://x.invalid)\n"
                                                 "\n- bullet\n\n> quoted\n\n```\nblock\n```\n"));
        QVERIFY2(!out.contains(QStringLiteral("38;2;")), "truecolor in the default palette");
        QVERIFY2(!out.contains(QStringLiteral("38;5;")), "indexed 256-colour in the default palette");
        QVERIFY(out.contains(QStringLiteral("97m")));   // prose is the terminal's own bright foreground
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

    // ---- a link's label carries its target (card #MDKN) -------------------------------------
    //
    // The label is painted in the link ink, so a person clicks it; until 2026-09-21 only the
    // `(target)` printed beside it was clickable, because that is text and the link scanner scans
    // text. With an anchor set the label's cells carry an OSC 8 run of their own — the anchor with
    // the target as its fragment — and the anchor is re-opened after it, so the block the label
    // sits in carries on and nothing else on the row points anywhere.

    void withNoAnchorTheBytesAreExactlyWhatTheyWere() {
        const QString md = QStringLiteral("see [docs](https://x.org/a) now\n");
        MarkdownAnsi a;
        MarkdownAnsi b;
        b.setLinkAnchor(QString());
        QCOMPARE(b.feed(md) + b.finish(), a.feed(md) + a.finish());
        QVERIFY(!(MarkdownAnsi().feed(md)).contains(QChar(0x1b) + QStringLiteral("]8")));
    }

    void anAnchoredLabelIsItsOwnOsc8Run() {
        MarkdownAnsi md;
        md.setLinkAnchor(QStringLiteral("relay://prose/p4/7"));
        const QString out = md.feed(QStringLiteral("see [docs](option:general/theme) now\n")) + md.finish();
        // The label's run, the anchor re-opened right after it, and the target still printed.
        QVERIFY(out.contains(QStringLiteral("\x1b]8;;relay://prose/p4/7#l=option%3Ageneral%2Ftheme\x1b\\")));
        QVERIFY(out.contains(QStringLiteral("\x1b]8;;relay://prose/p4/7\x1b\\")));
        QCOMPARE(plainer(out), QStringLiteral("see docs (option:general/theme) now\n"));
        // The run closes before the printed target: that stays plain text, which is what scans as
        // a link of its own and what survives a restore from saved bytes.
        const int labelRun = out.indexOf(QStringLiteral("#l="));
        const int reopen = out.indexOf(QStringLiteral("\x1b]8;;relay://prose/p4/7\x1b\\"), labelRun);
        QVERIFY(reopen > labelRun);
        QVERIFY(out.indexOf(QStringLiteral("(option:general/theme)")) > reopen);
    }

    void everyLinkKindRoundTripsThroughTheFragment() {
        const QStringList targets{QStringLiteral("option:models/provider/glm-coding"),
                                  QStringLiteral("session:0f3a91cc"),
                                  QStringLiteral("card:K7Q2"),
                                  QStringLiteral("#K7Q2"),
                                  QStringLiteral("src/Pane.h:42:7"),
                                  QStringLiteral("https://x.org/a?b=1&c=2")};
        for (const QString &target : targets) {
            MarkdownAnsi md;
            md.setLinkAnchor(QStringLiteral("relay://prose/p1/1"));
            const QString out = md.feed(QStringLiteral("[L](") + target + QStringLiteral(")\n")) + md.finish();
            const QString uri = out.mid(out.indexOf(QStringLiteral("\x1b]8;;")) + 5);
            QCOMPARE(relay::labellink::targetOf(uri.left(uri.indexOf(QChar(0x1b)))), target);
        }
    }

    // Two links in one paragraph, and the same text one character at a time: the renderer holds a
    // link back until its `)` arrives, so a link never straddles two chunks and each gets its own
    // run.
    void twoLinksInOneParagraphEachGetTheirOwnRun() {
        const QString md = QStringLiteral("[one](option:a/b) and [two](session:9f) done\n");
        MarkdownAnsi whole;
        whole.setLinkAnchor(QStringLiteral("relay://prose/p1/2"));
        const QString out = whole.feed(md) + whole.finish();
        QCOMPARE(out.count(QStringLiteral("#l=")), 2);
        QCOMPARE(out.count(QStringLiteral("\x1b]8;;relay://prose/p1/2\x1b\\")), 2);
        MarkdownAnsi streamed;
        streamed.setLinkAnchor(QStringLiteral("relay://prose/p1/2"));
        QString piece;
        for (const QChar c : md) piece += streamed.feed(QString(c));
        QCOMPARE(piece + streamed.finish(), out);
    }

    // A label with spaces, punctuation and a card reference in it, and a label inside a list item,
    // a quote and a table cell: the run is around the label's own characters, whatever they are.
    void aLabelKeepsItsTextWhateverIsInIt() {
        MarkdownAnsi md;
        md.setLinkAnchor(QStringLiteral("relay://prose/p2/3"));
        QString out = md.feed(QStringLiteral("- [Open #K7Q2 (the card), now](card:K7Q2)\n"
                                             "> quoted [link](option:a/b)\n"));
        out += md.finish();
        QVERIFY(plainer(out).contains(QStringLiteral("Open #K7Q2 (the card), now (card:K7Q2)")));
        QCOMPARE(out.count(QStringLiteral("#l=")), 2);
    }

    // A table cell's width is measured with visibleWidth(), which used to walk an escape to its
    // first letter: with an OSC in the cell that stopped at the `r` of `relay://` and counted the
    // rest of the URI as text, so the column was padded to the width of the URI.
    void aLinkInATableCellDoesNotWidenTheColumn() {
        const QString md = QStringLiteral("| Name | Size |\n|---|---|\n| [a](option:x/y) | 1 |\n");
        MarkdownAnsi plainMd;
        MarkdownAnsi anchored;
        anchored.setLinkAnchor(QStringLiteral("relay://prose/p3/4"));
        QCOMPARE(plainer(anchored.feed(md) + anchored.finish()),
                 plainer(plainMd.feed(md) + plainMd.finish()));
        QCOMPARE(MarkdownAnsi::visibleWidth(QStringLiteral("\x1b]8;;relay://prose/p/1\x1b\\x")), 1);
        QCOMPARE(MarkdownAnsi::visibleWidth(QStringLiteral("\x1b]8;;u\ay")), 1);
    }
};

QTEST_GUILESS_MAIN(MarkdownAnsiTest)
#include "markdownansi_test.moc"
