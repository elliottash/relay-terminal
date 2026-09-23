// SPDX-License-Identifier: AGPL-3.0-or-later
#include "TextMerge.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <vector>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace relay::merge {

// ----- revisions -------------------------------------------------------------------------------

Revision revisionOf(const QByteArray &bytes) {
    Revision revision;
    revision.exists = true;
    revision.size = bytes.size();
    revision.hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return revision;
}

Revision statLocalFile(const QString &path) {
    Revision revision;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return revision;
    revision.exists = true;
    revision.size = info.size();
    revision.mtimeMs = info.lastModified().toMSecsSinceEpoch();
#ifdef Q_OS_UNIX
    // An atomic replace (write a temporary, rename it over) keeps the size and can keep the mtime
    // tick; the inode is what always moves.
    struct stat buffer;
    if (::stat(QFile::encodeName(path).constData(), &buffer) == 0) revision.inode = quint64(buffer.st_ino);
#endif
    return revision;
}

Snapshot readLocalFile(const QString &path, qint64 cap) {
    Snapshot snapshot;
    const Revision stat = statLocalFile(path);
    if (!stat.exists) return snapshot;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return snapshot;
    snapshot.bytes = file.read(cap);
    // Read one byte past the cap rather than trusting the stat: the file may be growing.
    snapshot.truncated = !file.atEnd() && !file.read(1).isEmpty();
    snapshot.text = editorText(QString::fromUtf8(snapshot.bytes));
    snapshot.revision = revisionOf(snapshot.bytes);
    snapshot.revision.size = snapshot.truncated ? std::max(stat.size, cap + 1) : snapshot.bytes.size();
    snapshot.revision.mtimeMs = stat.mtimeMs;
    snapshot.revision.inode = stat.inode;
    return snapshot;
}

QString editorText(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\r')) {
            out += QLatin1Char('\n');
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
        } else if (c == QChar::LineSeparator || c == QChar::ParagraphSeparator) {
            out += QLatin1Char('\n');
        } else if (c == QChar::Nbsp) {
            out += QLatin1Char(' ');
        } else {
            out += c;
        }
    }
    return out;
}

// ----- line diff -------------------------------------------------------------------------------

namespace {

// Lines with their terminators; "" is no lines at all, and a last line without "\n" is a line.
QStringList splitLines(const QString &text) {
    QStringList lines;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            lines << text.mid(start, i + 1 - start);
            start = i + 1;
        }
    }
    if (start < text.size()) lines << text.mid(start);
    return lines;
}

// A[aStart, aEnd) is replaced by B[bStart, bEnd). Either range may be empty.
struct Hunk {
    int aStart, aEnd, bStart, bEnd;
};

// Past this many differences in the middle of two texts the diff stops looking for matches and
// calls the whole middle one change. The trace costs about D² ints, so this bounds it near 16 MB;
// the coarser answer is still correct, it only makes a merge conflict where a finer one might not.
constexpr int kMaxEditDistance = 2000;

// Myers' O((N+M)·D) diff over line ids, after stripping the common head and tail.
QVector<Hunk> diff(const std::vector<int> &a, const std::vector<int> &b) {
    const int n = int(a.size()), m = int(b.size());
    int head = 0;
    while (head < n && head < m && a[head] == b[head]) ++head;
    int tail = 0;
    while (tail < n - head && tail < m - head && a[n - 1 - tail] == b[m - 1 - tail]) ++tail;
    const int an = n - head - tail, bn = m - head - tail;
    QVector<Hunk> hunks;
    if (an == 0 && bn == 0) return hunks;
    if (an == 0 || bn == 0) {
        hunks.push_back({head, head + an, head, head + bn});
        return hunks;
    }
    auto A = [&](int i) { return a[head + i]; };
    auto B = [&](int j) { return b[head + j]; };

    const int max = std::min(an + bn, kMaxEditDistance);
    std::vector<int> v(2 * max + 3, 0);
    const int offset = max + 1;
    std::vector<std::vector<int>> trace;   // trace[d] = v[-d .. d] before step d
    bool found = false;
    for (int d = 0; d <= max && !found; ++d) {
        trace.emplace_back(v.begin() + offset - d, v.begin() + offset + d + 1);
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1])) ? v[offset + k + 1]
                                                                                   : v[offset + k - 1] + 1;
            int y = x - k;
            while (x < an && y < bn && A(x) == B(y)) { ++x; ++y; }
            v[offset + k] = x;
            if (x >= an && y >= bn) { found = true; break; }
        }
    }
    if (!found) {
        hunks.push_back({head, head + an, head, head + bn});
        return hunks;
    }

    // Walk back through the trace, collecting the matched pairs.
    std::vector<std::pair<int, int>> matches;
    int x = an, y = bn;
    for (int d = int(trace.size()) - 1; d >= 0; --d) {
        const std::vector<int> &slice = trace[size_t(d)];
        auto at = [&](int k) { return slice[size_t(k + d)]; };
        const int k = x - y;
        const int prevK = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
        const int prevX = d == 0 ? 0 : at(prevK);
        const int prevY = d == 0 ? 0 : prevX - prevK;
        while (x > prevX && y > prevY) {
            --x; --y;
            matches.emplace_back(x, y);
        }
        x = prevX;
        y = prevY;
    }
    std::reverse(matches.begin(), matches.end());
    int pa = 0, pb = 0;
    for (const auto &[i, j] : matches) {
        if (i > pa || j > pb) hunks.push_back({head + pa, head + i, head + pb, head + j});
        pa = i + 1;
        pb = j + 1;
    }
    if (pa < an || pb < bn) hunks.push_back({head + pa, head + an, head + pb, head + bn});
    return hunks;
}

struct Lines {
    QStringList base, ours, theirs;
    std::vector<int> baseIds, oursIds, theirsIds;
};

std::vector<int> idsOf(const QStringList &lines, QHash<QString, int> &table) {
    std::vector<int> ids;
    ids.reserve(size_t(lines.size()));
    for (const QString &line : lines) {
        auto it = table.find(line);
        if (it == table.end()) it = table.insert(line, int(table.size()));
        ids.push_back(*it);
    }
    return ids;
}

QString joined(const QStringList &lines, int from, int to) {
    QString out;
    for (int i = from; i < to; ++i) out += lines.at(i);
    return out;
}

void appendMarkerLine(QString &out, const QString &marker) {
    if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) out += QLatin1Char('\n');
    out += marker + QLatin1Char('\n');
}

}  // namespace

// ----- three-way merge -------------------------------------------------------------------------

MergeResult merge3(const QString &base, const QString &ours, const QString &theirs, const Labels &labels) {
    Lines l;
    l.base = splitLines(base);
    l.ours = splitLines(ours);
    l.theirs = splitLines(theirs);
    QHash<QString, int> table;
    l.baseIds = idsOf(l.base, table);
    l.oursIds = idsOf(l.ours, table);
    l.theirsIds = idsOf(l.theirs, table);
    const QVector<Hunk> O = diff(l.baseIds, l.oursIds);
    const QVector<Hunk> T = diff(l.baseIds, l.theirsIds);

    MergeResult result;
    int i = 0, j = 0, pos = 0;
    while (i < O.size() || j < T.size()) {
        const bool takeOurs = i < O.size() && (j >= T.size() || O[i].aStart <= T[j].aStart);
        const Hunk &first = takeOurs ? O[i] : T[j];
        const int gs = first.aStart;
        int ge = first.aEnd;
        bool endEmpty = first.aStart == first.aEnd;
        int oi0 = i, ti0 = j;
        (takeOurs ? i : j)++;
        // A hunk joins the group when it overlaps it, or touches its end with an insertion on
        // either side of the touch: then the order of the two changes is anybody's guess.
        auto joins = [&](const Hunk &h) {
            if (h.aStart < ge) return true;
            return h.aStart == ge && (h.aStart == h.aEnd || endEmpty);
        };
        auto absorb = [&](const Hunk &h) {
            const bool empty = h.aStart == h.aEnd;
            if (h.aEnd > ge) { ge = h.aEnd; endEmpty = empty; }
            else if (h.aEnd == ge) endEmpty = endEmpty || empty;
        };
        for (bool grew = true; grew;) {
            grew = false;
            if (i < O.size() && joins(O[i])) { absorb(O[i++]); grew = true; }
            if (j < T.size() && joins(T[j])) { absorb(T[j++]); grew = true; }
        }

        result.text += joined(l.base, pos, gs);
        // One side's text for the group: its hunks, stretched over the base lines around them.
        auto sideText = [&](const QVector<Hunk> &hunks, int from, int to, const QStringList &lines, bool *changed) {
            *changed = from < to;
            if (!*changed) return joined(l.base, gs, ge);
            const Hunk &h0 = hunks[from];
            const Hunk &h1 = hunks[to - 1];
            return joined(lines, h0.bStart - (h0.aStart - gs), h1.bEnd + (ge - h1.aEnd));
        };
        bool oursChanged = false, theirsChanged = false;
        const QString oursText = sideText(O, oi0, i, l.ours, &oursChanged);
        const QString theirsText = sideText(T, ti0, j, l.theirs, &theirsChanged);
        if (!theirsChanged || oursText == theirsText) {
            result.text += oursText;
        } else if (!oursChanged) {
            result.text += theirsText;
        } else {
            Conflict conflict;
            conflict.base = joined(l.base, gs, ge);
            conflict.ours = oursText;
            conflict.theirs = theirsText;
            if (!result.text.isEmpty() && !result.text.endsWith(QLatin1Char('\n'))) result.text += QLatin1Char('\n');
            conflict.line = int(result.text.count(QLatin1Char('\n')));
            appendMarkerLine(result.text, QStringLiteral("<<<<<<< ") + labels.ours);
            result.text += oursText;
            appendMarkerLine(result.text, QStringLiteral("||||||| ") + labels.base);
            result.text += conflict.base;
            appendMarkerLine(result.text, QStringLiteral("======="));
            result.text += theirsText;
            appendMarkerLine(result.text, QStringLiteral(">>>>>>> ") + labels.theirs);
            result.conflicts.push_back(conflict);
        }
        pos = ge;
    }
    result.text += joined(l.base, pos, int(l.base.size()));
    return result;
}

// ----- what a buffer does about a changed source ----------------------------------------------

Reconciliation reconcile(const QString &base, const QString &buffer, const QString &disk, const Labels &labels) {
    Reconciliation r;
    if (disk == base) { r.outcome = Outcome::Unchanged; return r; }
    if (buffer == disk) { r.outcome = Outcome::Converged; return r; }
    if (buffer == base) { r.outcome = Outcome::TakeDisk; r.text = disk; return r; }
    r.merge = merge3(base, buffer, disk, labels);
    r.outcome = r.merge.clean() ? Outcome::Merged : Outcome::Conflict;
    r.text = r.merge.text;
    if (r.outcome == Outcome::Merged && r.text == disk) r.outcome = Outcome::Converged;
    return r;
}

// ----- applying a new text to a buffer in place -----------------------------------------------

QVector<TextEdit> editsBetween(const QString &from, const QString &to) {
    QVector<TextEdit> edits;
    if (from == to) return edits;
    const QStringList a = splitLines(from), b = splitLines(to);
    QHash<QString, int> table;
    const QVector<Hunk> hunks = diff(idsOf(a, table), idsOf(b, table));
    // Character offsets of each line start, plus the end.
    auto starts = [](const QStringList &lines) {
        QVector<int> out(lines.size() + 1, 0);
        for (int k = 0; k < lines.size(); ++k) out[k + 1] = out[k] + int(lines.at(k).size());
        return out;
    };
    const QVector<int> sa = starts(a), sb = starts(b);
    for (const Hunk &h : hunks) {
        int pos = sa[h.aStart], end = sa[h.aEnd];
        int bpos = sb[h.bStart], bend = sb[h.bEnd];
        // Trim what the two sides of the hunk share at either end, so a one-word change leaves a
        // cursor elsewhere on that line where it was.
        while (pos < end && bpos < bend && from.at(pos) == to.at(bpos)) { ++pos; ++bpos; }
        while (end > pos && bend > bpos && from.at(end - 1) == to.at(bend - 1)) { --end; --bend; }
        if (pos == end && bpos == bend) continue;
        edits.push_back({pos, end - pos, to.mid(bpos, bend - bpos)});
    }
    return edits;
}

}  // namespace relay::merge
