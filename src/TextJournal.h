// SPDX-License-Identifier: AGPL-3.0-or-later
// A pane's text journal (card #HEY7): every line the pane's terminal let go of, kept once, with
// its formatting as a separate style layer, compressed once it stops changing.
//
// The terminal holds its newest rows itself (the engine's ring, about 10,000 rows) and the
// per-pane tail file keeps those across a restart (src/WindowState.h). A row that leaves the ring
// for good — overwritten at the limit, cleared, or cut by a smaller limit — comes here instead, and
// so does the tail when its pane is pruned for good. Journal (older) and tail (newer) are disjoint
// and in order; nothing in one is in the other.
//
// On disk, `$XDG_DATA_HOME/relay/text/<id>/`, where <id> is the pane's `scrollback` id (so a pane
// restored from its layout or from Recently closed goes on appending to the same journal):
//
//   meta.json            {"v":1, "id", "created", "updated", "lines", "cwd"}
//   seg-000001.rtj       the open segment: plain UTF-8, one JSON record per line, appended
//   seg-000001.rtj.z     a sealed segment: the same bytes as one zlib stream (RFC 1950)
//   seg-000001.rtj.xz    a sealed segment rewritten as xz by the 7-day pass (Python only)
//
// A reader takes, for each segment number, the most compressed file there is. Records (unknown
// keys are ignored, and a line that does not parse is skipped — a crash mid-append loses that line
// and nothing else):
//
//   {"s":n,"g":"1;38;5;196"}     style n is these SGR parameters (0 is the default, never defined)
//   {"k":n,"u":"relay-image:…"}  link n is this OSC 8 target (image, media and prose links only)
//   {"t":"text","r":[[col,len,s],[col,len,s,k],…],"m":marks}
//                                 one logical line (soft-wrapped rows joined); col and len count
//                                 code points; `r` and `m` are left out when empty
//   {"c":{"src":"relay","id":"…","dir":"…"}}  a conversation takes the pane; {"c":null} ends it
//   {"x":"clear"}                 the scrollback was cleared here
//   {"at":"2026-09-25T22:00:00Z"} the time, written at most once a minute
//
// Style and link tables are per segment, so any segment reads on its own.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace relay::textjournal {

// A run of one style (and link) inside a line. Positions count code points, not UTF-16 units, so
// the Python reader indexes the same text the same way.
struct Span {
    int col = 0;
    int len = 0;
    QString sgr;    // SGR parameters as the serializer writes them, without ESC [ and m; "" = default
    QString link;   // OSC 8 target, "" = none
    bool operator==(const Span &o) const { return col == o.col && len == o.len && sgr == o.sgr && link == o.link; }
};

struct Line {
    QString text;
    QVector<Span> spans;   // non-default runs only, in order, never overlapping
    quint8 marks = 0;      // PromptMark bits (engine/core/CellTypes.h)
    bool operator==(const Line &o) const { return text == o.text && spans == o.spans && marks == o.marks; }
};

// One saved row (engine/core/AnsiSerializer: CSI SGR and OSC 8 only) as text plus spans. Each SGR
// the serializer writes states the whole style from the default, so a sequence replaces the style
// rather than adding to it; `0` and an empty list are the default. Any other escape is dropped.
Line fromAnsi(const QString &ansi);
// The reverse, in exactly the form the serializer writes: the link first where both change, a
// `ESC[0m` back to the default between runs, and the closes at the end.
QString toAnsi(const Line &line);
// `b` appended to `a`, as one logical line: the continuation of a soft-wrapped row.
void join(Line *a, const Line &b);

// `$XDG_DATA_HOME/relay/text`. Empty when no data location is available.
QString rootDirectory();
// `<rootDirectory()>/<id>`; empty for an id WindowState would not name a scrollback file with.
QString journalDirectory(const QString &id);
bool exists(const QString &id);

// A sealed or open segment's records, oldest first. `.rtj` and `.rtj.z` are read here; an `.xz`
// segment (the 7-day pass) is Python's to read and yields nothing.
QVector<QJsonObject> readSegment(const QString &path);
// Every record of the journal, in order.
QVector<QJsonObject> readRecords(const QString &id);
// The journal's logical lines in order, from the last clear on (`sinceClear`) or all of them.
QVector<Line> readLines(const QString &id, bool sinceClear = false);
// meta.json's line count: how many lines the journal holds, without reading any segment.
qint64 lineCount(const QString &id);

class Writer {
public:
    // Seals whatever open segment an earlier writer left (its style tables are not known here),
    // and appends from a new segment. Nothing is created on disk until the first flush.
    explicit Writer(const QString &id);
    ~Writer();
    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;

    bool valid() const { return !m_dir.isEmpty(); }
    QString id() const { return m_id; }

    // One terminal row. A row whose `continuation` is set is the rest of the line before it, so it
    // is joined to it rather than written; the last line is held until the next row says whether
    // it goes on (or until flushPending()).
    void appendRow(const QString &ansi, bool continuation, quint8 marks);
    void appendLine(const Line &line);
    void appendClear();
    // A conversation takes the pane from here: `source` relay, claude or codex. Empty `id` ends it.
    void appendConversation(const QString &source, const QString &id, const QString &directory);

    // Write the held line too: the pane is closing, or the text is being handed over.
    void flushPending();
    // Append what is buffered to the open segment and update meta.json. Seals first when the
    // segment has reached kSealBytes or has been idle for kSealIdleSeconds.
    bool flush(QString *error = nullptr);
    // Flush, then compress the open segment (the pane closed).
    bool seal(QString *error = nullptr);

    void setDirectory(const QString &cwd) { m_cwd = cwd; }
    qint64 lines() const { return m_lines; }

    static constexpr qint64 kSealBytes = 1024 * 1024;
    static constexpr int kSealIdleSeconds = 600;

    // For tests: the clock the idle rule and the "at" records read.
    void setClockForTest(QDateTime now) { m_testNow = now; }

private:
    QDateTime now() const;
    void writeRecord(const QJsonObject &record);
    void writeLine(const Line &line);
    int styleIndex(const QString &sgr);
    int linkIndex(const QString &uri);
    void startSegment();
    bool sealOpen(QString *error);
    bool writeMeta(QString *error);
    QString segmentPath(int n) const;

    QString m_id;
    QString m_dir;
    QString m_cwd;
    int m_segment = 0;
    bool m_segmentOnDisk = false;
    qint64 m_segmentBytes = 0;
    QByteArray m_buffer;
    QHash<QString, int> m_styles;
    QHash<QString, int> m_links;
    Line m_pending;
    bool m_hasPending = false;
    qint64 m_lines = 0;
    QDateTime m_created;
    QDateTime m_lastAppend;
    QDateTime m_lastStamp;
    QDateTime m_testNow;
};

// Seal a raw segment file in place: `<path>.z` is written atomically, then `<path>` goes. Also
// what the worker's idle pass does, in Python, to a segment no writer holds.
bool sealFile(const QString &path, QString *error = nullptr);

}  // namespace relay::textjournal
