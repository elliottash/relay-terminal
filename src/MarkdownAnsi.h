// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

    Palette m_palette;
    QString m_pending;
    QStringList m_table;
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
