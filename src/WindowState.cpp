// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WindowState.h"
#include "TextJournal.h"

#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

namespace relay {
namespace windowstate {
namespace {

const QLatin1String kVersion("version");
const QLatin1String kSaved("saved");
const QLatin1String kWindows("windows");
const QLatin1String kGeometry("geometry");
const QLatin1String kScreen("screen");
const QLatin1String kTabs("tabs");
const QLatin1String kCurrent("current");
const QLatin1String kTitles("titles");
const QLatin1String kProject("project");
const QLatin1String kNode("node");

}  // namespace

QString defaultDirectory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/state");
}

QString defaultPath() {
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/windows.json");
}

QString defaultLockPath() {
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtime.isEmpty()) return runtime + QStringLiteral("/relay/windows.lock");
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/windows.lock");
}

QJsonObject document(const QJsonArray &windows, qint64 savedAt) {
    return {{kVersion, kSchemaVersion},
            {kSaved, savedAt > 0 ? savedAt : QDateTime::currentSecsSinceEpoch()},
            {kWindows, windows}};
}

QJsonArray windowsOf(const QJsonObject &document) { return document.value(kWindows).toArray(); }

QJsonObject windowRecord(const QRect &geometry, const QString &screen, const QJsonArray &tabs,
                         int current, const QStringList &titles) {
    QJsonObject record{{kTabs, tabs}, {kCurrent, current}};
    if (geometry.isValid())
        record.insert(kGeometry, QJsonArray{geometry.x(), geometry.y(), geometry.width(), geometry.height()});
    if (!screen.isEmpty()) record.insert(kScreen, screen);
    if (!titles.isEmpty()) record.insert(kTitles, QJsonArray::fromStringList(titles));
    return record;
}

QRect geometryOf(const QJsonObject &window) {
    const QJsonArray values = window.value(kGeometry).toArray();
    if (values.size() != 4) return {};
    const int x = values.at(0).toInt(), y = values.at(1).toInt();
    const int w = values.at(2).toInt(), h = values.at(3).toInt();
    if (w <= 0 || h <= 0) return {};
    return QRect(x, y, w, h);
}

QString screenOf(const QJsonObject &window) { return window.value(kScreen).toString(); }
QJsonArray tabsOf(const QJsonObject &window) { return window.value(kTabs).toArray(); }
int currentOf(const QJsonObject &window) { return window.value(kCurrent).toInt(); }

QStringList titlesOf(const QJsonObject &window) {
    QStringList titles;
    for (const auto &value : window.value(kTitles).toArray()) titles << value.toString();
    return titles;
}

bool write(const QString &path, const QJsonObject &state, QString *error) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (path.isEmpty()) return fail(QStringLiteral("No writable data directory for the window layout."));
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create %1.").arg(dir));
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(path);
    // QSaveFile writes a sibling temp file and renames it into place, so a reader (or a crash)
    // never sees a half-written layout.
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(QJsonDocument(state).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    if (error) error->clear();
    return true;
}

QJsonObject read(const QString &path, QString *error) {
    if (error) error->clear();
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return QJsonObject();
    };
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    QJsonParseError parse{};
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) return fail(parse.errorString());
    if (!parsed.isObject()) return fail(QStringLiteral("The saved window layout is not a JSON object."));
    const QJsonObject state = parsed.object();
    const QJsonValue version = state.value(kVersion);
    if (!version.isDouble()) return fail(QStringLiteral("The saved window layout has no schema version."));
    if (version.toInt() != kSchemaVersion)
        return fail(QStringLiteral("The saved window layout is version %1; this Relay writes version %2.")
                        .arg(version.toInt())
                        .arg(kSchemaVersion));
    if (!state.value(kWindows).isArray()) return fail(QStringLiteral("The saved window layout has no window list."));
    return state;
}

QJsonObject tabNode(const QJsonObject &tab) {
    const QJsonValue node = tab.value(kNode);
    return node.isObject() ? node.toObject() : tab;
}

QString tabProject(const QJsonObject &tab) {
    return tab.value(kNode).isObject() ? tab.value(kProject).toString() : QString();
}

QString tabTheme(const QJsonObject &tab) {
    return tab.value(kNode).isObject() ? tab.value(QStringLiteral("theme")).toString() : QString();
}

QString tabId(const QJsonObject &tab) {
    return tab.value(kNode).isObject() ? tab.value(QStringLiteral("tab_id")).toString() : QString();
}

bool isUsableNode(const QJsonObject &node, int depth) {
    if (depth > kMaxDepth || node.isEmpty()) return false;
    // A saved *tab* may be the {"project", "node"} wrapper (#JN7X): judge what is inside it. Only
    // at the top, because the wrapper is a tab's shape and never a node's; the depth still goes
    // up, so a hand-edited file cannot nest wrappers into a loop.
    if (depth == 0 && node.value(kNode).isObject()) return isUsableNode(tabNode(node), depth + 1);
    if (node.contains(QStringLiteral("split"))) {
        const QJsonArray children = node.value(QStringLiteral("children")).toArray();
        if (children.isEmpty()) return false;
        for (const auto &child : children)
            if (!child.isObject() || !isUsableNode(child.toObject(), depth + 1)) return false;
        return true;
    }
    if (node.contains(QStringLiteral("pane"))) return node.value(QStringLiteral("pane")).isObject();
    // The Switchboard is keyed on its workspace and tab, not on a file path.
    if (node.contains(QStringLiteral("board"))) {
        const QJsonObject board = node.value(QStringLiteral("board")).toObject();
        return node.value(QStringLiteral("board")).isObject()
               && !board.value(QStringLiteral("workspace")).toString().isEmpty();
    }
    // A card pane (#Y2BA) is a project and one card id; a card that has gone since is the
    // restore's problem (it says so and closes the pane), not a reason to lose the split.
    if (node.contains(QStringLiteral("card"))) {
        const QJsonObject card = node.value(QStringLiteral("card")).toObject();
        return node.value(QStringLiteral("card")).isObject()
               && !card.value(QStringLiteral("workspace")).toString().isEmpty()
               && !card.value(QStringLiteral("id")).toString().isEmpty();
    }
    // An Options or Actions pane (card #XAME) restores with whatever the catalog holds then; a
    // saved tab or row id that no longer exists is simply not revealed.
    if (node.contains(QStringLiteral("settings"))) return node.value(QStringLiteral("settings")).isObject();
    // The Test suites pane (card #7BM4) restores empty and asks its tab's board worker for the
    // inventory again, so the object may hold nothing but the project it was opened on.
    if (node.contains(QStringLiteral("testsuites"))) return node.value(QStringLiteral("testsuites")).isObject();
    // The models pane (card #MDL1 t:a11): the object holds the directory and the tab it was left
    // on, and it comes back serving the first terminal pane of its tab, so there is nothing in it
    // that can go stale. A node listed here is not merely *restorable* — a node this function does
    // not know makes its whole **split** unusable, and with it the tab: a driven quit-and-reopen
    // came back with no models pane *and no terminal*, because the pair was one split.
    if (node.contains(QStringLiteral("models"))) return node.value(QStringLiteral("models")).isObject();
    // The Sessions & Projects pane (card #8EXS): like the models pane, it comes back serving the
    // first terminal pane of its tab and re-reads its tab and search text from the object, so
    // there is nothing in it that can go stale either.
    if (node.contains(QStringLiteral("sessions"))) return node.value(QStringLiteral("sessions")).isObject();
    // The Activity pane (card #QT8C) is saved beside its owner (PaneChrome::serialize writes
    // `internals`) and buildNode() restores it empty until the next event — but this gate did not
    // know it, so a tab holding a terminal *and* an Activity pane came back as nothing (card #ACT1,
    // the same trap the models pane fell into in bb5fba2b).
    if (node.contains(QStringLiteral("internals"))) return node.value(QStringLiteral("internals")).isObject();
    // A subagent pane (card #WD83) comes back with its tabs' text; one with no tabs is not saved.
    if (node.contains(QStringLiteral("subagents"))) {
        const QJsonObject subagents = node.value(QStringLiteral("subagents")).toObject();
        return node.value(QStringLiteral("subagents")).isObject() && !subagents.value(QStringLiteral("tabs")).toArray().isEmpty();
    }
    for (const char *kind : {"explorer", "preview", "plan"}) {
        const QString key = QString::fromLatin1(kind);
        if (!node.contains(key)) continue;
        // A tool pane whose file is gone still restores: buildNode() falls back to a terminal pane.
        return node.value(key).isObject() && !node.value(key).toObject().value(QStringLiteral("path")).toString().isEmpty();
    }
    return false;
}

QJsonArray usableWindows(const QJsonObject &document) {
    QJsonArray windows;
    for (const auto &value : windowsOf(document)) {
        if (!value.isObject()) continue;
        const QJsonObject window = value.toObject();
        QJsonArray tabs;
        for (const auto &tab : tabsOf(window))
            if (tab.isObject() && isUsableNode(tab.toObject())) tabs.append(tab);
        if (tabs.isEmpty()) continue;
        QJsonObject kept = window;
        kept.insert(kTabs, tabs);
        kept.insert(kCurrent, qBound(0, currentOf(window), int(tabs.size()) - 1));
        windows.append(kept);
    }
    return windows;
}

QRect clampToScreens(const QRect &geometry, const QString &screenName, const QList<Screen> &screens) {
    if (!geometry.isValid() || screens.isEmpty()) return geometry;
    const Screen *target = nullptr;
    for (const Screen &screen : screens)
        if (!screenName.isEmpty() && screen.name == screenName && screen.available.isValid()) { target = &screen; break; }
    if (!target)
        for (const Screen &screen : screens)
            if (screen.available.isValid() && screen.available.contains(geometry.center())) { target = &screen; break; }
    if (!target)
        for (const Screen &screen : screens)
            if (screen.available.isValid() && screen.available.intersects(geometry)) { target = &screen; break; }
    if (!target)
        for (const Screen &screen : screens)
            if (screen.available.isValid()) { target = &screen; break; }
    if (!target) return geometry;
    const QRect area = target->available;
    QRect result = geometry;
    result.setWidth(std::min(result.width(), area.width()));
    result.setHeight(std::min(result.height(), area.height()));
    result.moveLeft(qBound(area.left(), result.left(), area.right() - result.width() + 1));
    result.moveTop(qBound(area.top(), result.top(), area.bottom() - result.height() + 1));
    return result;
}

QString resolveDirectory(const QString &cwd, const QString &workspace, const QString &home) {
    if (!cwd.isEmpty() && QFileInfo(cwd).isDir()) return cwd;
    if (!workspace.isEmpty() && QFileInfo(workspace).isDir()) return workspace;
    return home;
}

// ----- the terminal scrollback of a saved pane ------------------------------------------------

QString scrollbackDirectory() {
    const QString dir = defaultDirectory();
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/scrollback");
}

bool isScrollbackId(const QString &id) {
    if (id.size() < 8 || id.size() > 64) return false;
    for (const QChar c : id) {
        const ushort u = c.unicode();
        const bool ok = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '-';
        if (!ok) return false;
    }
    return true;
}

QString scrollbackPath(const QString &id) {
    const QString dir = scrollbackDirectory();
    if (dir.isEmpty() || !isScrollbackId(id)) return {};
    return dir + QLatin1Char('/') + id + QStringLiteral(".txt");
}

QStringList clampScrollback(QStringList lines, int maxLines, qint64 maxBytes) {
    // A terminal's bottom rows are usually blank; they are not worth a restart.
    while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty()) lines.removeLast();
    if (maxLines <= 0 || maxBytes <= 0) return {};
    if (lines.size() > maxLines) lines = lines.mid(lines.size() - maxLines);
    qint64 bytes = 0;
    int first = lines.size();
    // Walk back from the newest line and stop at the byte cap: the tail is what the user was
    // looking at when Relay quit.
    for (int i = lines.size() - 1; i >= 0; --i) {
        const qint64 size = qint64(lines.at(i).toUtf8().size()) + 1;
        if (bytes + size > maxBytes) break;
        bytes += size;
        first = i;
    }
    return first == 0 ? lines : lines.mid(first);
}

// The line that ends a file's rows and starts its prose trailer (#MTCS). Serialized rows contain
// printable text, CSI SGR and OSC 8 only (engine/core/AnsiSerializer.h), so no row can be this.
QString proseTrailerSeparator() {
    return QStringLiteral("\x1b_relay-prose\x1b\\");
}

namespace {

// ---- the prose trailer's records (#MTCS) -------------------------------------------------------
//
// One compact JSON object per line, the shape of relay::ProseBlock:
//   {"uri":"relay://prose/<pane>/<block>","columns":118,
//    "lines":[{"role":16,"spans":[{"text":"...","sgr":"1;35","link":"..."}]}]}
// `fg`/`bg` (#aarrggbb) and the style flags ride along when a span carries them, so a block
// round-trips exactly as it was handed to TerminalBackend::setProseBlock().

QJsonObject spanToJson(const FoldSpan &span) {
    QJsonObject object;
    object.insert(QStringLiteral("text"), span.text);
    if (!span.sgr.isEmpty()) object.insert(QStringLiteral("sgr"), span.sgr);
    if (!span.link.isEmpty()) object.insert(QStringLiteral("link"), span.link);
    if (span.fg.isValid()) object.insert(QStringLiteral("fg"), span.fg.name(QColor::HexArgb));
    if (span.bg.isValid()) object.insert(QStringLiteral("bg"), span.bg.name(QColor::HexArgb));
    if (span.bold) object.insert(QStringLiteral("bold"), true);
    if (span.italic) object.insert(QStringLiteral("italic"), true);
    if (span.underline) object.insert(QStringLiteral("underline"), true);
    if (span.strike) object.insert(QStringLiteral("strike"), true);
    if (span.dim) object.insert(QStringLiteral("dim"), true);
    if (span.reverse) object.insert(QStringLiteral("reverse"), true);
    return object;
}

QString proseToRecord(const ProseBlock &block) {
    QJsonObject object;
    object.insert(QStringLiteral("uri"), block.uri);
    object.insert(QStringLiteral("columns"), block.printColumns);
    QJsonArray lines;
    for (const FoldLine &line : block.lines) {
        QJsonObject lineObject;
        if (line.role) lineObject.insert(QStringLiteral("role"), int(line.role));
        QJsonArray spans;
        for (const FoldSpan &span : line.spans) spans.append(spanToJson(span));
        lineObject.insert(QStringLiteral("spans"), spans);
        lines.append(lineObject);
    }
    object.insert(QStringLiteral("lines"), lines);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// The reverse of proseToRecord(), minus what a hand-edited file must not be trusted with: the uri
// has to sit under kProsePrefix (a record can never register an arbitrary fold URI), the columns
// have to be a sane width, and a record that does not parse is dropped, not fatal. Reading stops
// at the caps so a pathological file costs nothing to refuse.
ProseBlock proseFromRecord(const QString &record) {
    QJsonParseError error{};
    const QJsonDocument parsed = QJsonDocument::fromJson(record.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) return {};
    const QJsonObject object = parsed.object();
    ProseBlock block;
    block.uri = object.value(QStringLiteral("uri")).toString();
    if (!block.uri.startsWith(QLatin1String(kProsePrefix))) return {};
    const int columns = object.value(QStringLiteral("columns")).toInt();
    if (columns < 1 || columns > 65535) return {};
    block.printColumns = columns;
    const QJsonArray lines = object.value(QStringLiteral("lines")).toArray();
    if (lines.size() > kScrollbackMaxLines) return {};
    for (const auto &value : lines) {
        const QJsonObject lineObject = value.toObject();
        FoldLine line;
        line.role = quint8(lineObject.value(QStringLiteral("role")).toInt());
        const QJsonArray spans = lineObject.value(QStringLiteral("spans")).toArray();
        if (spans.size() > kScrollbackMaxLines) return {};
        for (const auto &spanValue : spans) {
            const QJsonObject spanObject = spanValue.toObject();
            FoldSpan span;
            span.text = spanObject.value(QStringLiteral("text")).toString();
            span.sgr = spanObject.value(QStringLiteral("sgr")).toString();
            span.link = spanObject.value(QStringLiteral("link")).toString();
            if (spanObject.contains(QStringLiteral("fg")))
                span.fg = QColor(spanObject.value(QStringLiteral("fg")).toString());
            if (spanObject.contains(QStringLiteral("bg")))
                span.bg = QColor(spanObject.value(QStringLiteral("bg")).toString());
            span.bold = spanObject.value(QStringLiteral("bold")).toBool();
            span.italic = spanObject.value(QStringLiteral("italic")).toBool();
            span.underline = spanObject.value(QStringLiteral("underline")).toBool();
            span.strike = spanObject.value(QStringLiteral("strike")).toBool();
            span.dim = spanObject.value(QStringLiteral("dim")).toBool();
            span.reverse = spanObject.value(QStringLiteral("reverse")).toBool();
            line.spans.append(span);
        }
        block.lines.append(line);
    }
    return block;
}

// The file side of both stores: the per-pane one below and the per-session one in
// `relay::sessiontext`, which saves the same text under a different name (card #0TJ9). One copy,
// so the two can never drift apart on permissions, clamping or the empty-file rule. `prose` rides
// the trailer above; its records count against the byte cap alongside the rows.
bool writeScrollbackFile(const QString &path, const QStringList &lines,
                         const QVector<ProseBlock> &prose, QString *error,
                         int maxLines = kScrollbackMaxLines, qint64 maxBytes = kScrollbackMaxBytes) {
    auto fail = [error](const QString &text) {
        if (error) *error = text;
        return false;
    };
    if (error) error->clear();
    if (path.isEmpty()) return fail(QStringLiteral("No writable place for this scrollback."));
    QStringList records;
    qint64 reserved = 0;
    for (const ProseBlock &block : prose) {
        if (records.size() >= kScrollbackMaxProseBlocks) break;
        if (!block.uri.startsWith(QLatin1String(kProsePrefix))) continue;
        const QString record = proseToRecord(block);
        reserved += qint64(record.toUtf8().size()) + 1;
        records.append(record);
    }
    // A pathological trailer must not crowd the rows out of the file: bound it to half the byte
    // cap, dropping the oldest records (m_folds order is print order) to get there.
    while (records.size() > 1 && reserved > maxBytes / 2) {
        reserved -= qint64(records.constFirst().toUtf8().size()) + 1;
        records.removeFirst();
    }
    const QStringList kept = clampScrollback(lines, maxLines,
                                             maxBytes - proseTrailerSeparator().size() - 1 - reserved);
    if (kept.isEmpty()) {
        // Nothing to bring back: leaving the previous file would restore stale output. Prose
        // without rows is inert paint — the blocks' anchor rows are what makes them show.
        QFile::remove(path);
        return true;
    }
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) return fail(QStringLiteral("Could not create %1.").arg(dir));
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    QByteArray out = kept.join(QLatin1Char('\n')).toUtf8() + '\n';
    if (!records.isEmpty())
        out += (proseTrailerSeparator() + QLatin1Char('\n') + records.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    if (file.write(out) < 0) {
        file.cancelWriting();
        return fail(file.errorString());
    }
    if (!file.commit()) return fail(file.errorString());
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

struct ScrollbackFileText {
    QStringList rows;
    QStringList records;   // the trailer's raw record lines, separator not included
};

ScrollbackFileText readScrollbackParts(const QString &path, qint64 maxBytes = kScrollbackMaxBytes) {
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return {};
    // A file that grew past the cap (an older Relay, or an edit) is read from its tail only. The
    // trailer sits at the tail, so its records survive the cut; the rows above them may not.
    if (file.size() > maxBytes) file.seek(file.size() - maxBytes);
    QStringList lines = QString::fromUtf8(file.read(maxBytes + 1)).split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.constLast().isEmpty()) lines.removeLast();   // the trailing newline
    ScrollbackFileText text;
    const int separator = lines.indexOf(proseTrailerSeparator());
    if (separator < 0) {
        text.rows = lines;
    } else {
        text.rows = lines.mid(0, separator);
        text.records = lines.mid(separator + 1);
    }
    return text;
}

QStringList readScrollbackFile(const QString &path, int maxLines, int capLines = kScrollbackMaxLines,
                               qint64 capBytes = kScrollbackMaxBytes) {
    return clampScrollback(readScrollbackParts(path, capBytes).rows, std::min(maxLines, capLines), capBytes);
}

QVector<ProseBlock> readProseFile(const QString &path, qint64 capBytes = kScrollbackMaxBytes) {
    QVector<ProseBlock> prose;
    const QStringList records = readScrollbackParts(path, capBytes).records;
    for (const QString &record : records) {
        if (prose.size() >= kScrollbackMaxProseBlocks) break;
        ProseBlock block = proseFromRecord(record);
        if (block.uri.isEmpty() || block.lines.isEmpty()) continue;
        prose.append(block);
    }
    return prose;
}

}  // namespace

bool writeScrollback(const QString &id, const QStringList &lines, const QVector<ProseBlock> &prose,
                     QString *error) {
    const QString path = scrollbackPath(id);
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("No writable place for this pane's scrollback.");
        return false;
    }
    return writeScrollbackFile(path, lines, prose, error, kPaneTextMaxLines, kPaneTextMaxBytes);
}

QStringList readScrollback(const QString &id, int maxLines) {
    return readScrollbackFile(scrollbackPath(id), maxLines, kPaneTextMaxLines, kPaneTextMaxBytes);
}

QVector<ProseBlock> readScrollbackProse(const QString &id) {
    return readProseFile(scrollbackPath(id), kPaneTextMaxBytes);
}

bool absorbScrollback(const QString &id) {
    const QString path = scrollbackPath(id);
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    const QStringList rows = readScrollbackParts(path, kPaneTextMaxBytes).rows;
    textjournal::Writer writer(id);
    if (!writer.valid()) return false;
    // The tail's rows were saved one per terminal row; which of them wrapped is not recorded in
    // the file, so each becomes a line of its own.
    for (const QString &row : rows) writer.appendRow(row, false, 0);
    return writer.seal();
}

void removeRestoreChrome(QStringList *lines, const QStringList &marks) {
    if (!lines || lines->isEmpty()) return;
    static const QRegularExpression sgr(QStringLiteral("\\x1b\\[[0-9;:]*m"));
    static const QRegularExpression osc8(QStringLiteral("\\x1b\\]8;[^\\x1b\\x07]*(?:\\x07|\\x1b\\\\)"));
    static const QRegularExpression space(QStringLiteral("\\s+"));
    static const QRegularExpression loaded(
        QStringLiteral("^Sessionloaded(?::“.*”)?·[0-9]+turn\\(s\\)$"));
    auto compact = [&](QString text) {
        text.remove(osc8);
        text.remove(sgr);
        text.remove(space);
        return text;
    };
    QStringList compactMarks;
    for (const QString &mark : marks) compactMarks.append(compact(mark));
    const QString loadedStart = QStringLiteral("Sessionloaded");
    QStringList kept;
    kept.reserve(lines->size());
    for (int i = 0; i < lines->size();) {
        QString joined = compact(lines->at(i));
        bool possibleMark = false;
        for (const QString &mark : compactMarks)
            if (mark.startsWith(joined)) { possibleMark = true; break; }
        const bool possibleLoaded = loadedStart.startsWith(joined) || joined.startsWith(loadedStart);
        int matchedEnd = -1;
        if (!joined.isEmpty() && (possibleMark || possibleLoaded)) {
            for (int j = i; j < lines->size() && j < i + 32; ++j) {
                if (j > i) joined += compact(lines->at(j));
                if (compactMarks.contains(joined) || loaded.match(joined).hasMatch()) {
                    matchedEnd = j;
                    break;
                }
                bool markPrefix = false;
                for (const QString &mark : compactMarks)
                    if (mark.startsWith(joined)) { markPrefix = true; break; }
                if (!markPrefix && !(loadedStart.startsWith(joined) || joined.startsWith(loadedStart))) break;
                if (joined.size() > 4096) break;
            }
        }
        if (matchedEnd >= 0) {
            while (!kept.isEmpty() && kept.constLast().trimmed().isEmpty()) kept.removeLast();
            i = matchedEnd + 1;
            while (i < lines->size() && lines->at(i).trimmed().isEmpty()) ++i;
        } else {
            kept.append(lines->at(i++));
        }
    }
    *lines = kept;
}

namespace {

void collectScrollbackIds(const QJsonObject &node, QStringList *ids, int depth) {
    if (depth > kMaxDepth) return;
    if (node.contains(QStringLiteral("split"))) {
        for (const auto &child : node.value(QStringLiteral("children")).toArray())
            if (child.isObject()) collectScrollbackIds(child.toObject(), ids, depth + 1);
        return;
    }
    const QString id = node.value(QStringLiteral("pane")).toObject().value(QStringLiteral("scrollback")).toString();
    if (isScrollbackId(id) && !ids->contains(id)) ids->append(id);
}

}  // namespace

QStringList scrollbackIds(const QJsonArray &windows) {
    QStringList ids;
    for (const auto &value : windows) {
        if (!value.isObject()) continue;
        for (const auto &tab : tabsOf(value.toObject()))
            // tabNode(): an attached tab is a wrapper, and its panes' saved text must still be
            // found or the next prune deletes every one of them (#JN7X).
            if (tab.isObject()) collectScrollbackIds(tabNode(tab.toObject()), &ids, 0);
    }
    return ids;
}

int pruneScrollback(const QStringList &keep) {
    const QString dir = scrollbackDirectory();
    if (dir.isEmpty()) return 0;
    int removed = 0;
    const auto files = QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files);
    for (const QFileInfo &file : files) {
        if (keep.contains(file.completeBaseName())) continue;
        // The pane is gone for good: its tail joins the rows its journal already holds (#HEY7).
        absorbScrollback(file.completeBaseName());
        if (QFile::remove(file.absoluteFilePath())) ++removed;
    }
    return removed;
}

void removeAllScrollback() {
    const QString dir = scrollbackDirectory();
    if (dir.isEmpty()) return;
    // Restoring is off, or a fresh window set was asked for: nothing comes back into a pane,
    // but the text is still kept (#HEY7).
    const auto files = QDir(dir).entryInfoList({QStringLiteral("*.txt")}, QDir::Files);
    for (const QFileInfo &file : files) absorbScrollback(file.completeBaseName());
    QDir(dir).removeRecursively();
}

}  // namespace windowstate

// ----- the terminal text of a conversation (card #0TJ9) ---------------------------------------

namespace sessiontext {
namespace {

// Remove CSI SGR sequences so a line that was saved with formatting can still be matched by the
// plain-text rules that locate turn markers and restore marks.
QString stripSgr(const QString &text) {
    static const QRegularExpression sgr(QStringLiteral("\\x1b\\[[0-9;:]*m"));
    return QString(text).remove(sgr);
}

bool isHex(const QString &text, int from, int count, bool anyCase) {
    if (from + count > text.size()) return false;
    for (int i = from; i < from + count; ++i) {
        const ushort u = text.at(i).unicode();
        const bool lower = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f');
        if (!(lower || (anyCase && u >= 'A' && u <= 'F'))) return false;
    }
    return true;
}

// A directory Relay may put a sidecar in: absolute, and nothing that could climb out of it. The
// path comes from the worker, not from the user, but it becomes a file name here and a session
// directory carrying `..` would write outside the sessions tree.
bool isUsableDirectory(const QString &directory) {
    if (directory.isEmpty() || !QDir::isAbsolutePath(directory)) return false;
    const QString clean = QDir::cleanPath(directory);
    return clean == directory || clean + QLatin1Char('/') == directory;
}

}  // namespace

bool isSessionId(const QString &id) { return id.size() == 32 && isHex(id, 0, 32, false); }

bool isGuestId(const QString &id) {
    if (id.size() != 36) return false;
    for (int dash : {8, 13, 18, 23})
        if (id.at(dash) != QLatin1Char('-')) return false;
    return isHex(id, 0, 8, true) && isHex(id, 9, 4, true) && isHex(id, 14, 4, true)
           && isHex(id, 19, 4, true) && isHex(id, 24, 12, true);
}

bool isGuestSource(const QString &source) {
    return source == QLatin1String("claude") || source == QLatin1String("codex");
}

QString sessionPath(const QString &sessionDir, const QString &id) {
    if (!isUsableDirectory(sessionDir) || !isSessionId(id)) return {};
    return QDir::cleanPath(sessionDir) + QLatin1Char('/') + id + QStringLiteral(".scrollback.txt");
}

QString rewoundPath(const QString &sessionDir, const QString &id, int n) {
    if (n < 1 || n > 9999 || !isUsableDirectory(sessionDir) || !isSessionId(id)) return {};
    return QDir::cleanPath(sessionDir) + QLatin1Char('/') + id
           + QStringLiteral(".rewound-%1.scrollback.txt").arg(n);
}

QString guestDirectory() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data.isEmpty()) return {};
    return data + QStringLiteral("/relay/sessions/guests");
}

QString guestPath(const QString &source, const QString &id) {
    const QString dir = guestDirectory();
    if (dir.isEmpty() || !isGuestSource(source) || !isGuestId(id)) return {};
    return dir + QLatin1Char('/') + source + QLatin1Char('/') + id + QStringLiteral(".scrollback.txt");
}

bool write(const QString &path, const QStringList &lines, const QVector<ProseBlock> &prose,
          QString *error) {
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("No writable place for this conversation's terminal text.");
        return false;
    }
    return windowstate::writeScrollbackFile(path, lines, prose, error);
}

QStringList read(const QString &path, int maxLines) {
    return windowstate::readScrollbackFile(path, maxLines);
}

QVector<ProseBlock> readProse(const QString &path) {
    return windowstate::readProseFile(path);
}

QStringList sidecars(const QString &sessionDir, const QString &id) {
    const QString text = sessionPath(sessionDir, id);
    if (text.isEmpty()) return {};
    QStringList found;
    if (QFileInfo::exists(text)) found << text;
    const auto rewound = QDir(QDir::cleanPath(sessionDir))
                             .entryInfoList({id + QStringLiteral(".rewound-*.scrollback.txt")}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : rewound) found << file.absoluteFilePath();
    return found;
}

bool hasContent(const QStringList &lines, const QStringList &marks) {
    static const QRegularExpression space(QStringLiteral("\\s+"));
    static const QRegularExpression osc(QStringLiteral("\\x1b\\][^\\x1b\\x07]*(?:\\x07|\\x1b\\\\)"));
    static const QRegularExpression prompt(QStringLiteral("[\\w.+-]+@[\\w.+-]+:[^$#%\\s]*[$#%]"));
    QString text;
    for (const QString &line : lines) text += stripSgr(line).remove(osc);
    text.remove(space);
    for (QString mark : marks) {
        mark.remove(space);
        if (!mark.isEmpty()) text.remove(mark);
    }
    text.remove(prompt);
    return !text.isEmpty();
}

int turnStart(const QStringList &lines, const QString &prompt) {
    static const QString marker = QStringLiteral("✦ ");   // scrollback saved with the old glyph
    const QString first = prompt.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (first.isEmpty()) return -1;
    for (int i = lines.size() - 1; i >= 0; --i) {
        QString plain = stripSgr(lines.at(i));
        const bool marked = plain.startsWith(marker);
        if (marked) plain = plain.mid(marker.size());
        // The printed line is the prompt cut at the pane's width, so it is a prefix of it — and
        // the pane's other lines ("✦ the command finished …", lines of the agent's own prose) are
        // not, which is what keeps them from being mistaken for a turn.
        const QString shown = plain.trimmed();
        if (shown.isEmpty() || !first.startsWith(shown)) continue;
        // An unmarked short prefix could be a line of the agent's own prose ("ok", "Continue");
        // take it only when it is the whole first line or long enough that the width cut it.
        if (!marked && shown.size() < first.size() && shown.size() < 20) continue;
        return i;
    }
    return -1;
}

}  // namespace sessiontext
}  // namespace relay
