// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mock-ups for the per-pane resource meter (issue #D03W), owner 2026-09-19: "the cpu / mem bar
// things are ugly and unintuitive. i think it should be numbers. explore and mock it up."
//
// This is a throwaway renderer, not part of Relay. It paints a fake pane header row and a fake
// tab in the real palettes (data/theme/themes/relay-{dark,light}.toml, parsed here) at the real
// sizes, so the candidates can be laid beside the live screenshots and compared honestly.
//
//   usage-mockup <theme-dir> <out-dir>
//
// Variant 0 is a replica of what PaneChrome::PaneUsageChip paints today — the die and memory
// module glyphs are copied from paintDie()/paintModule() line for line — so the sheet's first
// row can be checked against the live capture and the rest read against it.

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QTextStream>

#include <cmath>

// ----- the palette, read from the theme file so nothing is a hard-coded guess -------------------

struct Tokens {
    QColor background, surface, surfaceRaised, border, borderStrong;
    QColor text, muted, accent, shell, warning, error;
};

static Tokens readTheme(const QString &path)
{
    Tokens t;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qFatal("cannot read %s", qPrintable(path));
    }
    QString section;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith('#') || line.isEmpty()) continue;
        if (line.startsWith('[')) { section = line.mid(1, line.indexOf(']') - 1); continue; }
        if (section != QLatin1String("ui")) continue;
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1).trimmed();
        value.remove('"');
        const QColor c(value);
        if (!c.isValid()) continue;
        if (key == QLatin1String("background")) t.background = c;
        else if (key == QLatin1String("surface")) t.surface = c;
        else if (key == QLatin1String("surface_raised")) t.surfaceRaised = c;
        else if (key == QLatin1String("border")) t.border = c;
        else if (key == QLatin1String("border_strong")) t.borderStrong = c;
        else if (key == QLatin1String("text")) t.text = c;
        else if (key == QLatin1String("text_muted")) t.muted = c;
        else if (key == QLatin1String("accent")) t.accent = c;
        else if (key == QLatin1String("shell")) t.shell = c;
        else if (key == QLatin1String("warning")) t.warning = c;
        else if (key == QLatin1String("error")) t.error = c;
    }
    return t;
}

// ----- one reading ------------------------------------------------------------------------------

struct Sample {
    bool hasCpu = false;
    bool hasMemory = false;
    double cpu = 0.0;        // % of the whole machine
    double ramPercent = 0.0; // % of physical memory
    double ramGb = 0.0;      // absolute resident, decimal GB
};

// The chip's own thresholds (PaneChrome.h): plain ink, warning at 60, error at 85.
static QColor ink(const Tokens &t, double value) { return value >= 85 ? t.error : value >= 60 ? t.warning : t.muted; }

// ----- the candidates ----------------------------------------------------------------------------

enum Variant { Today, Words, NumberFirst, Bare, AbsoluteMem, WordsAbsolute, VariantCount };

static const char *variantId(int v)
{
    switch (v) {
    case Today: return "0-today";
    case Words: return "a-words";
    case NumberFirst: return "b-number-first";
    case Bare: return "c-bare";
    case AbsoluteMem: return "d-absolute-memory";
    default: return "e-words-absolute-memory";
    }
}

static const char *variantName(int v)
{
    switch (v) {
    case Today: return "0 · today — die and memory-module glyphs, two bare percents";
    case Words: return "a · cpu 12% · mem 3% — plain words, body face, muted ink";
    case NumberFirst: return "b · 12% cpu  3% mem — the number first";
    case Bare: return "c · 12% · 3% — numbers alone, labels in the tooltip";
    case AbsoluteMem: return "d · cpu 12% · 1.4 GB — memory as an absolute figure";
    default: return "e · cpu 12% · mem 1.4 GB — a's words with d's memory figure";
    }
}

// A run of the chip: some text in one ink, or one of the two painted glyphs.
struct Seg {
    QString text;
    QColor ink;
    int glyph = 0;   // 0 none, 1 processor die, 2 memory module
};

static QString pct(double v) { return QString::number(int(std::lround(v))) + QStringLiteral("%"); }
static QString gb(double v)
{
    return v >= 10.0 ? QString::number(v, 'f', 0) + QStringLiteral(" GB")
                     : QString::number(v, 'f', 1) + QStringLiteral(" GB");
}

// What the chip is made of, for one variant and one reading. `narrow` is the header ladder's last
// rung (relay::panes::UsageForm::CpuOnly): the memory half and its separator go together.
static QList<Seg> segments(int v, const Sample &s, const Tokens &t, bool narrow)
{
    QList<Seg> out;
    const bool cpu = s.hasCpu;
    const bool mem = s.hasMemory && !narrow;
    if (!cpu && !mem) return out;
    const QColor ci = ink(t, s.cpu), mi = ink(t, s.ramPercent);
    switch (v) {
    case Today:
        if (cpu) { out << Seg{{}, ci, 1} << Seg{pct(s.cpu), ci}; }
        if (mem) { out << Seg{{}, mi, 2} << Seg{pct(s.ramPercent), mi}; }
        break;
    case Words:
        if (cpu) out << Seg{QStringLiteral("cpu "), t.muted} << Seg{pct(s.cpu), ci};
        if (cpu && mem) out << Seg{QStringLiteral(" · "), t.muted};
        if (mem) out << Seg{QStringLiteral("mem "), t.muted} << Seg{pct(s.ramPercent), mi};
        break;
    case NumberFirst:
        if (cpu) out << Seg{pct(s.cpu), ci} << Seg{QStringLiteral(" cpu"), t.muted};
        if (cpu && mem) out << Seg{QStringLiteral("   "), t.muted};
        if (mem) out << Seg{pct(s.ramPercent), mi} << Seg{QStringLiteral(" mem"), t.muted};
        break;
    case Bare:
        // Numbers alone. One half on its own has to be named or it says nothing — which is the
        // whole argument about this one, so the fallback is drawn rather than hidden.
        if (cpu && mem) out << Seg{pct(s.cpu), ci} << Seg{QStringLiteral(" · "), t.muted} << Seg{pct(s.ramPercent), mi};
        else if (cpu) out << Seg{pct(s.cpu), ci} << Seg{QStringLiteral(" cpu"), t.muted};
        else out << Seg{pct(s.ramPercent), mi} << Seg{QStringLiteral(" mem"), t.muted};
        break;
    case AbsoluteMem:
        // The memory half is a byte figure, which names itself: no "mem" is needed beside it.
        if (cpu) out << Seg{QStringLiteral("cpu "), t.muted} << Seg{pct(s.cpu), ci};
        if (cpu && mem) out << Seg{QStringLiteral(" · "), t.muted};
        if (mem) out << Seg{gb(s.ramGb), mi};
        break;
    default:
        // a's grammar with d's memory figure: every half is named, and the named half a person
        // could not read a percent of is the one that becomes a size.
        if (cpu) out << Seg{QStringLiteral("cpu "), t.muted} << Seg{pct(s.cpu), ci};
        if (cpu && mem) out << Seg{QStringLiteral(" · "), t.muted};
        if (mem) out << Seg{QStringLiteral("mem "), t.muted} << Seg{gb(s.ramGb), mi};
        break;
    }
    return out;
}

// The tab-label suffix each variant would carry (relay::usage::tabSuffix today).
static QString tabSuffix(int v, const Sample &s)
{
    if (!s.hasCpu && !s.hasMemory) return {};
    const QString lead = QStringLiteral("  ·  ");
    const bool both = s.hasCpu && s.hasMemory;
    switch (v) {
    case Today:
        if (both) return lead + pct(s.cpu) + QStringLiteral(" / ") + pct(s.ramPercent);
        return lead + (s.hasCpu ? pct(s.cpu) + QStringLiteral(" cpu") : pct(s.ramPercent) + QStringLiteral(" mem"));
    case Words:
        if (both) return lead + QStringLiteral("cpu ") + pct(s.cpu) + QStringLiteral(" · mem ") + pct(s.ramPercent);
        return lead + (s.hasCpu ? QStringLiteral("cpu ") + pct(s.cpu) : QStringLiteral("mem ") + pct(s.ramPercent));
    case NumberFirst:
        if (both) return lead + pct(s.cpu) + QStringLiteral(" cpu   ") + pct(s.ramPercent) + QStringLiteral(" mem");
        return lead + (s.hasCpu ? pct(s.cpu) + QStringLiteral(" cpu") : pct(s.ramPercent) + QStringLiteral(" mem"));
    case Bare:
        if (both) return lead + pct(s.cpu) + QStringLiteral(" · ") + pct(s.ramPercent);
        return lead + (s.hasCpu ? pct(s.cpu) + QStringLiteral(" cpu") : pct(s.ramPercent) + QStringLiteral(" mem"));
    case AbsoluteMem:
        if (both) return lead + QStringLiteral("cpu ") + pct(s.cpu) + QStringLiteral(" · ") + gb(s.ramGb);
        return lead + (s.hasCpu ? QStringLiteral("cpu ") + pct(s.cpu) : gb(s.ramGb));
    default:
        if (both) return lead + QStringLiteral("cpu ") + pct(s.cpu) + QStringLiteral(" · mem ") + gb(s.ramGb);
        return lead + (s.hasCpu ? QStringLiteral("cpu ") + pct(s.cpu) : QStringLiteral("mem ") + gb(s.ramGb));
    }
}

// ----- painting ----------------------------------------------------------------------------------

// Copied from PaneChrome.h so row 0 of every sheet is what is on screen today, not an impression
// of it: a processor die (a square with a smaller square inside, pins on the sides) and a memory
// module (a body with pins along its bottom edge), both 13 px.
static constexpr int kGlyph = 13;

static void paintDie(QPainter &p, const QRectF &r, const QColor &pen)
{
    p.setPen(QPen(pen, 1.1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(r.center().x() - 3, r.center().y() - 3, 6, 6), 1.5, 1.5);
    for (int k = -1; k <= 1; ++k) {
        const qreal y = r.center().y() + k * 2.4;
        p.drawLine(QPointF(r.left() + 0.5, y), QPointF(r.center().x() - 3, y));
        p.drawLine(QPointF(r.center().x() + 3, y), QPointF(r.right() - 0.5, y));
    }
}

static void paintModule(QPainter &p, const QRectF &r, const QColor &pen)
{
    p.setPen(QPen(pen, 1.1));
    p.setBrush(Qt::NoBrush);
    const QRectF body(r.left() + 1, r.top() + 1.5, r.width() - 2, r.height() - 5.5);
    p.drawRoundedRect(body, 1.2, 1.2);
    for (int k = 0; k < 4; ++k) {
        const qreal x = body.left() + 1.5 + k * 2.6;
        if (x >= body.right() - 0.5) break;
        p.drawLine(QPointF(x, body.bottom()), QPointF(x, r.bottom() - 0.5));
    }
}

static int chipWidth(const QList<Seg> &segs, const QFontMetrics &fm)
{
    if (segs.isEmpty()) return 0;
    int w = 0;
    bool afterPair = false;
    for (const Seg &s : segs) {
        if (s.glyph) {
            if (afterPair) w += 10;      // the gap between the chip's two halves, as today
            w += kGlyph + 4;
            afterPair = true;
        } else {
            w += fm.horizontalAdvance(s.text);
        }
    }
    return w;
}

static void paintChip(QPainter &p, int x, const QRect &row, const QList<Seg> &segs, const QFontMetrics &fm)
{
    bool afterPair = false;
    for (const Seg &s : segs) {
        if (s.glyph) {
            if (afterPair) x += 10;
            const QRectF r(x, row.center().y() - 6, kGlyph, kGlyph);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (s.glyph == 1) paintDie(p, r, s.ink); else paintModule(p, r, s.ink);
            x += kGlyph + 4;
            afterPair = true;
        } else {
            p.setPen(s.ink);
            const int w = fm.horizontalAdvance(s.text);
            p.drawText(QRect(x, row.top(), w + 2, row.height()), Qt::AlignLeft | Qt::AlignVCenter, s.text);
            x += w;
        }
    }
}

// A terminal pane's header row: the state glyph, the state's word, the chip, the title, then the
// directory line and the pane buttons on the right (src/Pane.h, buildStatus in src/PaneChrome.h).
static void paintHeader(QPainter &p, const QRect &box, const Tokens &t, const QFont &body,
                        int variant, const Sample &s, bool narrow,
                        const QString &word = QStringLiteral("Command running"))
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath frame;
    frame.addRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.fillPath(frame, t.background);
    p.setPen(QPen(t.border, 1));
    p.drawPath(frame);

    const QRect row = box.adjusted(10, 0, -10, 0);
    p.setFont(body);
    const QFontMetrics fm(body);
    QFont small = body;
    small.setPointSizeF(9.0);                 // theme::FloorPt: the directory line
    QFont bold = body;
    bold.setWeight(QFont::DemiBold);

    int x = row.left();
    // The state glyph: a ring, as a settled terminal pane draws it.
    p.setPen(QPen(t.shell, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(x + 1, row.center().y() - 4, 8, 8));
    x += 18;

    if (!narrow) {                            // the word is rung 3 of the ladder; a narrow pane drops it
        p.setFont(bold);
        p.setPen(t.shell);
        const int w = QFontMetrics(bold).horizontalAdvance(word);
        p.drawText(QRect(x, row.top(), w, row.height()), Qt::AlignLeft | Qt::AlignVCenter, word);
        x += w + 12;
        p.setFont(body);
    }

    const QList<Seg> segs = segments(variant, s, t, narrow);
    const int cw = chipWidth(segs, fm);
    if (cw > 0) {
        paintChip(p, x, row, segs, fm);
        x += cw + 14;
    }

    p.setFont(bold);
    p.setPen(t.text);
    p.drawText(QRect(x, row.top(), 200, row.height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("project"));
    p.setFont(body);

    // The right-hand end: the buttons, then the directory line to their left.
    int bx = row.right();
    const QStringList buttons{QStringLiteral("×"), QStringLiteral("⇱"), QStringLiteral("⊞")};
    p.setPen(t.muted);
    for (const QString &b : buttons) {
        const int w = fm.horizontalAdvance(b);
        bx -= w + 10;
        p.drawText(QRect(bx, row.top(), w + 2, row.height()), Qt::AlignLeft | Qt::AlignVCenter, b);
    }
    if (!narrow) {
        p.setFont(small);
        const QFontMetrics sfm(small);
        const QString dir = QStringLiteral("TERMINAL  ~/project");
        const int w = sfm.horizontalAdvance(dir);
        p.drawText(QRect(bx - w - 14, row.top(), w + 2, row.height()), Qt::AlignLeft | Qt::AlignVCenter, dir);
    }
    p.restore();
}

// The tab bar: the tab's label carries the suffix, which is the meter's other surface.
static void paintTab(QPainter &p, const QRect &box, const Tokens &t, const QFont &body,
                     int variant, const Sample &s)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(box, t.surface);
    QFont bold = body;
    const QString label = QStringLiteral("project") + tabSuffix(variant, s);
    const QFontMetrics fm(bold);
    const int w = fm.horizontalAdvance(label) + 74;
    const QRect tab(box.left() + 30, box.top() + 3, w, box.height() - 3);
    p.fillRect(tab, t.background);
    p.setPen(QPen(t.accent, 2));
    p.drawLine(tab.left(), tab.bottom() - 1, tab.right(), tab.bottom() - 1);
    p.setFont(bold);
    p.setPen(t.shell);
    p.drawText(QRect(tab.left() + 12, tab.top(), 12, tab.height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("▸"));
    p.setPen(t.text);
    p.drawText(QRect(tab.left() + 28, tab.top(), w, tab.height()), Qt::AlignLeft | Qt::AlignVCenter, label);
    p.setPen(t.muted);
    p.drawText(QRect(tab.right() - 22, tab.top(), 14, tab.height()), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("×"));
    p.restore();
}

// ----- the sheets ---------------------------------------------------------------------------------

struct Scene {
    const char *caption;
    Sample sample;
    bool narrow = false;
    bool tab = false;
    const char *word = "Command running";
};

// A 48 GB machine, so the absolute figures below are the ones these readings would really print.
static Sample normalSample() { return {true, true, 12.0, 3.0, 1.4}; }
static Sample busySample()   { return {true, true, 91.0, 62.0, 29.8}; }
static Sample cpuOnly()      { return {true, false, 12.0, 0.0, 0.0}; }
static Sample memOnly()      { return {false, true, 0.0, 3.0, 1.4}; }
static Sample idle()         { return {false, false, 0.0, 0.0, 0.0}; }

static QImage sheetFor(int variant, const Tokens &t, const QFont &body, const QString &title)
{
    const QList<Scene> scenes{
        {"quiet — nothing worth reading, so nothing is drawn", idle(), false, false, "Ready"},
        {"normal — 12 % of the machine, 1.4 GB of 48 GB resident", normalSample(), false, false},
        {"high — the warning and error inks (60 % and 85 %)", busySample(), false, false},
        {"CPU alone — memory under the 256 MiB floor", cpuOnly(), false, false},
        {"memory alone — an idle shell holding a big file", memOnly(), false, false},
        {"narrow pane — the ladder's last rung: CPU only", normalSample(), true, false},
        {"the tab label's suffix", normalSample(), false, true},
        {"the tab label, high", busySample(), false, true},
    };

    const int width = 880;
    const int rowH = 34, capH = 20, gap = 12;
    const int headH = 52;
    QImage img(width, headH + int(scenes.size()) * (capH + rowH + gap) + 12, QImage::Format_ARGB32);
    img.fill(t.surface);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont heading = body;
    heading.setWeight(QFont::DemiBold);
    p.setFont(heading);
    p.setPen(t.text);
    p.drawText(QRect(16, 12, width - 32, 24), Qt::AlignLeft | Qt::AlignVCenter, title);

    QFont cap = body;
    cap.setPointSizeF(9.0);
    int y = headH;
    for (const Scene &sc : scenes) {
        p.setFont(cap);
        p.setPen(t.muted);
        p.drawText(QRect(16, y, width - 32, capH), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(sc.caption));
        const QRect box(16, y + capH, sc.narrow ? 330 : width - 32, rowH);
        if (sc.tab) paintTab(p, box, t, body, variant, sc.sample);
        else paintHeader(p, box, t, body, variant, sc.sample, sc.narrow, QString::fromUtf8(sc.word));
        y += capH + rowH + gap;
    }
    p.end();
    return img;
}

// One sheet with every candidate in its normal state, for the side-by-side look.
static QImage compareSheet(const Tokens &t, const QFont &body, const QString &title)
{
    const int width = 880, rowH = 34, capH = 20, gap = 14, headH = 52;
    QImage img(width, headH + VariantCount * 2 * (capH + rowH + gap) + 12, QImage::Format_ARGB32);
    img.fill(t.surface);
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont heading = body;
    heading.setWeight(QFont::DemiBold);
    p.setFont(heading);
    p.setPen(t.text);
    p.drawText(QRect(16, 12, width - 32, 24), Qt::AlignLeft | Qt::AlignVCenter, title);
    QFont cap = body;
    cap.setPointSizeF(9.0);
    int y = headH;
    for (int v = 0; v < VariantCount; ++v) {
        for (int pass = 0; pass < 2; ++pass) {
            const Sample s = pass == 0 ? normalSample() : busySample();
            p.setFont(cap);
            p.setPen(t.muted);
            const QString c = pass == 0 ? QString::fromUtf8(variantName(v))
                                        : QStringLiteral("      …the same reading at 91 % / 62 %");
            p.drawText(QRect(16, y, width - 32, capH), Qt::AlignLeft | Qt::AlignVCenter, c);
            paintHeader(p, QRect(16, y + capH, width - 32, rowH), t, body, v, s, false);
            y += capH + rowH + gap;
        }
    }
    p.end();
    return img;
}

static void save(const QImage &img, const QString &path)
{
    img.save(path);
    img.scaled(img.width() * 2, img.height() * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation)
        .save(QString(path).replace(QStringLiteral(".png"), QStringLiteral("-2x.png")));
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString themeDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("data/theme/themes");
    const QString outDir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral(".");

    // theme::applyTheme() raises a generic 9pt desktop default to BodyPt; do the same here.
    QFont body = app.font();
    if (body.pointSizeF() > 0 && body.pointSizeF() < 10.0) body.setPointSizeF(10.0);
    else if (body.pointSizeF() <= 0) body.setPointSizeF(10.0);

    {   // What each candidate costs the header row, in px, at the real face and size.
        const Tokens t = readTheme(themeDir + QStringLiteral("/relay-dark.toml"));
        const QFontMetrics fm(body);
        QTextStream err(stderr);
        err << "chip width (px): variant  wide  cpu-only\n";
        for (int v = 0; v < VariantCount; ++v)
            err << "  " << variantId(v) << "  " << chipWidth(segments(v, normalSample(), t, false), fm)
                << "  " << chipWidth(segments(v, normalSample(), t, true), fm) << "\n";
        err.flush();
    }

    for (const QString &palette : {QStringLiteral("relay-dark"), QStringLiteral("relay-light")}) {
        const Tokens t = readTheme(themeDir + QStringLiteral("/") + palette + QStringLiteral(".toml"));
        save(compareSheet(t, body, QStringLiteral("Pane usage meter · all candidates · ") + palette),
             outDir + QStringLiteral("/mock-") + palette + QStringLiteral("-all.png"));
        for (int v = 0; v < VariantCount; ++v)
            save(sheetFor(v, t, body, QString::fromUtf8(variantName(v)) + QStringLiteral("  ·  ") + palette),
                 outDir + QStringLiteral("/mock-") + palette + QStringLiteral("-") + QString::fromUtf8(variantId(v)) + QStringLiteral(".png"));
    }
    return 0;
}
