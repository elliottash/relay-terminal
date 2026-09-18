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
    // Every colour the renderer can emit, as SGR parameter lists ("38;2;R;G;B", plus any
    // attributes). The defaults are the dark-theme values this renderer shipped with; the app
    // fills them from the live theme instead, because near-white prose on IBM Beige's warm paper
    // is unreadable (owner report, 2026-09-18). Lines already printed keep the colours they were
    // written in: a terminal cannot recolour its scrollback.
    struct Palette {
        QString base = QStringLiteral("39");
        QString heading = QStringLiteral("1;38;2;180;142;247");
        QString marker = QStringLiteral("38;2;180;142;247");
        QString quote = QStringLiteral("3;38;2;160;166;180");
        QString inlineCode = QStringLiteral("38;2;230;170;120");
        QString codeBlock = QStringLiteral("38;2;170;200;230");
        QString dim = QStringLiteral("38;2;110;117;132");
        QString link = QStringLiteral("4;38;2;110;170;245");
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
