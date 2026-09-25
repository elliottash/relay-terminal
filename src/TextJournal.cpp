// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TextJournal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace relay::textjournal {
namespace {

const QString kOpenSuffix = QStringLiteral(".rtj");
const QString kZlibSuffix = QStringLiteral(".rtj.z");
const QString kXzSuffix = QStringLiteral(".rtj.xz");

// The same rule WindowState applies to a `scrollback` id (isScrollbackId), kept here so this
// library needs nothing but QtCore: a hand-edited layout cannot point the journal at `../`.
bool isJournalId(const QString &id) {
    if (id.size() < 8 || id.size() > 64) return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '-';
        if (!ok) return false;
    }
    return true;
}

int codePoints(const QString &text) {
    int n = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) ++i;
        ++n;
    }
    return n;
}

// The UTF-16 index of code point `cp` in `text` (text.size() past the end).
int unitIndex(const QString &text, int cp) {
    int i = 0;
    for (int n = 0; n < cp && i < text.size(); ++n) {
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) ++i;
        ++i;
    }
    return i;
}

// The number of the segment a file name belongs to, or 0.
int segmentNumber(const QString &name) {
    if (!name.startsWith(QLatin1String("seg-"))) return 0;
    const int dot = name.indexOf(QLatin1Char('.'));
    if (dot < 5) return 0;
    bool ok = false;
    const int n = name.mid(4, dot - 4).toInt(&ok);
    return ok && n > 0 ? n : 0;
}

struct SegmentFiles {
    int number = 0;
    QString open, zlib, xz;
};

QVector<SegmentFiles> segments(const QString &dir) {
    QHash<int, SegmentFiles> byNumber;
    const QStringList names = QDir(dir).entryList({QStringLiteral("seg-*")}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        const int n = segmentNumber(name);
        if (!n) continue;
        SegmentFiles &files = byNumber[n];
        files.number = n;
        const QString path = dir + QLatin1Char('/') + name;
        if (name.endsWith(kXzSuffix)) files.xz = path;
        else if (name.endsWith(kZlibSuffix)) files.zlib = path;
        else if (name.endsWith(kOpenSuffix)) files.open = path;
    }
    QVector<SegmentFiles> out = byNumber.values().toVector();
    std::sort(out.begin(), out.end(), [](const SegmentFiles &a, const SegmentFiles &b) { return a.number < b.number; });
    return out;
}

// A zlib stream (what Python's zlib.compress writes) through qUncompress, which wants Qt's own
// 4-byte big-endian length in front. The length is only a hint to Qt, so an estimate that is too
// small still inflates; one too large for memory is refused by Qt rather than crashing.
QByteArray inflate(const QByteArray &stream, qint64 rawHint) {
    QByteArray framed;
    const quint32 hint = quint32(std::min<qint64>(std::max<qint64>(rawHint, stream.size() * 4), 0x7fffffff));
    framed.append(char((hint >> 24) & 0xff));
    framed.append(char((hint >> 16) & 0xff));
    framed.append(char((hint >> 8) & 0xff));
    framed.append(char(hint & 0xff));
    framed.append(stream);
    return qUncompress(framed);
}

QVector<QJsonObject> parseRecords(const QByteArray &bytes) {
    QVector<QJsonObject> out;
    for (const QByteArray &raw : bytes.split('\n')) {
        if (raw.trimmed().isEmpty()) continue;
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;   // a torn last line
        out.append(doc.object());
    }
    return out;
}

QString isoNow(const QDateTime &now) { return now.toUTC().toString(Qt::ISODate); }

}  // namespace

// ----- the style layer -----------------------------------------------------------------------

Line fromAnsi(const QString &ansi) {
    Line line;
    QString sgr;
    QString link;
    int col = 0;
    auto extend = [&](const QString &text) {
        if (text.isEmpty()) return;
        const int n = codePoints(text);
        line.text += text;
        if (!sgr.isEmpty() || !link.isEmpty()) {
            if (!line.spans.isEmpty()) {
                Span &last = line.spans.last();
                if (last.sgr == sgr && last.link == link && last.col + last.len == col) {
                    last.len += n;
                    col += n;
                    return;
                }
            }
            line.spans.append(Span{col, n, sgr, link});
        }
        col += n;
    };
    QString run;
    for (int i = 0; i < ansi.size(); ++i) {
        const QChar c = ansi.at(i);
        if (c.unicode() != 0x1b) {
            if (c.unicode() < 0x20 && c.unicode() != '\t') continue;   // controls are not text
            run += c;
            continue;
        }
        extend(run);
        run.clear();
        if (i + 1 >= ansi.size()) break;
        const QChar kind = ansi.at(i + 1);
        if (kind == QLatin1Char('[')) {   // CSI: parameters, then a final byte
            int k = i + 2;
            while (k < ansi.size() && ansi.at(k).unicode() >= 0x20 && ansi.at(k).unicode() <= 0x3f) ++k;
            if (k >= ansi.size()) break;
            if (ansi.at(k) == QLatin1Char('m')) {
                const QString params = ansi.mid(i + 2, k - i - 2);
                // The serializer states the whole style each time, from the default.
                sgr = (params.isEmpty() || params == QLatin1String("0")) ? QString() : params;
                if (sgr.startsWith(QLatin1String("0;"))) sgr = sgr.mid(2);
            }
            i = k;
        } else if (kind == QLatin1Char(']')) {   // OSC, to ST (ESC \) or BEL
            int k = i + 2;
            int end = -1, after = -1;
            for (; k < ansi.size(); ++k) {
                if (ansi.at(k).unicode() == 0x07) { end = k; after = k; break; }
                if (ansi.at(k).unicode() == 0x1b && k + 1 < ansi.size() && ansi.at(k + 1) == QLatin1Char('\\')) {
                    end = k;
                    after = k + 1;
                    break;
                }
            }
            if (end < 0) break;
            const QString body = ansi.mid(i + 2, end - i - 2);
            if (body.startsWith(QLatin1String("8;"))) {
                const int semi = body.indexOf(QLatin1Char(';'), 2);
                link = semi >= 0 ? body.mid(semi + 1) : QString();
            }
            i = after;
        } else {
            ++i;   // a two-byte escape: dropped
        }
    }
    extend(run);
    return line;
}

QString toAnsi(const Line &line) {
    QString out;
    QString sgr, link;
    bool emitted = false;
    auto enter = [&](const QString &nextSgr, const QString &nextLink) {
        if (nextLink != link) {
            out += QStringLiteral("\x1b]8;;") + nextLink + QStringLiteral("\x1b\\");
            link = nextLink;
        }
        if (nextSgr != sgr) {
            out += QStringLiteral("\x1b[") + (nextSgr.isEmpty() ? QStringLiteral("0") : nextSgr) + QLatin1Char('m');
            sgr = nextSgr;
            emitted = true;
        }
    };
    int col = 0;   // code points written so far
    int unit = 0;  // the matching UTF-16 index into line.text
    for (const Span &span : line.spans) {
        if (span.col > col) {
            enter(QString(), QString());
            const int to = unitIndex(line.text, span.col);
            out += line.text.mid(unit, to - unit);
            unit = to;
            col = span.col;
        }
        enter(span.sgr, span.link);
        const int to = unitIndex(line.text, span.col + span.len);
        out += line.text.mid(unit, to - unit);
        unit = to;
        col = span.col + span.len;
    }
    if (unit < line.text.size()) {
        enter(QString(), QString());
        out += line.text.mid(unit);
    }
    if (!link.isEmpty()) out += QStringLiteral("\x1b]8;;\x1b\\");
    if (emitted && !sgr.isEmpty()) out += QStringLiteral("\x1b[0m");
    return out;
}

void join(Line *a, const Line &b) {
    const int offset = codePoints(a->text);
    a->text += b.text;
    for (Span span : b.spans) {
        span.col += offset;
        if (!a->spans.isEmpty()) {
            Span &last = a->spans.last();
            if (last.sgr == span.sgr && last.link == span.link && last.col + last.len == span.col) {
                last.len += span.len;
                continue;
            }
        }
        a->spans.append(span);
    }
    a->marks |= b.marks;
}

// ----- where it lives ------------------------------------------------------------------------

QString rootDirectory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return data.isEmpty() ? QString() : data + QStringLiteral("/relay/text");
}

QString journalDirectory(const QString &id) {
    const QString root = rootDirectory();
    if (root.isEmpty() || !isJournalId(id)) return {};
    return root + QLatin1Char('/') + id;
}

bool exists(const QString &id) {
    const QString dir = journalDirectory(id);
    return !dir.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/meta.json"));
}

// ----- reading -------------------------------------------------------------------------------

QVector<QJsonObject> readSegment(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    if (path.endsWith(kZlibSuffix)) return parseRecords(inflate(bytes, bytes.size() * 8));
    if (path.endsWith(kOpenSuffix)) return parseRecords(bytes);
    return {};
}

QVector<QJsonObject> readRecords(const QString &id) {
    const QString dir = journalDirectory(id);
    if (dir.isEmpty()) return {};
    QVector<QJsonObject> out;
    for (const SegmentFiles &files : segments(dir)) {
        // The most compressed copy wins: a seal writes the new file before it removes the old.
        const QString path = !files.xz.isEmpty() ? files.xz : !files.zlib.isEmpty() ? files.zlib : files.open;
        out += readSegment(path);
    }
    return out;
}

QVector<Line> readLines(const QString &id, bool sinceClear) {
    QVector<Line> out;
    QHash<int, QString> styles, links;
    for (const QJsonObject &record : readRecords(id)) {
        if (record.contains(QStringLiteral("s"))) {
            styles.insert(record.value(QStringLiteral("s")).toInt(), record.value(QStringLiteral("g")).toString());
        } else if (record.contains(QStringLiteral("k"))) {
            links.insert(record.value(QStringLiteral("k")).toInt(), record.value(QStringLiteral("u")).toString());
        } else if (record.contains(QStringLiteral("x"))) {
            if (sinceClear) out.clear();
        } else if (record.contains(QStringLiteral("t"))) {
            Line line;
            line.text = record.value(QStringLiteral("t")).toString();
            line.marks = quint8(record.value(QStringLiteral("m")).toInt());
            for (const QJsonValue &value : record.value(QStringLiteral("r")).toArray()) {
                const QJsonArray run = value.toArray();
                if (run.size() < 3) continue;
                Span span;
                span.col = run.at(0).toInt();
                span.len = run.at(1).toInt();
                span.sgr = styles.value(run.at(2).toInt());
                span.link = run.size() > 3 ? links.value(run.at(3).toInt()) : QString();
                line.spans.append(span);
            }
            out.append(line);
        }
        // Tables are per segment; a new segment redefines what it uses from 1, so stale entries
        // left in the hashes are only ever shadowed, never read.
    }
    return out;
}

qint64 lineCount(const QString &id) {
    const QString dir = journalDirectory(id);
    if (dir.isEmpty()) return 0;
    QFile file(dir + QStringLiteral("/meta.json"));
    if (!file.open(QIODevice::ReadOnly)) return 0;
    return qint64(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("lines")).toDouble());
}

bool sealFile(const QString &path, QString *error) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return fail(in.errorString());
    const QByteArray raw = in.readAll();
    in.close();
    const QString target = path + QStringLiteral(".z");
    if (!raw.isEmpty()) {
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly)) return fail(out.errorString());
        out.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        // qCompress is Qt's 4-byte length and then a plain zlib stream; the stream is the file.
        if (out.write(qCompress(raw, 6).mid(4)) < 0 || !out.commit()) return fail(out.errorString());
    }
    QFile::remove(path);
    return true;
}

// ----- writing -------------------------------------------------------------------------------

Writer::Writer(const QString &id) : m_id(id), m_dir(journalDirectory(id)) {
    if (m_dir.isEmpty()) return;
    int last = 0;
    for (const SegmentFiles &files : segments(m_dir)) {
        last = std::max(last, files.number);
        // An open segment an earlier writer left: its tables are unknown here, so it is sealed and
        // this writer starts the next one.
        if (!files.open.isEmpty()) {
            if (!files.zlib.isEmpty() || !files.xz.isEmpty()) QFile::remove(files.open);   // sealed already
            else sealFile(files.open);
        }
    }
    m_segment = last;
    QFile meta(m_dir + QStringLiteral("/meta.json"));
    if (meta.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(meta.readAll()).object();
        m_lines = qint64(object.value(QStringLiteral("lines")).toDouble());
        m_created = QDateTime::fromString(object.value(QStringLiteral("created")).toString(), Qt::ISODate);
        m_cwd = object.value(QStringLiteral("cwd")).toString();
    }
    startSegment();
}

Writer::~Writer() {
    flushPending();
    flush();
}

QDateTime Writer::now() const { return m_testNow.isValid() ? m_testNow : QDateTime::currentDateTimeUtc(); }

QString Writer::segmentPath(int n) const {
    return m_dir + QStringLiteral("/seg-%1").arg(n, 6, 10, QLatin1Char('0')) + kOpenSuffix;
}

void Writer::startSegment() {
    ++m_segment;
    m_segmentOnDisk = false;
    m_segmentBytes = 0;
    m_styles.clear();
    m_links.clear();
    m_lastStamp = QDateTime();
}

void Writer::writeRecord(const QJsonObject &record) {
    m_buffer += QJsonDocument(record).toJson(QJsonDocument::Compact);
    m_buffer += '\n';
}

int Writer::styleIndex(const QString &sgr) {
    if (sgr.isEmpty()) return 0;
    auto it = m_styles.constFind(sgr);
    if (it != m_styles.cend()) return it.value();
    const int n = int(m_styles.size()) + 1;
    m_styles.insert(sgr, n);
    writeRecord({{QStringLiteral("s"), n}, {QStringLiteral("g"), sgr}});
    return n;
}

int Writer::linkIndex(const QString &uri) {
    if (uri.isEmpty()) return 0;
    auto it = m_links.constFind(uri);
    if (it != m_links.cend()) return it.value();
    const int n = int(m_links.size()) + 1;
    m_links.insert(uri, n);
    writeRecord({{QStringLiteral("k"), n}, {QStringLiteral("u"), uri}});
    return n;
}

void Writer::writeLine(const Line &line) {
    if (!valid()) return;
    const QDateTime t = now();
    // An idle segment is sealed before anything new goes in, so a pane left open for a day does
    // not keep a day-old segment raw.
    if (m_lastAppend.isValid() && m_lastAppend.secsTo(t) >= kSealIdleSeconds) {
        flush();
        sealOpen(nullptr);
    }
    if (!m_lastStamp.isValid() || m_lastStamp.secsTo(t) >= 60) {
        writeRecord({{QStringLiteral("at"), isoNow(t)}});
        m_lastStamp = t;
    }
    m_lastAppend = t;
    QJsonObject record{{QStringLiteral("t"), line.text}};
    if (!line.spans.isEmpty()) {
        QJsonArray runs;
        for (const Span &span : line.spans) {
            QJsonArray run{span.col, span.len, styleIndex(span.sgr)};
            if (!span.link.isEmpty()) run.append(linkIndex(span.link));
            runs.append(run);
        }
        record.insert(QStringLiteral("r"), runs);
    }
    if (line.marks) record.insert(QStringLiteral("m"), int(line.marks));
    writeRecord(record);
    ++m_lines;
}

void Writer::appendRow(const QString &ansi, bool continuation, quint8 marks) {
    Line line = fromAnsi(ansi);
    line.marks = marks;
    if (continuation && m_hasPending) {
        join(&m_pending, line);
        return;
    }
    if (m_hasPending) writeLine(m_pending);
    m_pending = line;
    m_hasPending = true;
}

void Writer::appendLine(const Line &line) {
    flushPending();
    writeLine(line);
}

void Writer::appendClear() {
    flushPending();
    if (!valid()) return;
    writeRecord({{QStringLiteral("x"), QStringLiteral("clear")}});
}

void Writer::appendConversation(const QString &source, const QString &id, const QString &directory) {
    flushPending();
    if (!valid()) return;
    if (id.isEmpty()) {
        writeRecord({{QStringLiteral("c"), QJsonValue::Null}});
        return;
    }
    QJsonObject ref{{QStringLiteral("src"), source}, {QStringLiteral("id"), id}};
    if (!directory.isEmpty()) ref.insert(QStringLiteral("dir"), directory);
    writeRecord({{QStringLiteral("c"), ref}});
}

void Writer::flushPending() {
    if (!m_hasPending) return;
    m_hasPending = false;
    writeLine(m_pending);
    m_pending = Line();
}

bool Writer::writeMeta(QString *error) {
    if (!m_created.isValid()) m_created = now();
    QJsonObject meta{{QStringLiteral("v"), 1},
                     {QStringLiteral("id"), m_id},
                     {QStringLiteral("created"), isoNow(m_created)},
                     {QStringLiteral("updated"), isoNow(now())},
                     {QStringLiteral("lines"), double(m_lines)}};
    if (!m_cwd.isEmpty()) meta.insert(QStringLiteral("cwd"), m_cwd);
    QSaveFile file(m_dir + QStringLiteral("/meta.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    file.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool Writer::flush(QString *error) {
    if (!valid() || m_buffer.isEmpty()) return true;
    if (!QDir().mkpath(m_dir)) {
        if (error) *error = QStringLiteral("Could not create %1.").arg(m_dir);
        return false;
    }
    QFile::setPermissions(QFileInfo(m_dir).absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile::setPermissions(m_dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString path = segmentPath(m_segment);
    // Someone sealed the open segment under this writer (the worker's idle pass): the tables it
    // defined went with it, so start the next segment and define them again.
    if (m_segmentOnDisk && !QFileInfo::exists(path)) {
        const QByteArray body = m_buffer;
        m_buffer.clear();
        QHash<QString, int> styles = m_styles, links = m_links;
        startSegment();
        // Re-declare every style and link the buffered records may name. Indices are kept, so the
        // buffered lines stay valid as they are.
        for (auto it = styles.cbegin(); it != styles.cend(); ++it)
            writeRecord({{QStringLiteral("s"), it.value()}, {QStringLiteral("g"), it.key()}});
        for (auto it = links.cbegin(); it != links.cend(); ++it)
            writeRecord({{QStringLiteral("k"), it.value()}, {QStringLiteral("u"), it.key()}});
        m_styles = styles;
        m_links = links;
        m_buffer += body;
        return flush(error);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(m_buffer) != m_buffer.size()) {
        if (error) *error = file.errorString();
        return false;
    }
    file.close();
    m_segmentOnDisk = true;
    m_segmentBytes += m_buffer.size();
    m_buffer.clear();
    const bool metaOk = writeMeta(error);
    if (m_segmentBytes >= kSealBytes) sealOpen(error);
    return metaOk;
}

bool Writer::sealOpen(QString *error) {
    if (!valid()) return true;
    bool ok = true;
    if (m_segmentOnDisk && QFileInfo::exists(segmentPath(m_segment))) ok = sealFile(segmentPath(m_segment), error);
    if (m_segmentOnDisk) startSegment();
    return ok;
}

bool Writer::seal(QString *error) {
    flushPending();
    if (!flush(error)) return false;
    return sealOpen(error);
}

}  // namespace relay::textjournal
