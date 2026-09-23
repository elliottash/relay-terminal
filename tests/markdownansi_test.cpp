// SPDX-License-Identifier: AGPL-3.0-or-later
// Agent replies rendered into the terminal: Markdown in, ANSI out, whatever the chunking.
#include "MarkdownAnsi.h"

#include "LabelLinks.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

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

// Card #1MGS: a renderer with inline images on, run whole or a character at a time.
QString renderImages(const QString &markdown, const QString &base = QString(), bool streamed = false) {
    MarkdownAnsi md;
    md.setInlineImages(true);
    md.setImageBaseDir(base);
    QString out;
    if (streamed) for (const QChar c : markdown) out += md.feed(QString(c));
    else out = md.feed(markdown);
    out += md.finish();
    return out;
}

QString writePng(const QString &path, int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    return image.save(path, "PNG") ? path : QString();
}

// The kitty escape for a PNG at `path` placed as `cols` x `rows` cells.
QString kitty(const QString &path, int cols, int rows) {
    return QStringLiteral("\x1b_Ga=T,t=f,f=100,q=2,c=%1,r=%2;").arg(cols).arg(rows)
           + QString::fromLatin1(path.toUtf8().toBase64()) + QStringLiteral("\x1b\\");
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

    void inlineTableKeepsTextAndAddsSortableRow() {
        const QString md = QStringLiteral("| name | value |\n|---|---:|\n| small | 2 |\n| large | 10 |\n");
        const QString out = renderImages(md);
        QVERIFY(out.contains(QStringLiteral("small")));
        QVERIFY(out.contains(QStringLiteral("large")));
        QVERIFY(out.contains(MarkdownAnsi::kMediaEscapeStart));
        QCOMPARE(out.count(MarkdownAnsi::kMediaEscapeStart), 1);
        QCOMPARE(renderImages(md, QString(), true), out);
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

    // ----- inline images (card #1MGS) --------------------------------------------------------

    // A standalone image of a local file: the alt text dim on a line of its own, then the kitty
    // escape — base64 of the absolute path, the size in cells from the file's pixels at 8 x 16 a
    // cell — at the start of the next row.
    void anAbsolutePathImageIsAKittyEscape() {
        QTemporaryDir dir;
        const QString png = writePng(dir.filePath(QStringLiteral("shot.png")), 160, 64);
        QVERIFY(!png.isEmpty());
        const QString out = renderImages(QStringLiteral("![the chart](") + png + QStringLiteral(")\n"));
        QCOMPARE(out, QStringLiteral("\x1b[0;97m\x1b[0;2;97mthe chart\x1b[0m\n") + kitty(png, 20, 4)
                          + QStringLiteral("\x1b[0m\n"));
        QCOMPARE(MarkdownAnsi::visibleWidth(kitty(png, 20, 4)), 0);
        // No alt text: the file's name is the line above the picture.
        QVERIFY(plain(renderImages(QStringLiteral("![](") + png + QStringLiteral(")\n")))
                    .startsWith(QStringLiteral("shot.png\n\x1b_G")));
        // Off — the default, and every surface but the pane — it is the `!` and a link it always was.
        QCOMPARE(plain(render(QStringLiteral("![the chart](") + png + QStringLiteral(")\n"))),
                 QStringLiteral("!the chart (") + png + QStringLiteral(")\n"));
    }

    void aRelativePathResolvesAgainstTheBaseDir() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("out")));
        const QString png = writePng(dir.filePath(QStringLiteral("out/plot.png")), 80, 32);
        QVERIFY(!png.isEmpty());
        QVERIFY(renderImages(QStringLiteral("![p](out/plot.png)\n"), dir.path()).contains(kitty(png, 10, 2)));
        QVERIFY(renderImages(QStringLiteral("![p](./out/plot.png)\n"), dir.path()).contains(kitty(png, 10, 2)));
        // With no base dir a relative path names nothing.
        QVERIFY(!renderImages(QStringLiteral("![p](out/plot.png)\n")).contains(QStringLiteral("\x1b_G")));
    }

    void fileUrlsAndTheHomeDirectoryResolve() {
        QTemporaryDir dir;
        const QString png = writePng(dir.filePath(QStringLiteral("with space.png")), 16, 16);
        QVERIFY(!png.isEmpty());
        const QString url = QUrl::fromLocalFile(png).toString(QUrl::FullyEncoded);
        QVERIFY(url.contains(QStringLiteral("%20")));
        QVERIFY(renderImages(QStringLiteral("![a](") + url + QStringLiteral(")\n")).contains(kitty(png, 2, 1)));
        QVERIFY(renderImages(QStringLiteral("![a](<") + png + QStringLiteral("> \"a title\")\n")).contains(kitty(png, 2, 1)));

        const QByteArray home = qgetenv("HOME");
        qputenv("HOME", dir.path().toUtf8());
        const QString out = renderImages(QStringLiteral("![a](~/with%20space.png)\n"));
        qputenv("HOME", home);
        QVERIFY2(out.contains(kitty(png, 2, 1)), qPrintable(out));
    }

    // Deltas arrive in any pieces: the escape is emitted whole once the `)` is in, and the bytes
    // are the same as the whole reply rendered at once.
    void aStreamedImageIsTheSameBytes() {
        QTemporaryDir dir;
        const QString png = writePng(dir.filePath(QStringLiteral("a.png")), 1600, 400);
        QVERIFY(!png.isEmpty());
        const QString md = QStringLiteral("Here it is:\n\n![**chart**](a.png)\n- see ![a](a.png) and **Done:** that\n"
                                          "Wow! [x](y) ![gone](nope.png) !\n");
        const QString whole = renderImages(md, dir.path());
        QCOMPARE(renderImages(md, dir.path(), true), whole);
        QCOMPARE(whole.count(QStringLiteral("\x1b_G")), 2);
        // A wide picture is fitted into 80 columns, keeping its aspect ratio.
        QVERIFY(whole.contains(kitty(png, 80, 10)));
        MarkdownAnsi md2;
        md2.setInlineImages(true);
        md2.setImageBaseDir(dir.path());
        QString streamed;
        const QString line = QStringLiteral("![a](a.png)\n");
        for (int n = 0; n < line.size(); ++n) {
            streamed += md2.feed(line.mid(n, 1));
            if (n < line.indexOf(QLatin1Char(')'))) QVERIFY(!streamed.contains(QStringLiteral("\x1b_G")));
        }
        QVERIFY(streamed.contains(kitty(png, 80, 10)));
        // A picture mid-line ends the line before it, and what follows it starts a row under it.
        const QString mid = renderImages(QStringLiteral("see ![a](a.png) there\n"), dir.path());
        QCOMPARE(plain(mid), QStringLiteral("see \na\n") + kitty(png, 80, 10) + QStringLiteral("\nthere\n"));
        // A narrower pane is a narrower picture.
        MarkdownAnsi narrow;
        narrow.setInlineImages(true);
        narrow.setImageBaseDir(dir.path());
        narrow.setImageColumns(40);
        QVERIFY(QString(narrow.feed(line) + narrow.finish()).contains(kitty(png, 40, 5)));
    }

    // A web image is never fetched: it is a link with its alt text as the label.
    void anHttpImageIsALink() {
        const QString out = renderImages(QStringLiteral("![logo](https://example.com/logo.png)\n"));
        QVERIFY(!out.contains(QStringLiteral("\x1b_G")));
        QCOMPARE(out, render(QStringLiteral("[logo](https://example.com/logo.png)\n")));
    }

    void aMissingFileIsItsAltTextAndPath() {
        const QString out = renderImages(QStringLiteral("![the plot](/nonexistent/relay/plot.png)\n"));
        QVERIFY(!out.contains(QStringLiteral("\x1b_G")));
        QCOMPARE(plain(out), QStringLiteral("the plot (/nonexistent/relay/plot.png)\n"));
        QVERIFY(out.contains(QStringLiteral("\x1b[2;97m (/nonexistent/relay/plot.png)")));
        // A file that is there but no picture is a link to it.
        QTemporaryDir dir;
        QFile text(dir.filePath(QStringLiteral("notes.txt")));
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("not an image");
        text.close();
        QCOMPARE(renderImages(QStringLiteral("![n](notes.txt)\n"), dir.path()),
                 render(QStringLiteral("[n](notes.txt)\n")));
    }

    void imageSyntaxInCodeIsUnchanged() {
        QTemporaryDir dir;
        QVERIFY(!writePng(dir.filePath(QStringLiteral("a.png")), 16, 16).isEmpty());
        const QString md = QStringLiteral("```\n![a](a.png)\n```\nand `![a](a.png)` inline\n    ![a](a.png)\n");
        const QString out = renderImages(md, dir.path());
        QVERIFY(out.contains(QStringLiteral("\x1b_G")));   // only the indented line (not a code block here)
        QCOMPARE(out.count(QStringLiteral("\x1b_G")), 1);
        const QString fenced = QStringLiteral("```\n![a](a.png)\n```\nand `![a](a.png)` inline\n");
        QCOMPARE(renderImages(fenced, dir.path()), render(fenced));
        QCOMPARE(renderImages(fenced, dir.path(), true), render(fenced));
        // A table cell is inline content: an image there stays text.
        const QString table = QStringLiteral("| a |\n|---|\n| ![a](a.png) |\n");
        const QString renderedTable = renderImages(table, dir.path());
        QVERIFY(renderedTable.startsWith(render(table)));  // same readable cells, then the sortable row
        QVERIFY(renderedTable.contains(MarkdownAnsi::kMediaEscapeStart));
    }

    void localAudioLinkBecomesOneMediaRow()
    {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("clip.wav"));
        QFile file(wav);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("RIFFtestWAVE");
        file.close();
        const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", dir.path().toUtf8());
        struct RestoreCache {
            QByteArray old;
            ~RestoreCache() { qputenv("XDG_CACHE_HOME", old); }
        } restore{oldCache};
        const QString text = QStringLiteral("[Play clip](clip.wav)\n");
        const QString whole = renderImages(text, dir.path());
        QCOMPARE(renderImages(text, dir.path(), true), whole);
        QVERIFY(whole.contains(MarkdownAnsi::kMediaEscapeStart));
        QCOMPARE(whole.count(MarkdownAnsi::kMediaEscapeStart), 1);
        const int at = whole.indexOf(MarkdownAnsi::kMediaEscapeStart);
        const int end = whole.indexOf(QStringLiteral("\x1b\\"), at);
        QVERIFY(end > at);
        const QString uri = whole.mid(at + 5, end - at - 5);
        const QString encoded = uri.section(QLatin1Char('/'), 3);
        const QString manifest = QUrl::fromPercentEncoding(encoded.toLatin1());
        QFile saved(manifest);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object().value(QStringLiteral("path")).toString(), wav);
        QVERIFY(!renderImages(QStringLiteral("[Play](https://example.com/clip.wav)\n"))
                     .contains(MarkdownAnsi::kMediaEscapeStart));
    }

    void imageCellsFitAndKeepTheAspect() {
        const QSize cell(8, 16);
        QCOMPARE(MarkdownAnsi::imageCells(QSize(160, 64), cell, 80, 20), QSize(20, 4));
        QCOMPARE(MarkdownAnsi::imageCells(QSize(1920, 1080), cell, 80, 20), QSize(72, 20));
        QCOMPARE(MarkdownAnsi::imageCells(QSize(1920, 1080), cell, 80, MarkdownAnsi::kThumbnailRows), QSize(22, 6));
        QCOMPARE(MarkdownAnsi::imageCells(QSize(1, 1), cell, 80, 20), QSize(1, 1));
        QCOMPARE(MarkdownAnsi::imageCells(QSize(160, 64), QSize(), 80, 20), QSize(20, 4));   // the 1:2 default
    }
};

QTEST_GUILESS_MAIN(MarkdownAnsiTest)
#include "markdownansi_test.moc"
