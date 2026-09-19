// SPDX-License-Identifier: GPL-3.0-or-later
#include "CallLines.h"

#include "DiffView.h"
#include "MarkdownAnsi.h"

#include <QJsonArray>
#include <QStringList>
#include <QUrl>

namespace relay::calllines {

namespace {

QString encode(const QString &value) {
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

// A span with nothing but text and a colour: most of a fold is these.
FoldSpan plain(const QString &text, const QColor &fg = QColor()) {
    FoldSpan span;
    span.text = text;
    span.fg = fg;
    return span;
}

FoldLine oneSpan(const FoldSpan &span) {
    FoldLine line;
    line.spans << span;
    return line;
}

FoldLine row(const QString &text, const QColor &fg = QColor()) { return oneSpan(plain(text, fg)); }

FoldLine mutedRow(const QString &text, const Palette &palette) {
    FoldSpan span = plain(text, palette.muted);
    span.dim = true;
    return oneSpan(span);
}

FoldLine headingRow(const QString &text, const Palette &palette) {
    FoldSpan span = plain(text, palette.muted);
    span.bold = true;
    span.dim = true;
    return oneSpan(span);
}

// Splits a finished line at its first " · ": the title takes its own ink, the stats are muted.
Row splitAt(const QString &full, bool failed) {
    Row out;
    out.failed = failed;
    const int at = full.indexOf(QStringLiteral(" · "));
    if (at < 0) { out.title = full; return out; }
    out.title = full.left(at);
    out.rest = full.mid(at);
    return out;
}

// Cuts a Row to `cells`, taking it out of the muted remainder first: the title is what the line is
// about, and losing it to a narrow pane would leave a row that says nothing.
void cut(Row &line, int cells) {
    if (cells <= 0) return;
    if (line.title.size() + line.rest.size() <= cells) return;
    if (line.title.size() >= cells) { line.title = fit(line.title, cells); line.rest.clear(); return; }
    line.rest = fit(line.rest, cells - line.title.size());
}

// The run key a label would join, for a started label that carries no `merge` of its own (§ 23.1
// sends the fixed half only, and an older worker sends none).
QString mergeKeyOf(const toollabel::Label &label) {
    if (label.hasMerge) return label.mergeKey;
    if (label.kind == QStringLiteral("read")) return QStringLiteral("read");
    if (label.kind == QStringLiteral("list")) return QStringLiteral("list");
    return {};
}

}  // namespace

// ----- the anchor URI -------------------------------------------------------------------------

QString runCall(const QString &first, int count) {
    return count > 1 ? first + QLatin1Char('+') + QString::number(count) : first;
}

QString foldUri(const QString &pane, const QString &turn, const QString &call, int extra) {
    const QString tail = extra > 1 ? encode(call) + QLatin1Char('+') + QString::number(extra) : encode(call);
    return kFoldPrefix + encode(pane) + QLatin1Char('/') + encode(turn) + QLatin1Char('/') + tail;
}

QString openUri(const QString &pane, const QString &turn, const QString &call) {
    return kOpenPrefix + encode(pane) + QLatin1Char('/') + encode(turn) + QLatin1Char('/') + encode(call);
}

Ref parseUri(const QString &uri) {
    Ref out;
    if (!uri.startsWith(kFoldPrefix) && !uri.startsWith(kOpenPrefix)) return out;
    out.fold = uri.startsWith(kFoldPrefix);
    const QString rest = uri.mid((out.fold ? kFoldPrefix : kOpenPrefix).size());
    const QStringList parts = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 3) return out;
    out.valid = true;
    out.pane = QUrl::fromPercentEncoding(parts.at(0).toUtf8());
    out.turn = QUrl::fromPercentEncoding(parts.at(1).toUtf8());
    QString call = parts.at(2);
    const int plus = call.lastIndexOf(QLatin1Char('+'));
    if (plus > 0) {
        bool ok = false;
        const int extra = call.mid(plus + 1).toInt(&ok);
        if (ok && extra > 1) { out.extra = extra; call = call.left(plus); }
    }
    out.call = QUrl::fromPercentEncoding(call.toUtf8());
    return out;
}

// ----- the row --------------------------------------------------------------------------------

QString fit(const QString &text, int cells) {
    if (cells <= 0 || text.size() <= cells) return text;
    if (cells == 1) return QStringLiteral("…");
    return text.left(cells - 1) + QStringLiteral("…");
}

Row finishedRow(const toollabel::Label &label, int cells) {
    Row out;
    out.failed = label.failed();
    out.title = label.title.isEmpty() ? QStringLiteral("tool call") : label.title;
    if (out.failed) out.title += QStringLiteral(" ✗");
    QStringList pieces = label.stats;
    // A call that never happened says why on the same row; one that ran and failed already has
    // "exit 1" in its stats (§ 23.2).
    if (!label.error.isEmpty()) pieces << label.error;
    if (!pieces.isEmpty()) out.rest = QStringLiteral(" · ") + pieces.join(QStringLiteral(" · "));
    cut(out, cells);
    return out;
}

Row runningRow(const toollabel::Label &label, int cells, qint64 liveLines) {
    Row out;
    const QString head = !label.running.isEmpty() ? label.running
                       : !label.title.isEmpty()   ? label.title
                                                  : QStringLiteral("running");
    out.title = head + QStringLiteral("…");
    if (liveLines > 0)
        out.rest = QStringLiteral(" · %1 %2").arg(toollabel::thousands(liveLines),
                                                  liveLines == 1 ? QStringLiteral("line") : QStringLiteral("lines"));
    cut(out, cells);
    return out;
}

Row mergedRow(const toollabel::MergeRun &run, int cells) {
    Row out = splitAt(run.line(), false);
    cut(out, cells);
    return out;
}

// ----- what a click does ----------------------------------------------------------------------

Click clickFor(const toollabel::Label &label) {
    if (label.failed()) return Click::Fold;   // § 23.6: a failure always opens its fold
    const QString &type = label.openType;
    if (type == QStringLiteral("file")) return Click::File;
    if (type == QStringLiteral("diff")) return Click::Diff;
    if (type == QStringLiteral("subagent")) return Click::Subagent;
    if (type == QStringLiteral("card")) return Click::Card;
    if (type == QStringLiteral("plan")) return Click::Plan;
    if (type == QStringLiteral("todos")) return Click::Todos;
    return Click::Fold;
}

// ----- the state machine ------------------------------------------------------------------------

void LineCursor::dropRun() {
    m_run.clear();
    m_members.clear();
    m_first.clear();
    m_runRow = false;
}

QString LineCursor::openCall() const {
    if (!m_held) return {};
    return m_runRow ? runCall(m_first, m_run.count()) : m_started;
}

Step LineCursor::start(const QString &call, const toollabel::Label &label) {
    Step step;
    // Inside a run of reads a member that will merge shows nothing of its own: the run's row is
    // already on screen, and a "reading x.py…" that lives for a millisecond only flickers.
    const QString key = mergeKeyOf(label);
    if (m_held && m_runRow && !m_dirty && m_run.active() && !key.isEmpty() && key == m_run.key()) {
        step.nothing = true;
        m_started = call;
        return step;
    }
    if (m_held) { step.endRun = true; m_held = false; }
    dropRun();
    step.newRow = true;
    step.hold = true;               // the result rewrites this row in place
    step.row = runningRow(label, m_cells);
    step.call = call;
    step.callId = call;
    m_started = call;
    m_held = true;
    m_dirty = false;
    m_runRow = false;
    return step;
}

Step LineCursor::live(const QString &call, const toollabel::Label &label, qint64 lines, int cells) {
    Step step;
    if (!m_held || m_dirty || m_runRow || m_started != call || call.isEmpty()) { step.nothing = true; return step; }
    step.rewrite = true;
    step.hold = true;
    step.row = runningRow(label, cells > 0 ? cells : m_cells, lines);
    step.call = call;
    step.callId = call;
    return step;
}

Step LineCursor::result(const QString &call, const toollabel::Label &label, int cells) {
    const int width = cells > 0 ? cells : m_cells;
    Step step;
    // The run on screen grows: rewrite its row and keep the cursor on it.
    if (m_held && !m_dirty && m_runRow && m_run.accepts(label)) {
        m_run.add(label);
        m_members.append({label.line(), label.path, call});
        step.rewrite = true;
        step.hold = true;
        step.merged = m_run.count() > 1;
        step.row = step.merged ? mergedRow(m_run, width) : finishedRow(label, width);
        step.call = runCall(m_first, m_run.count());
        step.callId = m_first;
        step.extra = m_run.count() > 1 ? m_run.count() : 0;
        m_started.clear();
        return step;
    }
    if (m_held && !m_dirty && !m_runRow && m_started == call && !call.isEmpty()) {
        step.rewrite = true;        // this call's own "running…" row, untouched since
    } else {
        if (m_held) step.endRun = true;
        step.newRow = true;
    }
    m_held = false;
    dropRun();
    step.row = finishedRow(label, width);
    step.call = call;
    step.callId = call;
    // A mergeable result opens a run: the row keeps the cursor so the next read can rewrite it.
    if (label.hasMerge && !label.failed()) {
        m_run.add(label);
        m_members.append({label.line(), label.path, call});
        m_first = call;
        m_runRow = true;
        m_held = true;
        m_dirty = false;
        step.hold = true;
    }
    m_started.clear();
    return step;
}

Step LineCursor::other() {
    Step step;
    if (m_held) { step.endRun = true; m_held = false; }
    dropRun();
    m_started.clear();
    m_dirty = true;
    return step;
}

// ----- the fold's content -----------------------------------------------------------------------

QString stripAnsi(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (int at = 0; at < text.size(); ++at) {
        const ushort c = text.at(at).unicode();
        if (c == 0x1b) {
            // CSI and the other two-character escapes: skip to the sequence's final byte.
            if (at + 1 < text.size() && text.at(at + 1) == QLatin1Char('[')) {
                at += 2;
                while (at < text.size() && !(text.at(at).unicode() >= 0x40 && text.at(at).unicode() <= 0x7e)) ++at;
            } else if (at + 1 < text.size() && (text.at(at + 1) == QLatin1Char(']') || text.at(at + 1) == QLatin1Char('P'))) {
                at += 2;   // OSC / DCS: to BEL or ST
                while (at < text.size() && text.at(at).unicode() != 0x07
                       && !(text.at(at).unicode() == 0x1b && at + 1 < text.size() && text.at(at + 1) == QLatin1Char('\\')))
                    ++at;
                if (at < text.size() && text.at(at).unicode() == 0x1b) ++at;
            } else {
                ++at;
            }
            continue;
        }
        if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7f && !(c >= 0x80 && c < 0xa0))) out += text.at(at);
    }
    return out;
}

namespace {

// The diff of one section, as rows: add and remove keep their pair of colours on a tint, hunk
// headers are muted, and the `--- / +++` file rows are dropped — the line above the fold already
// names the file.
void appendDiff(QVector<FoldLine> &out, const QString &text, const Palette &palette) {
    const ParsedDiff diff = parseUnifiedDiff(text);
    if (diff.isEmpty()) { out << row(text, palette.text); return; }
    for (const DiffLine &line : diff.lines) {
        switch (line.kind) {
        case DiffLine::FileHeader:
            continue;
        case DiffLine::Hunk:
            out << mutedRow(line.text, palette);
            continue;
        case DiffLine::Add: {
            FoldSpan span = plain(line.text, palette.add);
            span.bg = palette.addBg;
            out << oneSpan(span);
            continue;
        }
        case DiffLine::Remove: {
            FoldSpan span = plain(line.text, palette.remove);
            span.bg = palette.removeBg;
            out << oneSpan(span);
            continue;
        }
        default:
            out << row(line.text, palette.text);
            continue;
        }
    }
}

// "open in pane · open x.py": the last row of every fold, so the whole thing is still reachable in
// a pane when the terminal is the wrong place to read it.
FoldLine linkRow(const Palette &palette, const FoldOptions &options) {
    FoldLine line;
    if (!options.openInPane.isEmpty()) {
        FoldSpan span = plain(QStringLiteral("open in pane"), palette.muted);
        span.link = options.openInPane;
        span.underline = true;
        line.spans << span;
    }
    if (!options.openPath.isEmpty()) {
        if (!line.spans.isEmpty()) line.spans << plain(QStringLiteral("  ·  "), palette.muted);
        QString name = options.openName;
        if (name.isEmpty()) {
            const int slash = options.openPath.lastIndexOf(QLatin1Char('/'));
            name = slash >= 0 ? options.openPath.mid(slash + 1) : options.openPath;
        }
        FoldSpan span = plain(QStringLiteral("open ") + name, palette.muted);
        span.link = options.openPath;
        span.underline = true;
        line.spans << span;
    }
    return line;
}

void capAndClose(QVector<FoldLine> &out, const Palette &palette, const FoldOptions &options) {
    const int cap = options.maxLines > 0 ? options.maxLines : kFoldLineCap;
    if (out.size() > cap) {
        const int more = out.size() - cap;
        out.resize(cap);
        out << mutedRow(QStringLiteral("… %1 more lines · open in pane").arg(toollabel::thousands(more)), palette);
    }
    const FoldLine links = linkRow(palette, options);
    if (!links.spans.isEmpty()) out << links;
}

// The renderer's ANSI foregrounds as the fold's Palette sees them. Reasoning is chrome around the
// reply, so its prose is muted; what still has to be findable keeps an ink: code and links the
// code colour, headings plain text, **Problem:** red.
QColor markdownFg(int sgr, const Palette &palette) {
    switch (sgr) {
    case 31: return palette.error;
    case 33: case 34: case 36: return palette.code;
    case 35: return palette.text;
    default: return palette.muted;
    }
}

// The renderer's ANSI into spans, one FoldLine per line. Only what a FoldSpan can say survives —
// bold, dim, italic, underline and the foreground; anything else the renderer emitted is dropped.
// Attributes carry across a newline exactly as they would on a grid, and MarkdownAnsi re-states
// each line's whole style at its start, so a line's first sequence lands as its own.
void appendMarkdown(QVector<FoldLine> &out, const QString &ansi, const Palette &palette) {
    if (out.isEmpty()) out << FoldLine{};
    FoldSpan span;
    QString text;
    const auto flush = [&] {
        if (text.isEmpty()) return;
        span.text = text;
        out.last().spans << span;
        text.clear();
    };
    for (int at = 0; at < ansi.size(); ++at) {
        const ushort u = ansi.at(at).unicode();
        if (u == 0x1b) {
            flush();
            if (at + 1 >= ansi.size()) break;
            if (ansi.at(at + 1) != QLatin1Char('[')) { ++at; continue; }   // two-character escape
            int end = at + 2;
            while (end < ansi.size() && ansi.at(end).unicode() >= 0x30 && ansi.at(end).unicode() <= 0x3f) ++end;
            if (end < ansi.size() && ansi.at(end) == QLatin1Char('m')) {
                const QStringList params = ansi.mid(at + 2, end - at - 2)
                                               .split(QLatin1Char(';'), Qt::SkipEmptyParts);
                for (const QString &param : params) {
                    bool ok = false;
                    const int n = param.toInt(&ok);
                    if (!ok) continue;
                    if (n == 0) { span = FoldSpan{}; continue; }   // a reset clears everything
                    if (n == 1) span.bold = true;
                    else if (n == 2) span.dim = true;
                    else if (n == 3) span.italic = true;
                    else if (n == 4) span.underline = true;
                    else if ((n >= 30 && n <= 37) || n == 39 || (n >= 90 && n <= 97))
                        span.fg = markdownFg(n, palette);
                    // 9 (crossed out) and the rest: a fold row cannot say them
                }
            } else {
                while (end < ansi.size() && !(ansi.at(end).unicode() >= 0x40 && ansi.at(end).unicode() <= 0x7e)) ++end;
            }
            at = end;   // the loop's ++at steps over the final byte
            continue;
        }
        if (u == '\n') {
            flush();
            out << FoldLine{};
            continue;
        }
        if (u == '\t' || (u >= 0x20 && u != 0x7f)) text += ansi.at(at);
    }
    flush();
}

}  // namespace

QVector<FoldLine> foldForReply(const QJsonObject &reply, const Palette &palette, const FoldOptions &options) {
    QVector<FoldLine> out;
    const QJsonArray sections = reply.value(QStringLiteral("detail")).toArray();
    const bool headings = sections.size() > 1;
    for (const QJsonValue &value : sections) {
        const QJsonObject section = value.toObject();
        const QString style = section.value(QStringLiteral("style")).toString();
        const QString heading = section.value(QStringLiteral("heading")).toString();
        const QString text = section.value(QStringLiteral("text")).toString();
        if (headings && !heading.isEmpty()) out << headingRow(heading, palette);
        if (style == QStringLiteral("diff")) {
            if (options.diffToPane) out << mutedRow(QStringLiteral("(opened in a diff pane)"), palette);
            else appendDiff(out, text, palette);
        } else if (style == QStringLiteral("code")) {
            // The command, as it would be typed. A heredoc keeps its shape; only the first row
            // carries the prompt.
            const QStringList lines = stripAnsi(text).split(QLatin1Char('\n'));
            for (int at = 0; at < lines.size(); ++at) {
                FoldSpan span = plain((at == 0 ? QStringLiteral("$ ") : QStringLiteral("  ")) + lines.at(at), palette.code);
                span.bold = true;
                out << oneSpan(span);
            }
        } else {
            const QColor colour = style == QStringLiteral("error") ? palette.error : palette.text;
            const QStringList lines = stripAnsi(text).split(QLatin1Char('\n'));
            for (const QString &line : lines) out << row(line, colour);
        }
        if (section.value(QStringLiteral("truncated")).toBool())
            out << mutedRow(QStringLiteral("(truncated)"), palette);
    }
    if (sections.isEmpty()) {
        // A worker from before § 23.5 answers with the text it has and no sections at all.
        const QString text = reply.contains(QStringLiteral("text")) ? reply.value(QStringLiteral("text")).toString()
                                                                    : reply.value(QStringLiteral("preview")).toString();
        if (text.isEmpty()) out << mutedRow(QStringLiteral("(No output recorded for this call.)"), palette);
        else for (const QString &line : stripAnsi(text).split(QLatin1Char('\n'))) out << row(line, palette.text);
    }
    capAndClose(out, palette, options);
    return out;
}

QVector<FoldLine> foldForRun(const QVector<RunMember> &members, const Palette &palette,
                             const FoldOptions &options) {
    QVector<FoldLine> out;
    for (const RunMember &member : members) {
        if (member.path.isEmpty()) { out << row(member.line, palette.text); continue; }
        FoldSpan span = plain(member.line, palette.text);
        span.link = member.path;
        span.underline = true;
        out << oneSpan(span);
    }
    if (out.isEmpty()) out << mutedRow(QStringLiteral("(Nothing was read in this run.)"), palette);
    FoldOptions withoutPath = options;
    withoutPath.openPath.clear();   // a run has no single file to open
    capAndClose(out, palette, withoutPath);
    return out;
}

QVector<FoldLine> foldForNote(const QString &text, const Palette &palette) {
    return {mutedRow(text, palette)};
}

QVector<FoldLine> foldForMarkdown(const QString &markdown, const Palette &palette,
                                  const FoldOptions &options) {
    QVector<FoldLine> out;
    if (markdown.isEmpty()) return out;
    MarkdownAnsi renderer;
    appendMarkdown(out, renderer.feed(markdown) + renderer.finish(), palette);
    while (!out.isEmpty() && out.first().spans.isEmpty()) out.removeFirst();
    while (!out.isEmpty() && out.last().spans.isEmpty()) out.removeLast();
    bool blank = true;
    for (const FoldLine &line : out)
        for (const FoldSpan &span : line.spans)
            if (!span.text.trimmed().isEmpty()) blank = false;
    if (blank) return {};
    const int cap = options.maxLines > 0 ? options.maxLines : kFoldLineCap;
    if (out.size() > cap) {
        const int earlier = out.size() - cap;
        out.remove(0, earlier);
        out.prepend(mutedRow(QStringLiteral("… %1 earlier lines · open in pane")
                                 .arg(toollabel::thousands(earlier)), palette));
    }
    const FoldLine links = linkRow(palette, options);
    if (!links.spans.isEmpty()) out << links;
    return out;
}

}  // namespace relay::calllines
