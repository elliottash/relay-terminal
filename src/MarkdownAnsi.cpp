// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MarkdownAnsi.h"

#include "Images.h"
#include "LabelLinks.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace relay {

namespace {

const QString kEsc = QStringLiteral("\x1b[");
const QString kReset = QStringLiteral("\x1b[0m");
// Colours follow the inline inks in main.cpp: violet is the agent, amber is a tool.

QString sgr(const QString &params) { return kEsc + params + QLatin1Char('m'); }

// OSC 8 with an ST terminator, the form every writer in the pane uses. An empty URI closes the
// run; a link's label re-opens the block's own anchor rather than closing, so the block the label
// sits in carries on (card #MDKN).
QString osc8(const QString &uri) { return QStringLiteral("\x1b]8;;") + uri + QStringLiteral("\x1b\\"); }

int runLength(const QString &text, int from, QChar c) {
    int n = 0;
    while (from + n < text.size() && text.at(from + n) == c) ++n;
    return n;
}

// Leading spaces, then the rest of the line.
int indentOf(const QString &line) {
    int n = 0;
    while (n < line.size() && (line.at(n) == QLatin1Char(' ') || line.at(n) == QLatin1Char('\t'))) ++n;
    return n;
}

// "---", "* * *", "___": one marker character repeated, spaces allowed.
bool ruleShape(const QString &rest, int *count) {
    if (rest.isEmpty()) return false;
    const QChar c = rest.at(0);
    if (c != QLatin1Char('-') && c != QLatin1Char('*') && c != QLatin1Char('_')) return false;
    int n = 0;
    for (const QChar ch : rest) {
        if (ch == c) ++n;
        else if (ch != QLatin1Char(' ')) return false;
    }
    if (count) *count = n;
    return true;
}

QStringList tableCells(const QString &row) {
    QString text = row.trimmed();
    if (text.startsWith(QLatin1Char('|'))) text.remove(0, 1);
    if (text.endsWith(QLatin1Char('|')) && !text.endsWith(QStringLiteral("\\|"))) text.chop(1);
    QStringList cells;
    QString cell;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('|')) { cell += QLatin1Char('|'); ++i; }
        else if (text.at(i) == QLatin1Char('|')) { cells << cell.trimmed(); cell.clear(); }
        else cell += text.at(i);
    }
    cells << cell.trimmed();
    return cells;
}

// Card #CVHT (bolding main points): the closed keyword lists behind **Done:** / **Need:** /
// **Problem:**. A bold run is coloured when its *first* word — punctuation stripped, compared
// case-insensitively — is in one of these lists; anything else stays plain bold, as before.
const QStringList kDoneWords = {QStringLiteral("done"), QStringLiteral("finished"), QStringLiteral("complete"),
                                QStringLiteral("completed"), QStringLiteral("ready"), QStringLiteral("success"),
                                QStringLiteral("passed"), QStringLiteral("fixed"), QStringLiteral("works"),
                                QStringLiteral("working")};
const QStringList kProblemWords = {QStringLiteral("problem"), QStringLiteral("error"), QStringLiteral("failed"),
                                   QStringLiteral("failure"), QStringLiteral("broken"), QStringLiteral("blocked"),
                                   QStringLiteral("warning"), QStringLiteral("bug")};
const QStringList kNeedWords = {QStringLiteral("need"), QStringLiteral("needs"), QStringLiteral("question"),
                                QStringLiteral("waiting"), QStringLiteral("ask"), QStringLiteral("decision")};
// Enough held characters for the first word of a bold run and a little more; a stray unclosed `**`
// cannot stall the stream for longer than this.
constexpr int kBoldHoldMax = 32;

// Card #1MGS: an image target that is a URL of some other scheme (`https:`, `data:`) is never
// fetched. A single letter before the colon is a Windows drive, not a scheme.
bool isForeignUrl(const QString &target) {
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
    return scheme.match(target).hasMatch() && !target.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive);
}

// `<path>` and a trailing `"title"` are Markdown's, not the path's.
QString bareTarget(QString target) {
    target = target.trimmed();
    static const QRegularExpression title(QStringLiteral("\\s+(\"[^\"]*\"|'[^']*')$"));
    target.remove(title);
    if (target.size() >= 2 && target.startsWith(QLatin1Char('<')) && target.endsWith(QLatin1Char('>')))
        target = target.mid(1, target.size() - 2);
    return target;
}

}  // namespace

const QSize MarkdownAnsi::kImageCellPixels(8, 16);
const QString MarkdownAnsi::kImageEscapeStart = QStringLiteral("\x1b_G");
const QString MarkdownAnsi::kMediaEscapeStart = QStringLiteral("\x1b]8;;relay-media:");

QString MarkdownAnsi::resolveImageTarget(const QString &target, const QString &baseDir) {
    QString path = bareTarget(target);
    if (path.isEmpty() || isForeignUrl(path)) return QString();
    if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) path = QUrl(path).toLocalFile();
    else if (path == QStringLiteral("~") || path.startsWith(QStringLiteral("~/"))) path = QDir::homePath() + path.mid(1);
    else if (QDir::isRelativePath(path)) {
        if (baseDir.isEmpty()) return QString();
        path = QDir(baseDir).filePath(path);
    }
    if (path.isEmpty()) return QString();
    QFileInfo info(path);
    if (!info.isFile() && path.contains(QLatin1Char('%'))) info = QFileInfo(QUrl::fromPercentEncoding(path.toUtf8()));
    return info.isFile() ? info.absoluteFilePath() : QString();
}

QSize MarkdownAnsi::imageCells(QSize pixels, QSize cellPixels, int maxColumns, int maxRows) {
    if (!cellPixels.isValid() || cellPixels.isEmpty()) cellPixels = kImageCellPixels;
    maxColumns = std::max(1, maxColumns);
    maxRows = std::max(1, maxRows);
    const double pw = std::max(1, pixels.width()), ph = std::max(1, pixels.height());
    double cols = pw / cellPixels.width(), rows = ph / cellPixels.height();
    const double scale = std::min({1.0, maxColumns / cols, maxRows / rows});
    cols *= scale;
    rows *= scale;
    return QSize(std::clamp(int(std::ceil(cols - 1e-6)), 1, maxColumns),
                 std::clamp(int(std::ceil(rows - 1e-6)), 1, maxRows));
}

QString MarkdownAnsi::imageEscape(const QString &absolutePath, int maxColumns, int maxRows, QSize cellPixels) {
    const QString type = images::mediaTypeOf(absolutePath);
    if (type.isEmpty()) return QString();
    QSize pixels = QImageReader(absolutePath).size();
    // A format this build cannot decode still has a picture in it for the engine to try: a
    // square, as tall as it may be.
    if (!pixels.isValid() || pixels.isEmpty()) pixels = QSize(1024, 1024);
    const QSize cells = imageCells(pixels, cellPixels, maxColumns, maxRows);
    return QStringLiteral("\x1b_Ga=T,t=f,%1q=2,c=%2,r=%3;%4\x1b\\")
        .arg(type == QStringLiteral("image/png") ? QStringLiteral("f=100,") : QString())
        .arg(cells.width())
        .arg(cells.height())
        .arg(QString::fromLatin1(absolutePath.toUtf8().toBase64()));
}

QString MarkdownAnsi::mediaEscape(const QString &absolutePath, int maxColumns) {
    QFile source(absolutePath);
    if (!source.open(QIODevice::ReadOnly)) return {};
    const QByteArray head = source.read(16);
    const QString ext = QFileInfo(absolutePath).suffix().toLower();
    const bool audio = (head.startsWith("RIFF") && head.mid(8, 4) == "WAVE") ||
                       head.startsWith("ID3") || head.startsWith("OggS") || head.startsWith("fLaC") ||
                       ext == QStringLiteral("mp3") || ext == QStringLiteral("m4a") ||
                       ext == QStringLiteral("aac") || ext == QStringLiteral("opus");
    if (!audio) return {};
    const QString base = qEnvironmentVariable("XDG_CACHE_HOME");
    const QString cache = (base.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                                          : base) + QStringLiteral("/relay/media");
    if (cache.isEmpty() || !QDir().mkpath(cache)) return {};
    QFile::setPermissions(cache, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QJsonObject object{{QStringLiteral("version"), 1}, {QStringLiteral("kind"), QStringLiteral("audio")},
                             {QStringLiteral("path"), QFileInfo(absolutePath).absoluteFilePath()}};
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const QString name = QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex())
                         + QStringLiteral(".json");
    const QString manifest = QDir(cache).filePath(name);
    if (!QFileInfo::exists(manifest)) {
        QSaveFile file(manifest);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (file.write(content) != content.size() || !file.commit()) return {};
    }
    const int cols = std::clamp(maxColumns, 1, 120);
    const QString uri = QStringLiteral("relay-media:0/1/%1/%2")
        .arg(cols).arg(QString::fromLatin1(QUrl::toPercentEncoding(manifest)));
    return osc8(uri) + QChar(0x2800) + osc8(QString());
}

MarkdownAnsi::MarkdownAnsi(const QString &baseSgr) { m_palette.base = baseSgr; }
MarkdownAnsi::MarkdownAnsi(const Palette &palette) : m_palette(palette) {}
void MarkdownAnsi::setPalette(const Palette &palette) { m_palette = palette; }

void MarkdownAnsi::reset() {
    m_pending.clear();
    m_table.clear();
    m_final = false;
    m_lineStarted = false;
    m_line = Line::Paragraph;
    m_inFence = false;
    m_fenceLen = 0;
    resetInline();
}

void MarkdownAnsi::resetInline() {
    m_bold = m_italic = m_strike = false;
    m_codeRun = 0;
    m_prev = QChar();
    m_afterImage = false;
    m_boldHold.clear();
    m_boldRole = BoldRole::None;
}

int MarkdownAnsi::visibleWidth(const QString &rendered) {
    int width = 0;
    for (int i = 0; i < rendered.size(); ++i) {
        const QChar c = rendered.at(i);
        if (c == QChar(0x1b)) {
            // An OSC (a label's link, #MDKN) runs to BEL or ST and its body is full of letters:
            // the CSI rule below would stop at the `r` of `relay://…` and count the rest of the
            // URI as text, which is what a table cell's width is measured with.
            // An image's kitty escape (APC, `ESC _`, #1MGS) is a string of the same shape.
            if (i + 1 < rendered.size() && (rendered.at(i + 1) == QLatin1Char(']') || rendered.at(i + 1) == QLatin1Char('_'))) {
                i += 2;
                while (i < rendered.size() && rendered.at(i) != QChar(0x07) && rendered.at(i) != QChar(0x1b)) ++i;
                if (i < rendered.size() && rendered.at(i) == QChar(0x1b)) ++i;   // ST: ESC \, both
                continue;
            }
            while (i < rendered.size() && !rendered.at(i).isLetter()) ++i;   // through the final byte
            continue;
        }
        if (c.isLowSurrogate()) continue;
        ++width;
    }
    return width;
}

QString MarkdownAnsi::feed(const QString &text) {
    m_pending += text;
    return process();
}

QString MarkdownAnsi::finish() {
    m_final = true;
    QString out = process();
    flushBoldHold(out);   // an open bold run's held first word still deserves its colour
    if (!m_table.isEmpty()) out += renderTable();
    if (m_lineStarted) out += kReset;
    reset();
    return out;
}

QString MarkdownAnsi::lineBase() const {
    switch (m_line) {
    case Line::Heading1: return m_palette.heading + QStringLiteral(";4");
    case Line::Heading: return m_palette.heading;
    case Line::Quote: return m_palette.quote;
    case Line::Code: return m_palette.codeBlock;
    case Line::Paragraph: break;
    }
    return m_palette.base;
}

QString MarkdownAnsi::style() const {
    QString params = QStringLiteral("0;") + lineBase();
    if (m_bold) {
        params += QStringLiteral(";1");
        // Card #CVHT: once a bold run's first word is classified, its role colour holds for the
        // rest of the run (until the closing marker or the end of the line).
        if (m_boldRole == BoldRole::Done) params += QLatin1Char(';') + m_palette.done;
        else if (m_boldRole == BoldRole::Need) params += QLatin1Char(';') + m_palette.need;
        else if (m_boldRole == BoldRole::Problem) params += QLatin1Char(';') + m_palette.problem;
    }
    if (m_italic) params += QStringLiteral(";3");
    if (m_strike) params += QStringLiteral(";9");
    if (m_codeRun > 0) params += QLatin1Char(';') + m_palette.inlineCode;
    return sgr(params);
}

QString MarkdownAnsi::process() {
    QString out;
    for (;;) {
        if (!m_lineStarted) {
            if (!decideLine(out)) break;
            continue;
        }
        int i = 0;
        bool lineDone = false;
        while (i < m_pending.size()) {
            if (m_pending.at(i) == QLatin1Char('\n')) {
                flushBoldHold(out);   // a bold run left open at the line end still gets its colour
                out += kReset + QLatin1Char('\n');
                ++i;
                m_lineStarted = false;
                m_line = Line::Paragraph;
                resetInline();
                lineDone = true;
                break;
            }
            if (m_line == Line::Code) { out += m_pending.at(i++); continue; }
            if (!inlineStep(out, i)) break;   // held: needs more input
        }
        m_pending.remove(0, i);
        if (!lineDone) break;
    }
    return out;
}

// Classifies the line at the start of m_pending and emits its prefix. False when more input is
// needed to tell what the line is.
bool MarkdownAnsi::decideLine(QString &out) {
    if (m_pending.isEmpty()) return false;
    const int nl = m_pending.indexOf(QLatin1Char('\n'));
    const bool complete = nl >= 0 || m_final;
    const int end = nl >= 0 ? nl : m_pending.size();
    const int consumed = nl >= 0 ? nl + 1 : end;
    const QString newline = nl >= 0 ? QStringLiteral("\n") : QString();
    const QString line = m_pending.left(end);
    const int indent = indentOf(line);
    const QString rest = line.mid(indent);

    // A table is rendered once its last row is in: column widths depend on every row.
    if (!m_table.isEmpty()) {
        if (!complete && (rest.isEmpty() || rest.startsWith(QLatin1Char('|')))) return false;
        if (complete && rest.startsWith(QLatin1Char('|'))) {
            m_table << line;
            m_pending.remove(0, consumed);
            return true;
        }
        out += renderTable();
    }

    auto startLine = [&](Line kind, const QString &prefix, int skip) {
        m_line = kind;
        m_lineStarted = true;
        resetInline();
        out += prefix + style();
        m_pending.remove(0, skip);
    };

    if (m_inFence) {
        const bool couldClose = rest.isEmpty() || runLength(rest, 0, m_fenceChar) == rest.size();
        if (!complete && couldClose) return false;
        if (complete && runLength(rest, 0, m_fenceChar) >= m_fenceLen && rest.trimmed().size() == runLength(rest, 0, m_fenceChar)) {
            m_inFence = false;
            out += sgr(m_palette.dim) + line + kReset + newline;
            m_pending.remove(0, consumed);
            return true;
        }
        startLine(Line::Code, QString(), 0);
        return true;
    }

    if (rest.isEmpty()) {
        if (!complete) return false;
        out += line + newline;
        m_pending.remove(0, consumed);
        return true;
    }

    // Fences: ``` or ~~~, with an optional language.
    const QChar first = rest.at(0);
    if (first == QLatin1Char('`') || first == QLatin1Char('~')) {
        const int run = runLength(rest, 0, first);
        if (!complete && run == rest.size() && run < 3) return false;
        if (run >= 3 && (first == QLatin1Char('~') || !rest.mid(run).contains(QLatin1Char('`')))) {
            if (!complete) return false;
            m_inFence = true;
            m_fenceChar = first;
            m_fenceLen = run;
            out += sgr(m_palette.dim) + line + kReset + newline;
            m_pending.remove(0, consumed);
            return true;
        }
    }

    // A table cell's own renderer never starts a table: `|` inside a cell is a character, and a
    // row that renderTable() has already decided is not a table must not become one again.
    if (first == QLatin1Char('|') && !m_inlineOnly) {
        if (!complete) return false;
        m_table << line;
        m_pending.remove(0, consumed);
        return true;
    }

    int ruleCount = 0;
    if (ruleShape(rest, &ruleCount)) {
        if (!complete) return false;
        if (ruleCount >= 3) {
            out += sgr(m_palette.dim) + QString(40, QChar(0x2500)) + kReset + newline;
            m_pending.remove(0, consumed);
            return true;
        }
    }

    if (first == QLatin1Char('#')) {
        const int hashes = runLength(rest, 0, first);
        if (!complete && hashes == rest.size() && hashes <= 6) return false;
        if (hashes <= 6 && (hashes == rest.size() || rest.at(hashes) == QLatin1Char(' '))) {
            int skip = indent + hashes;
            while (skip < end && m_pending.at(skip) == QLatin1Char(' ')) ++skip;
            startLine(hashes == 1 ? Line::Heading1 : Line::Heading, line.left(indent), skip);
            return true;
        }
    }

    if (first == QLatin1Char('>')) {
        if (!complete && rest.size() == 1) return false;
        int skip = indent + 1;
        if (skip < end && m_pending.at(skip) == QLatin1Char(' ')) ++skip;
        startLine(Line::Quote, line.left(indent) + sgr(m_palette.marker) + QStringLiteral("▎ "), skip);
        return true;
    }

    if (first == QLatin1Char('-') || first == QLatin1Char('*') || first == QLatin1Char('+')) {
        if (!complete && rest.size() == 1) return false;
        if (rest.size() == 1 || rest.at(1) == QLatin1Char(' ')) {
            const QString after = rest.mid(2);
            static const QStringList boxes = {QStringLiteral("[ ] "), QStringLiteral("[x] "), QStringLiteral("[X] ")};
            if (!complete)
                for (const QString &box : boxes)
                    if (box.startsWith(after) && after.size() < box.size()) return false;
            const int level = indent / 2;
            QString glyph = level == 0 ? QStringLiteral("•") : level == 1 ? QStringLiteral("◦") : QStringLiteral("▪");
            int skip = indent + std::min<int>(2, rest.size());
            if (after.startsWith(boxes.at(0))) { glyph = QStringLiteral("☐"); skip += 4; }
            else if (after.startsWith(boxes.at(1)) || after.startsWith(boxes.at(2))) { glyph = QStringLiteral("☑"); skip += 4; }
            startLine(Line::Paragraph, line.left(indent) + sgr(m_palette.marker) + glyph + QLatin1Char(' '), skip);
            return true;
        }
    }

    if (first.isDigit()) {
        int digits = 0;
        while (digits < rest.size() && rest.at(digits).isDigit()) ++digits;
        if (!complete && digits == rest.size() && digits <= 9) return false;
        if (digits <= 9 && digits < rest.size() && (rest.at(digits) == QLatin1Char('.') || rest.at(digits) == QLatin1Char(')'))) {
            if (!complete && digits + 1 == rest.size()) return false;
            if (digits + 1 == rest.size() || rest.at(digits + 1) == QLatin1Char(' ')) {
                const int skip = indent + std::min<int>(digits + 2, rest.size());
                startLine(Line::Paragraph, line.left(indent) + sgr(m_palette.marker) + rest.left(digits + 1) + QLatin1Char(' '), skip);
                return true;
            }
        }
    }

    startLine(Line::Paragraph, line.left(indent), indent);
    return true;
}

// One step of inline Markdown at m_pending[i]. False when the text from i needs more input.
bool MarkdownAnsi::inlineStep(QString &out, int &i) {
    const QString &text = m_pending;
    const QChar c = text.at(i);
    const auto atEnd = [&](int j) { return j >= text.size(); };
    const auto next = [&](int j) { return atEnd(j) ? QChar(QLatin1Char(' ')) : text.at(j); };
    const auto isSpace = [](QChar ch) { return ch.isNull() || ch.isSpace(); };

    // Card #CVHT: a held first word is classified before any inline marker is handled, so the
    // markers of a bold run are never mistaken for its text.
    if (!m_boldHold.isEmpty()
        && (c == QLatin1Char('*') || c == QLatin1Char('_') || c == QLatin1Char('`')
            || c == QLatin1Char('~') || c == QLatin1Char('[') || c == QLatin1Char('\\')))
        flushBoldHold(out);

    // Card #1MGS: the rest of a line an image ended starts on a row of its own, under the picture.
    if (m_afterImage) {
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) { ++i; return true; }
        m_afterImage = false;
        out += QLatin1Char('\n') + style();
    }

    if (c == QLatin1Char('\\') && m_codeRun == 0) {
        if (atEnd(i + 1) && !m_final) return false;
        const QChar escaped = next(i + 1);
        if (escaped.isPunct() || escaped.isSymbol()) { out += escaped; m_prev = escaped; i += 2; return true; }
        out += c; m_prev = c; ++i;
        return true;
    }

    if (c == QLatin1Char('`')) {
        const int run = runLength(text, i, c);
        if (atEnd(i + run) && !m_final) return false;
        if (m_codeRun == 0) m_codeRun = run;
        else if (run == m_codeRun) m_codeRun = 0;
        else { out += text.mid(i, run); i += run; return true; }
        out += style();
        m_prev = c; i += run;
        return true;
    }
    if (m_codeRun > 0) { out += c; m_prev = c; ++i; return true; }

    if (c == QLatin1Char('!') && m_images && !m_inlineOnly) {
        const int at = i;
        if (!imageStep(out, i)) return false;
        if (i != at) return true;   // handled: the `!` is gone
    }

    if (c == QLatin1Char('*') || c == QLatin1Char('_')) {
        const int run = runLength(text, i, c);
        if (atEnd(i + run) && !m_final) return false;
        const QChar after = next(i + run);
        bool canOpen = !isSpace(after), canClose = !isSpace(m_prev);
        if (c == QLatin1Char('_')) {   // snake_case is not emphasis
            canOpen = canOpen && !m_prev.isLetterOrNumber();
            canClose = canClose && !after.isLetterOrNumber();
        }
        int left = run > 3 ? 0 : run;
        bool changed = false;
        if (canClose && left > 0) {
            if (left >= 2 && m_bold) { m_bold = false; m_boldRole = BoldRole::None; left -= 2; changed = true; }
            if (left >= 1 && m_italic) { m_italic = false; left -= 1; changed = true; }
            if (left >= 2 && m_bold) { m_bold = false; m_boldRole = BoldRole::None; left -= 2; changed = true; }
        }
        if (!changed && canOpen && left > 0) {
            if (left >= 2) m_bold = true;
            if (left != 2) m_italic = true;
            left = 0;
            changed = true;
            if (m_bold) { m_boldRole = BoldRole::None; m_boldHold.clear(); }   // a fresh run: classify it
        }
        if (changed) out += style();
        if (!changed) left = run;
        if (left > 0) out += QString(left, c);
        m_prev = c; i += run;
        return true;
    }

    if (c == QLatin1Char('~')) {
        const int run = runLength(text, i, c);
        if (atEnd(i + run) && !m_final) return false;
        if (run == 2 && ((m_strike && !isSpace(m_prev)) || (!m_strike && !isSpace(next(i + run))))) {
            m_strike = !m_strike;
            out += style();
        } else {
            out += text.mid(i, run);
        }
        m_prev = c; i += run;
        return true;
    }

    if (c == QLatin1Char('[')) {
        // [text](url) on one line; held until it resolves, bounded so a stray '[' cannot stall.
        const int lineEnd = text.indexOf(QLatin1Char('\n'), i);
        const int limit = lineEnd < 0 ? text.size() : lineEnd;
        const int close = text.indexOf(QLatin1Char(']'), i);
        const bool open = lineEnd < 0 && !m_final && limit - i < 400;
        if ((close < 0 || close > limit) && open) return false;
        if (close >= 0 && close < limit) {
            if (close + 1 >= limit && open) return false;
            if (close + 1 < limit && text.at(close + 1) == QLatin1Char('(')) {
                const int paren = text.indexOf(QLatin1Char(')'), close + 2);
                if ((paren < 0 || paren > limit) && open) return false;
                if (paren >= 0 && paren < limit) {
                    const QString label = text.mid(i + 1, close - i - 1);
                    const QString url = text.mid(close + 2, paren - close - 2).trimmed();
                    // Card #MDKN: the label is painted like a link, so it is one. Its cells carry
                    // an OSC 8 run inside the block's own run — the anchor with the target as its
                    // fragment — and the anchor is re-opened straight after, so the block carries
                    // on and only these cells point anywhere. With no anchor set (every surface
                    // that is not an anchored block of terminal output) this is the old two lines.
                    const QString linkUri = labellink::uriFor(m_linkAnchor, url);
                    if (!linkUri.isEmpty()) out += osc8(linkUri);
                    out += sgr(QStringLiteral("0;") + m_palette.link) + label + style();
                    if (!linkUri.isEmpty()) out += osc8(m_linkAnchor);
                    // Outside the run on purpose: the printed target is what a person reads before
                    // clicking, and text is what survives a restore from saved bytes, where OSC 8
                    // is stripped. It scans as a link of its own exactly as it did before.
                    if (!url.isEmpty() && url != label) out += sgr(m_palette.dim) + QStringLiteral(" (") + url + QLatin1Char(')') + style();
                    // A local audio link also gets a player row; ordinary web and file links
                    // remain clickable prose. The one linked cell is emitted outside the prose
                    // run by Pane::agentProse, just like an inline picture.
                    if (m_images && !m_inlineOnly && !isForeignUrl(url)) {
                        const QString path = resolveImageTarget(url, m_imageBase);
                        const QString media = path.isEmpty() ? QString() :
                            mediaEscape(path, m_imageColumns > 0 ? m_imageColumns : 60);
                        if (!media.isEmpty()) {
                            out += kReset + QLatin1Char('\n') + media;
                            m_afterImage = true;
                        }
                    }
                    m_prev = QLatin1Char(')');
                    i = paren + 1;
                    return true;
                }
            }
        }
    }

    // Card #CVHT: the first characters of a bold run are held (bounded) until its first word can
    // be classified, so a role colour can start at the run's very first character.
    if (m_bold && m_boldRole == BoldRole::None && m_boldHold.size() < kBoldHoldMax) {
        m_boldHold += c;
        m_prev = c;
        ++i;
        if (m_boldHold.size() >= kBoldHoldMax) flushBoldHold(out);
        return true;
    }

    out += c; m_prev = c; ++i;
    return true;
}

// Card #1MGS: `![alt](target)` at m_pending[i], held on one line and bounded like a link. False
// when it needs more input. Otherwise i has moved past whatever it handled — the whole image, or
// just the `!` of an image that is to print as a link — or, when this is no image, not at all.
bool MarkdownAnsi::imageStep(QString &out, int &i) {
    const QString &text = m_pending;
    const int lineEnd = text.indexOf(QLatin1Char('\n'), i);
    const int limit = lineEnd < 0 ? text.size() : lineEnd;
    const bool open = lineEnd < 0 && !m_final && limit - i < 400;
    if (i + 1 >= limit) return !open;
    if (text.at(i + 1) != QLatin1Char('[')) return true;
    const int close = text.indexOf(QLatin1Char(']'), i + 2);
    if (close < 0 || close >= limit || close + 1 >= limit) return !open;
    if (text.at(close + 1) != QLatin1Char('(')) return true;
    const int paren = text.indexOf(QLatin1Char(')'), close + 2);
    if (paren < 0 || paren >= limit) return !open;

    const QString alt = text.mid(i + 2, close - i - 2).trimmed();
    const QString target = bareTarget(text.mid(close + 2, paren - close - 2));
    const QString path = isForeignUrl(target) ? QString() : resolveImageTarget(target, m_imageBase);
    const int columns = m_imageColumns > 0 ? std::min(m_imageColumns, int(kImageMaxColumns)) : int(kImageMaxColumns);
    const QString escape = path.isEmpty() ? QString() : imageEscape(path, columns, kImageMaxRows, m_imageCell);
    if (isForeignUrl(target) || (!path.isEmpty() && escape.isEmpty())) {
        ++i;   // a web image, or a file that is no picture: its link is what follows the `!`
        return true;
    }
    flushBoldHold(out);
    if (escape.isEmpty()) {
        // Names nothing here: say what it would have shown, and where it looked.
        if (!alt.isEmpty()) out += alt;
        if (!target.isEmpty()) out += sgr(m_palette.dim) + (alt.isEmpty() ? QString() : QStringLiteral(" ")) + QLatin1Char('(') + target + QLatin1Char(')') + style();
    } else {
        // The picture takes rows of its own: a line of its alt text, then the escape at the start
        // of the next row. Text before it on the line ends that line first.
        if (!m_prev.isNull()) out += kReset + QLatin1Char('\n');
        out += sgr(QStringLiteral("0;") + m_palette.dim) + (alt.isEmpty() ? QFileInfo(path).fileName() : alt) + kReset
               + QLatin1Char('\n') + escape;
        m_afterImage = true;
    }
    m_prev = QLatin1Char(')');
    i = paren + 1;
    return true;
}

// Classifies the held first word of a bold run and emits it in that role's colour (plain bold when
// the word matches nothing). Card #CVHT.
void MarkdownAnsi::flushBoldHold(QString &out) {
    if (m_boldHold.isEmpty()) return;
    QString word;
    for (const QChar ch : m_boldHold) {
        if (ch.isSpace()) break;
        word += ch;
    }
    while (!word.isEmpty() && !word.at(0).isLetterOrNumber()) word.remove(0, 1);
    while (!word.isEmpty() && !word.back().isLetterOrNumber()) word.chop(1);
    word = word.toLower();
    m_boldRole = BoldRole::Plain;
    if (!word.isEmpty()) {
        if (kDoneWords.contains(word)) m_boldRole = BoldRole::Done;
        else if (kNeedWords.contains(word)) m_boldRole = BoldRole::Need;
        else if (kProblemWords.contains(word)) m_boldRole = BoldRole::Problem;
    }
    out += style() + m_boldHold;
    m_boldHold.clear();
}

QString MarkdownAnsi::renderInline(const QString &text, bool bold) const {
    // A table cell is rendered by a second instance, which needs the whole palette and not just
    // the base: inline code and links inside a cell are the same colours as anywhere else.
    Palette cell = m_palette;
    if (bold) cell.base += QStringLiteral(";1");
    MarkdownAnsi inner(cell);
    // The cell is inline content, so the inner renderer may not collect a table of its own. It
    // used to: a single row with no separator line under it — `| like this |`, which an agent
    // writes by accident all the time — is "not a table after all" here, and renderTable() sent
    // the row back through renderInline(), whose inner renderer saw a line starting with `|`,
    // collected it as a table, and called renderTable() again on finish(). Relay died of a stack
    // overflow in the middle of an answer, twice on 2026-09-19 (16:30:40 and 16:36:18), with
    // 8 MiB of that cycle on the stack and nothing in the log to say so.
    inner.m_inlineOnly = true;
    inner.m_linkAnchor = m_linkAnchor;   // a link in a table cell is a link like any other (#MDKN)
    // Two statements, not `feed(text) + finish()`: the order in which `+` evaluates its operands
    // is unspecified, and g++ 15 on x86_64 runs finish() first, which flushes an empty renderer
    // and then feeds text nothing will ever emit. Every numeric table cell came out blank on that
    // toolchain while the same source was right here (aarch64, g++ 13), 2026-09-18.
    QString rendered = inner.feed(text);
    rendered += inner.finish();
    return rendered;
}

QString MarkdownAnsi::renderTable() {
    const QStringList rows = m_table;
    m_table.clear();
    static const QRegularExpression separatorCell(QStringLiteral("^:?-+:?$"));
    bool hasHeader = rows.size() >= 2;
    const QStringList separator = hasHeader ? tableCells(rows.at(1)) : QStringList();
    for (const QString &cell : separator) if (!separatorCell.match(cell).hasMatch()) hasHeader = false;

    QString out;
    if (!hasHeader) {   // not a table after all: plain lines
        for (const QString &row : rows) out += renderInline(row, false) + QLatin1Char('\n');
        return out;
    }

    QList<QStringList> cells;
    int columns = 0;
    for (int r = 0; r < rows.size(); ++r) {
        if (r == 1) continue;
        QStringList rendered;
        for (const QString &cell : tableCells(rows.at(r))) rendered << renderInline(cell, r == 0);
        columns = std::max(columns, int(rendered.size()));
        cells << rendered;
    }
    QVector<int> widths(columns, 0);
    QVector<QChar> align(columns, QLatin1Char('l'));
    for (int col = 0; col < columns && col < separator.size(); ++col) {
        const QString s = separator.at(col);
        if (s.startsWith(QLatin1Char(':')) && s.endsWith(QLatin1Char(':'))) align[col] = QLatin1Char('c');
        else if (s.endsWith(QLatin1Char(':'))) align[col] = QLatin1Char('r');
    }
    for (const QStringList &row : cells)
        for (int col = 0; col < row.size(); ++col) widths[col] = std::max(widths[col], visibleWidth(row.at(col)));

    const QString bar = sgr(m_palette.dim) + QStringLiteral(" │ ");
    for (int r = 0; r < cells.size(); ++r) {
        QString line;
        for (int col = 0; col < columns; ++col) {
            const QString cell = col < cells.at(r).size() ? cells.at(r).at(col) : QString();
            const int pad = widths.at(col) - visibleWidth(cell);
            const int before = align.at(col) == QLatin1Char('r') ? pad : align.at(col) == QLatin1Char('c') ? pad / 2 : 0;
            if (col > 0) line += bar;
            line += sgr(QStringLiteral("0;") + m_palette.base) + QString(before, QLatin1Char(' '));
            line += cell;
            line += sgr(QStringLiteral("0;") + m_palette.base) + QString(pad - before, QLatin1Char(' '));
        }
        out += line + kReset + QLatin1Char('\n');
        if (r == 0) {
            QStringList dashes;
            for (int col = 0; col < columns; ++col) dashes << QString(widths.at(col), QChar(0x2500));
            out += sgr(m_palette.dim) + dashes.join(QStringLiteral("─┼─")) + kReset + QLatin1Char('\n');
        }
    }
    return out;
}

}  // namespace relay
