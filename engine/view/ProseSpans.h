// SPDX-License-Identifier: AGPL-3.0-or-later
// relay::ProseCollector: the logical lines of one block of the pane's own
// output, collected from the ANSI the pane renders (#R2WQ).
//
// The pane prints agent prose and its other inline text through MarkdownAnsi
// and the inks' own SGR, pre-wrapped by relay::WordWrap into the grid. To let
// the view re-wrap that block at another width, the pane hands over the same
// text *before* the wrapper — as FoldLine spans, so bold, italic, faint,
// underlines and the ink itself survive. This class is that conversion: it
// walks the rendered bytes, keeps the SGR state, and closes a span whenever
// the style changes.
//
// The style of a run is kept both as flags and as the SGR parameters that set
// it (FoldSpan::sgr); the view resolves the SGR against the theme when it
// paints, so a re-wrapped block follows a theme switch exactly as the grid's
// own rows do.
//
// Header-only and QtCore-only: the pane includes it, and the engine's unit
// tests exercise it next to the fold layer that consumes its output.
#pragma once

#include "TerminalBackend.h"
#include "LabelLinks.h"

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

namespace relay {

class ProseCollector {
public:
    // The lines so far, complete ones followed by the one still open.
    void feed(const QString &rendered);
    // The block's logical lines. Trailing newlines open fresh grid rows that
    // carry no cells of the block's OSC 8 run, so they are not the block's: an
    // empty last line is dropped, whoever prints next owns that row. An empty
    // line with something after it stays.
    QVector<FoldLine> take();

    // Test introspection: the style state a newly opened span would take.
    QString currentSgr() const;

private:
    void closeSpan();
    void applySgr(const QString &params);
    void applyOsc(const QString &body);

    QVector<FoldLine> m_lines;
    FoldLine m_line;         // the line being built
    FoldSpan m_open;         // the span being built (style set when it opened)
    bool m_bold = false, m_italic = false, m_underline = false, m_faint = false, m_reverse = false;
    int m_fg = -1;           // -1 = the default ink; else an ANSI index
    // The OSC 8 link the next span opens with (card #MDKN). Only a *label* URI is kept: an
    // anchor — the block's own prose run, a fold's URI — is a handle, not a destination, and a
    // span that carried one would make the whole block clickable.
    QString m_link;
    enum class Scan { Ground, Esc, Csi, Osc, OscEsc } m_scan = Scan::Ground;
    QString m_csi;
    QString m_osc;
};

inline QString ProseCollector::currentSgr() const
{
    QStringList p;
    if (m_bold) p << QStringLiteral("1");
    if (m_faint) p << QStringLiteral("2");
    if (m_italic) p << QStringLiteral("3");
    if (m_reverse) p << QStringLiteral("7");
    if (m_underline) p << QStringLiteral("4");
    if (m_fg >= 0) {
        if (m_fg < 8) p << QString::number(30 + m_fg);
        else if (m_fg < 16) p << QString::number(90 + m_fg - 8);
        else p << QStringLiteral("38;5;") + QString::number(m_fg);
    }
    return p.join(QLatin1Char(';'));
}

inline void ProseCollector::closeSpan()
{
    if (!m_open.text.isEmpty())
        m_line.spans << m_open;
    m_open = FoldSpan{};
}

inline void ProseCollector::applySgr(const QString &params)
{
    const QStringList ps = params.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (int i = 0; i < ps.size(); ++i) {
        const int p = ps.at(i).toInt();
        if (p == 0) { m_bold = m_italic = m_underline = m_faint = m_reverse = false; m_fg = -1; }
        else if (p == 1) m_bold = true;
        else if (p == 2) m_faint = true;
        else if (p == 3) m_italic = true;
        else if (p == 4 || p == 21) m_underline = true;
        else if (p == 7) m_reverse = true;
        else if (p == 27) m_reverse = false;
        else if (p == 22) { m_bold = m_faint = false; }
        else if (p == 23) m_italic = false;
        else if (p == 24) m_underline = false;
        else if (p == 39) m_fg = -1;
        else if (p >= 30 && p <= 37) m_fg = p - 30;
        else if (p >= 90 && p <= 97) m_fg = p - 90 + 8;
        else if (p == 38 && i + 1 < ps.size()) {
            const int mode = ps.at(i + 1).toInt();
            if (mode == 5 && i + 2 < ps.size()) { m_fg = ps.at(i + 2).toInt(); i += 2; }
            else if (mode == 2) i += 4;   // a truecolour: no theme index to follow
        }
    }
    closeSpan();   // the style change ends the span in progress
}

// OSC 8 (card #MDKN): `8;;<uri>`. A label's URI becomes the span's link, which is how a re-wrapped
// prose block and a markdown fold keep a clickable label — the view hit-tests FoldSpan::link, the
// same field a tool-call fold's rows use. An anchor, or the empty URI that closes a run, clears it.
inline void ProseCollector::applyOsc(const QString &body)
{
    if (!body.startsWith(QLatin1String("8;")))
        return;
    const int semi = body.indexOf(QLatin1Char(';'), 2);   // past the params field, which is unused
    if (semi < 0)
        return;
    const QString uri = body.mid(semi + 1);
    const QString link = labellink::isLabelUri(uri) ? uri : QString();
    if (link == m_link)
        return;
    closeSpan();   // the link change ends the span in progress, exactly as a style change does
    m_link = link;
}

inline void ProseCollector::feed(const QString &rendered)
{
    for (const QChar ch : rendered) {
        switch (m_scan) {
        case Scan::Ground:
            if (ch == QChar(0x1b)) { m_scan = Scan::Esc; continue; }
            if (ch == QLatin1Char('\n')) {
                closeSpan();
                m_lines << m_line;
                m_line = FoldLine{};
                continue;
            }
            if (m_open.text.isEmpty()) {
                m_open.sgr = currentSgr();
                m_open.bold = m_bold;
                m_open.italic = m_italic;
                m_open.underline = m_underline;
                m_open.dim = m_faint;
                m_open.reverse = m_reverse;
                m_open.link = m_link;
            }
            m_open.text += ch;
            continue;
        case Scan::Esc:
            if (ch == QLatin1Char('[')) { m_scan = Scan::Csi; m_csi.clear(); continue; }
            if (ch == QLatin1Char(']')) { m_scan = Scan::Osc; m_osc.clear(); continue; }
            m_scan = Scan::Ground;   // a two-character escape: nothing to keep
            continue;
        case Scan::Csi: {
            const ushort u = ch.unicode();
            if (u >= 0x40 && u <= 0x7e) {
                applySgr(m_csi);
                m_scan = Scan::Ground;
                continue;
            }
            m_csi += ch;   // parameters and intermediates until the final byte
            continue;
        }
        case Scan::Osc:
            if (ch == QChar(0x07)) { applyOsc(m_osc); m_scan = Scan::Ground; continue; }
            if (ch == QChar(0x1b)) { m_scan = Scan::OscEsc; continue; }
            m_osc += ch;
            continue;
        case Scan::OscEsc:
            applyOsc(m_osc);
            m_scan = Scan::Ground;   // ST: the OSC is over
            continue;
        }
    }
}

inline QVector<FoldLine> ProseCollector::take()
{
    closeSpan();
    if (!m_line.spans.isEmpty())
        m_lines << m_line;
    while (!m_lines.isEmpty() && m_lines.last().spans.isEmpty())
        m_lines.removeLast();
    return m_lines;
}

}  // namespace relay
