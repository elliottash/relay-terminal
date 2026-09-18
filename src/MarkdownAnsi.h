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
    // The defaults name the *terminal's own* colours — the default foreground (39), the faint
    // attribute, and the ANSI indices — rather than absolute RGB, and that is the point. An
    // absolute colour is burnt into the scrollback: prose written under one theme stays that
    // colour when the theme changes, so a switch to IBM Beige left near-white paragraphs on warm
    // paper and a switch back left dark ones on charcoal (owner report, 2026-09-18). Indexed
    // colours are resolved by the engine at paint time from the active theme, so the whole
    // transcript, scrollback included, follows the theme for free — and it inherits the contrast
    // every theme's palette is measured for (docs/THEMES.md; tests/theme_test.cpp asserts AA for
    // ANSI 1-7 and 9-15 on the grid).
    //
    // An app that wants its own colours can still set them; nothing here requires the defaults.
    struct Palette {
        QString base = QStringLiteral("39");            // the terminal's own foreground
        QString heading = QStringLiteral("1;35");       // magenta, the agent's end of the palette
        QString marker = QStringLiteral("35");
        QString quote = QStringLiteral("3;2;39");       // italic, faint: quieter by proportion
        QString inlineCode = QStringLiteral("33");
        QString codeBlock = QStringLiteral("36");
        QString dim = QStringLiteral("2;39");
        QString link = QStringLiteral("4;34");
    };

    // `baseSgr` is the SGR parameter list of plain text, e.g. "38;2;226;229;235".
    explicit MarkdownAnsi(const QString &baseSgr = QStringLiteral("39"));
    explicit MarkdownAnsi(const Palette &palette);
    // Takes effect from the next line: anything held back keeps the colours it started in.
    void setPalette(const Palette &palette);

    QString feed(const QString &text);
    // Emits whatever is held back, closes open styles and a pending table, and resets all state
    // (a new text segment starts fresh). Never adds a newline of its own.
    QString finish();
    void reset();
    // Something to finish(): held input, a pending table, or a styled line still open.
    bool holding() const { return !m_pending.isEmpty() || !m_table.isEmpty() || m_lineStarted; }

    // Printable width of rendered text: escape sequences removed.
    static int visibleWidth(const QString &rendered);

private:
    enum class Line { Paragraph, Heading1, Heading, Quote, Code };

    QString process();
    bool decideLine(QString &out);
    bool inlineStep(QString &out, int &i);
    QString style() const;
    QString lineBase() const;
    QString renderTable();
    QString renderInline(const QString &text, bool bold) const;
    void resetInline();

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
    int m_codeRun = 0;
    QChar m_prev;
};

}  // namespace relay
