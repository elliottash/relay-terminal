// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

namespace relay {

// Streaming Markdown -> ANSI for agent replies printed into the terminal. Deltas arrive in
// arbitrary chunks, so the renderer holds back only what it cannot decide yet (the marker at the
// start of a line, a trailing `*` run, an unfinished `[link](url)`, a table until its last row)
// and streams everything else as it comes.
//
// Input must already be sanitized (no control characters): the output's escape sequences are the
// renderer's own. Lines end in '\n'; the caller turns that into "\r\n" for the terminal.
class MarkdownAnsi {
public:
    // Every colour the renderer can emit, as SGR parameter lists (attributes included).
    //
    // The defaults name the *terminal's own* colours — the bright foreground (97), the faint
    // attribute, and the ANSI indices — rather than absolute RGB, and that is the point. An
    // absolute colour is burnt into the scrollback: prose written under one theme stays that
    // colour when the theme changes, so a switch to IBM Beige left near-white paragraphs on warm
    // paper and a switch back left dark ones on charcoal (owner report, 2026-09-18). Indexed
    // colours are resolved by the engine at paint time from the active theme, so the whole
    // transcript, scrollback included, follows the theme for free — and it inherits the contrast
    // every theme's palette is measured for (docs/THEMES.md; tests/theme_test.cpp asserts AA for
    // ANSI 1-7 and 9-15 on the grid, and the light themes invert the ramp so 15 stays the readable
    // end). Prose takes the bright half (97, ANSI 15) rather than the default foreground (39):
    // agent prose is the thing being read, and in every shipped theme that index is the higher-
    // contrast of the pair.
    //
    // An app that wants its own colours can still set them; nothing here requires the defaults.
    struct Palette {
        QString base = QStringLiteral("97");            // the terminal's own bright foreground
        QString heading = QStringLiteral("1;35");       // magenta, the agent's end of the palette
        QString marker = QStringLiteral("35");
        QString quote = QStringLiteral("3;2;97");       // italic, faint: quieter by proportion
        // Bold, no hue (was amber, ANSI 33, until 2026-09-19). Two reasons: amber is the "waiting
        // on you" colour and had a second job here; and a filename in backticks — the commonest
        // openable thing in a reply — has to be able to take the link blue at rest, which the
        // view paints only over plain ink (TerminalView::setLinksColouredAtRest). Bold says code.
        QString inlineCode = QStringLiteral("1;97");
        QString codeBlock = QStringLiteral("36");
        QString dim = QStringLiteral("2;97");
        // Underlined ANSI 2 — the palette's dark green, which is what `[ui] link` defaults to, so
        // a link in the agent's prose is the green a path in program output and a fold's "open
        // x.py" wear (owner, 2026-09-19: "dark green, like Warp").
        QString link = QStringLiteral("4;32");
        // The three labelled bolds the system prompt teaches — **Done:**, **Need:**, **Problem:**
        // — so a reply's main point is findable at a glance in the scrollback (card #CVHT). Since
        // card #4E13 they speak the same language as the pane states and the "Relaying…" line:
        // bold green done (the Done glyph's own colour), bold amber need (every needs-you mark
        // is amber), bold red problem. Indexed like everything above, never absolute RGB: an
        // absolute colour is burnt into the scrollback and does not follow a theme switch.
        QString done = QStringLiteral("1;32");        // green
        QString need = QStringLiteral("1;33");        // amber
        QString problem = QStringLiteral("1;31");    // red
    };

    // `baseSgr` is the SGR parameter list of plain text, e.g. "38;2;226;229;235".
    explicit MarkdownAnsi(const QString &baseSgr = QStringLiteral("97"));
    explicit MarkdownAnsi(const Palette &palette);
    // Takes effect from the next line: anything held back keeps the colours it started in.
    void setPalette(const Palette &palette);

    // The OSC 8 anchor a link's *label* is hung from (card #MDKN, relay::labellink).
    //
    // Empty — the default — and the renderer emits nothing but SGR, exactly the bytes it emitted
    // before this existed: a surface that is not an anchored block of terminal output has nothing
    // to hang a label from, and the tests measure those bytes. Set to the URI of the block being
    // printed (a prose run, a fold), and `[LABEL](target)` wraps the label in an OSC 8 run whose
    // URI is that anchor with the target as its fragment, then re-opens the anchor. The
    // `(target)` printed after the label is left outside the run and is still plain text: it is
    // what a person reads before clicking, and it is what survives a restore from saved terminal
    // bytes, where OSC 8 is stripped.
    //
    // A block ends and the next one begins between chunks, so the caller sets this before each
    // feed(); a link never spans two feeds (it is held back until its `)` arrives).
    void setLinkAnchor(const QString &anchorUri) { m_linkAnchor = anchorUri; }
    QString linkAnchor() const { return m_linkAnchor; }

    // Inline images (card #1MGS). Off by default, and then `![alt](target)` is `!` and a link,
    // the bytes it always was. On, an image whose target is a local image file — an absolute
    // path, `file://`, `~/`, or a path relative to setImageBaseDir() — prints as a line of its
    // alt text (dim; the file name when there is none), then a kitty graphics escape for the file
    // at the start of its own line (imageEscape()); the engine turns that into the picture
    // (engine/core/InlineImage.h). The escape is only ever emitted whole: the image is held back
    // until its `)` arrives, like a link. An `http(s)` image is never fetched: it is a link with
    // its alt text as the label. A target that names no file prints the alt text and the target,
    // dimmed, and no escape. Code spans, fences and table cells are left exactly as before.
    //
    // A caller that has somewhere to anchor text (the pane's prose runs) has to write the escape
    // outside them: find it in the output with kImageEscapeStart.
    void setInlineImages(bool on) { m_images = on; }
    bool inlineImages() const { return m_images; }
    void setImageBaseDir(const QString &dir) { m_imageBase = dir; }
    // The pane's width in columns: a picture is fitted into min(columns, kImageMaxColumns).
    void setImageColumns(int columns) { m_imageColumns = columns; }
    // The pixel size of one cell, when the caller knows it; else kImageCellPixels.
    void setImageCellPixels(QSize pixels) { m_imageCell = pixels; }

    static constexpr int kImageMaxColumns = 80;
    static constexpr int kImageMaxRows = 20;       // a picture in a reply
    static constexpr int kThumbnailRows = 6;       // a prompt's attachment, a tool's image
    static const QSize kImageCellPixels;           // 8 x 16: the 1:2 cell of a terminal font
    static const QString kImageEscapeStart;        // "\x1b_G": the head of imageEscape()'s output
    static const QString kMediaEscapeStart;        // OSC 8 relay-media:, one local audio row

    // The file a Markdown image target names, made absolute, or an empty string when the target
    // is a URL other than file:// or names nothing on this machine that is a file.
    static QString resolveImageTarget(const QString &target, const QString &baseDir);
    // The cells a picture of `pixels` takes, fitted within maxColumns x maxRows keeping its
    // aspect ratio — the rule relay::inlineimage::cellsFor applies on the engine side.
    static QSize imageCells(QSize pixels, QSize cellPixels, int maxColumns, int maxRows);
    // `ESC _ G a=T,t=f,[f=100,]q=2,c=<cols>,r=<rows> ; <base64 path> ESC \` for the image file at
    // `absolutePath` (f=100 only when the file is a PNG: the engine sniffs any other format).
    // Its size is read from the file's header. Empty when the file is not an image.
    static QString imageEscape(const QString &absolutePath, int maxColumns, int maxRows,
                               QSize cellPixels = QSize());
    // A local audio file as one linked media row. Empty for other files. The row's manifest is
    // content-addressed in the private cache, and the view fills duration/waveform asynchronously.
    static QString mediaEscape(const QString &absolutePath, int maxColumns);

    QString feed(const QString &text);
    // Emits whatever is held back, closes open styles and a pending table, and resets all state
    // (a new text segment starts fresh). Never adds a newline of its own.
    QString finish();
    void reset();
    // Something to finish(): held input, a pending table, or a styled line still open.
    bool holding() const { return !m_pending.isEmpty() || !m_table.isEmpty() || m_lineStarted || !m_boldHold.isEmpty(); }

    // Printable width of rendered text: escape sequences removed.
    static int visibleWidth(const QString &rendered);

private:
    enum class Line { Paragraph, Heading1, Heading, Quote, Code };
    enum class BoldRole { None, Plain, Done, Need, Problem };

    QString process();
    bool decideLine(QString &out);
    bool inlineStep(QString &out, int &i);
    QString style() const;
    QString lineBase() const;
    QString renderTable();
    QString renderInline(const QString &text, bool bold) const;
    void resetInline();
    void flushBoldHold(QString &out);
    bool imageStep(QString &out, int &i);

    Palette m_palette;
    QString m_linkAnchor;   // #MDKN; a setting like the palette, so reset() leaves it alone
    // #1MGS: settings too, so reset() leaves them alone.
    bool m_images = false;
    QString m_imageBase;
    int m_imageColumns = 0;
    QSize m_imageCell;
    // An image's escape was the last thing on this line: whatever else the line holds starts a
    // new row under the picture.
    bool m_afterImage = false;
    QString m_pending;
    QStringList m_table;
    // Set on the inner renderer a table cell (or a row that turned out not to be a table) is drawn
    // with: inside one, a line beginning with `|` is text, not the start of another table. Without
    // it the two renderers call each other for ever — see renderInline().
    bool m_inlineOnly = false;
    bool m_final = false;
    bool m_lineStarted = false;
    Line m_line = Line::Paragraph;
    bool m_inFence = false;
    QChar m_fenceChar;
    int m_fenceLen = 0;
    bool m_bold = false, m_italic = false, m_strike = false;
    // Card #CVHT: while a bold run is open and its first word not yet classified, the run's first
    // characters are held here (bounded, see kBoldHoldMax) instead of emitted, so the role colour
    // can begin at the run's first character once the word is known.
    QString m_boldHold;
    BoldRole m_boldRole = BoldRole::None;
    int m_codeRun = 0;
    QChar m_prev;
};

}  // namespace relay
